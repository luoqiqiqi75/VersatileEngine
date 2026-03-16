#include "ve/service/rescue.h"

#include <execinfo.h>
#include <signal.h>
#include <unistd.h>
#include <cxxabi.h>
#include <dlfcn.h>
#include <pthread.h>
#include <dirent.h>
#include <sstream>
#include <vector>
#include <string>
#include <cstring>
#include <sys/syscall.h>
#include <cstdio>
#include <memory>
#include <array>
#include <link.h>

// ---------------------------------------------------------------------------
// Helper: get all thread IDs of current process
// ---------------------------------------------------------------------------
static std::vector<pid_t> GetAllThreadIds()
{
    std::vector<pid_t> threadIds;
    std::string taskPath = "/proc/" + std::to_string(getpid()) + "/task";
    DIR* dir = opendir(taskPath.c_str());

    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (entry->d_type == DT_DIR) {
                std::string name = entry->d_name;
                if (name != "." && name != "..") {
                    try { threadIds.push_back(std::stoi(name)); }
                    catch (...) {}
                }
            }
        }
        closedir(dir);
    }
    return threadIds;
}

// ---------------------------------------------------------------------------
// C++ symbol demangling
// ---------------------------------------------------------------------------
static std::string DemangleName(const char* symbol)
{
    std::string s(symbol);
    size_t funcStart = s.find('(');
    size_t funcEnd   = s.find('+', funcStart);

    if (funcStart != std::string::npos && funcEnd != std::string::npos) {
        std::string funcName = s.substr(funcStart + 1, funcEnd - funcStart - 1);
        int status = 0;
        char* demangled = abi::__cxa_demangle(funcName.c_str(), nullptr, nullptr, &status);
        if (status == 0 && demangled) {
            std::string result = s.substr(0, funcStart + 1) + demangled + s.substr(funcEnd);
            free(demangled);
            return result;
        }
    }
    return s;
}

// ---------------------------------------------------------------------------
// Module base address for PIE executables
// ---------------------------------------------------------------------------
static uintptr_t GetModuleBaseAddress(const char* modulePath)
{
    FILE* maps = fopen("/proc/self/maps", "r");
    if (!maps) return 0;

    const char* targetName = strrchr(modulePath, '/');
    targetName = targetName ? targetName + 1 : modulePath;

    char line[512];
    uintptr_t baseAddr = 0;
    while (fgets(line, sizeof(line), maps)) {
        uintptr_t start;
        char perms[16], offset[32], path[256];
        if (sscanf(line, "%lx-%*x %s %s %*s %*s %255s", &start, perms, offset, path) >= 4) {
            if (strstr(path, targetName) && strcmp(offset, "00000000") == 0) {
                baseAddr = start;
                break;
            }
        }
    }
    fclose(maps);
    return baseAddr;
}

// ---------------------------------------------------------------------------
// Batch addr2line for source locations (PIE-aware)
// ---------------------------------------------------------------------------
static std::vector<std::string> GetSourceLocations(const std::vector<void*>& addresses, const char* modulePath)
{
    std::vector<std::string> results(addresses.size());
    if (!modulePath || addresses.empty()) return results;

    uintptr_t baseAddr = GetModuleBaseAddress(modulePath);

    std::ostringstream cmd;
    cmd << "addr2line -e '" << modulePath << "' -f -C -i -s -p";
    for (void* addr : addresses) {
        uintptr_t a = (uintptr_t)addr;
        if (baseAddr > 0 && a > baseAddr) cmd << " 0x" << std::hex << (a - baseAddr);
        else                               cmd << " 0x" << std::hex << a;
    }
    cmd << " 2>/dev/null";

    FILE* pipe = popen(cmd.str().c_str(), "r");
    if (!pipe) return results;

    char buffer[1024];
    size_t idx = 0;
    while (fgets(buffer, sizeof(buffer), pipe) && idx < addresses.size()) {
        std::string line = buffer;
        if (!line.empty() && line.back() == '\n') line.pop_back();
        if (line.find("?? ??:0") == std::string::npos &&
            line.find("??:?")    == std::string::npos && !line.empty())
            results[idx] = line;
        idx++;
    }
    pclose(pipe);
    return results;
}

// ---------------------------------------------------------------------------
// Print stack trace for a given thread (current thread only)
// ---------------------------------------------------------------------------
static std::string PrintThreadStack(pid_t threadId)
{
    std::ostringstream oss;
    pid_t currentTid = syscall(SYS_gettid);

    oss << "thread " << threadId;
    if (threadId != currentTid) {
        oss << " (cannot backtrace other threads in normal mode, current: "
            << currentTid << ")\n";
        return oss.str();
    }
    oss << " trace stack:\n";

    constexpr int MAX_FRAMES = 128;
    void* buffer[MAX_FRAMES];
    int nptrs = backtrace(buffer, MAX_FRAMES);
    if (nptrs <= 0) { oss << "  Error: cannot get backtrace\n"; return oss.str(); }

    char** strings = backtrace_symbols(buffer, nptrs);
    if (!strings)   { oss << "  Error: cannot get backtrace symbols\n"; return oss.str(); }

    // Resolve exe path
    char exePath[512] = {};
    ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (len > 0) exePath[len] = '\0';

    // Collect per-frame info via dladdr
    struct FrameInfo { void* addr; const char* modulePath; std::string funcName; };
    std::vector<FrameInfo> frames(nptrs);

    for (int i = 0; i < nptrs; i++) {
        frames[i].addr = buffer[i];
        Dl_info info;
        if (dladdr(buffer[i], &info)) {
            frames[i].modulePath = info.dli_fname;
            if (info.dli_sname) {
                int status = 0;
                char* d = abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &status);
                if (status == 0 && d) { frames[i].funcName = d; free(d); }
                else                    frames[i].funcName = info.dli_sname;
            }
        } else {
            frames[i].modulePath = exePath;
        }
    }

    // Batch addr2line for the main executable frames
    std::vector<void*> mainAddrs;
    std::vector<int>   mainIndices;
    const char* mainExePath = nullptr;
    for (int i = 0; i < nptrs; i++) {
        if (frames[i].modulePath && strstr(frames[i].modulePath, exePath)) {
            mainAddrs.push_back(frames[i].addr);
            mainIndices.push_back(i);
            if (!mainExePath) mainExePath = frames[i].modulePath;
        }
    }
    auto sourceLocations = (!mainAddrs.empty() && mainExePath)
        ? GetSourceLocations(mainAddrs, mainExePath)
        : std::vector<std::string>{};

    // Format each frame
    for (int i = 0; i < nptrs; i++) {
        oss << "  #" << i << " ";
        std::string srcLoc;
        for (size_t j = 0; j < mainIndices.size(); j++) {
            if (mainIndices[j] == i && j < sourceLocations.size()) { srcLoc = sourceLocations[j]; break; }
        }
        if (!srcLoc.empty()) {
            oss << srcLoc;
        } else if (!frames[i].funcName.empty()) {
            if (frames[i].modulePath) {
                const char* fn = strrchr(frames[i].modulePath, '/');
                oss << (fn ? fn + 1 : frames[i].modulePath) << "(";
            }
            oss << frames[i].funcName << ")";
        } else {
            oss << DemangleName(strings[i]);
        }
        oss << " [0x" << std::hex << (uintptr_t)buffer[i] << std::dec << "]\n";
    }

    free(strings);
    oss << "\n";
    return oss.str();
}

// ---------------------------------------------------------------------------
// Signal handler — captures crash and prints stack trace to stderr
// ---------------------------------------------------------------------------
static void CrashSignalHandler(int sig, siginfo_t* info, void* /*context*/)
{
    // Build crash report (async-signal-unsafe, but best effort)
    std::ostringstream oss;
    oss << "\n==========================================================\n"
        << "Caught signal " << sig << " (";
    switch (sig) {
        case SIGSEGV: oss << "SIGSEGV - Segmentation fault"; break;
        case SIGABRT: oss << "SIGABRT - Abort signal";       break;
        case SIGFPE:  oss << "SIGFPE - Floating point exception"; break;
        case SIGILL:  oss << "SIGILL - Illegal instruction";  break;
        case SIGBUS:  oss << "SIGBUS - Bus error";            break;
        default:      oss << "Unknown signal";                break;
    }
    oss << ")\nSignal info: si_addr=" << info->si_addr
        << ", si_code=" << info->si_code
        << "\n==========================================================\n\n";

    pid_t currentTid = syscall(SYS_gettid);
    oss << PrintThreadStack(currentTid);

    const std::string crashLog = oss.str();
    // Write to stderr (signal-safe write)
    write(STDERR_FILENO, crashLog.c_str(), crashLog.size());

    // Re-raise with default handler
    signal(sig, SIG_DFL);
    raise(sig);
}

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------
void setupRescue()
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = CrashSignalHandler;

    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    sigaction(SIGFPE,  &sa, nullptr);
    sigaction(SIGILL,  &sa, nullptr);
    sigaction(SIGBUS,  &sa, nullptr);
}
