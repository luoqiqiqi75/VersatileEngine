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
#include <unordered_map>

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
// Shell escaping for popen("addr2line ...")
// ---------------------------------------------------------------------------
static std::string ShellEscapeSingleQuotes(const char* s)
{
    if (!s) return {};
    std::string out;
    for (const char* p = s; *p; ++p) {
        if (*p == '\'') out += "'\\''";
        else            out += *p;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Batch addr2line for source locations (PIE / shared-library aware)
// ---------------------------------------------------------------------------
static std::vector<std::string> GetSourceLocations(const std::vector<void*>& addresses,
                                                   const char* modulePath,
                                                   uintptr_t moduleBase)
{
    std::vector<std::string> results(addresses.size());
    if (!modulePath || addresses.empty()) return results;

    std::ostringstream cmd;
    cmd << "addr2line -e '" << ShellEscapeSingleQuotes(modulePath) << "' -f -C -i -s -p";
    for (void* addr : addresses) {
        uintptr_t a = (uintptr_t)addr;
        if (moduleBase > 0 && a > moduleBase) cmd << " 0x" << std::hex << (a - moduleBase);
        else                                  cmd << " 0x" << std::hex << a;
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
    struct FrameInfo {
        void* addr = nullptr;
        const char* modulePath = nullptr;
        uintptr_t moduleBase = 0;
        std::string funcName;
    };
    std::vector<FrameInfo> frames(nptrs);

    for (int i = 0; i < nptrs; i++) {
        frames[i].addr = buffer[i];
        Dl_info info;
        if (dladdr(buffer[i], &info)) {
            frames[i].modulePath = info.dli_fname;
            frames[i].moduleBase = reinterpret_cast<uintptr_t>(info.dli_fbase);
            if (info.dli_sname) {
                int status = 0;
                char* d = abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &status);
                if (status == 0 && d) { frames[i].funcName = d; free(d); }
                else                    frames[i].funcName = info.dli_sname;
            }
        } else {
            frames[i].modulePath = exePath;
            frames[i].moduleBase = 0;
        }
    }

    // Batch addr2line by module (main executable + all DSOs).
    struct ModuleBatch {
        const char* modulePath = nullptr;
        uintptr_t moduleBase = 0;
        std::vector<void*> addrs;
        std::vector<int> indices;
    };
    std::unordered_map<std::string, ModuleBatch> moduleBatches;
    moduleBatches.reserve(16);
    for (int i = 0; i < nptrs; i++) {
        const char* modulePath = frames[i].modulePath ? frames[i].modulePath : exePath;
        if (!modulePath || !*modulePath) continue;
        auto& batch = moduleBatches[std::string(modulePath)];
        if (!batch.modulePath) {
            batch.modulePath = modulePath;
            batch.moduleBase = frames[i].moduleBase;
        }
        batch.addrs.push_back(frames[i].addr);
        batch.indices.push_back(i);
    }

    std::vector<std::string> sourceLocations(nptrs);
    for (auto& [_, batch] : moduleBatches) {
        auto locs = GetSourceLocations(batch.addrs, batch.modulePath, batch.moduleBase);
        for (size_t i = 0; i < batch.indices.size() && i < locs.size(); ++i)
            sourceLocations[batch.indices[i]] = std::move(locs[i]);
    }

    // Format each frame
    for (int i = 0; i < nptrs; i++) {
        oss << "  #" << i << " ";
        const std::string& srcLoc = sourceLocations[i];
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
        if (frames[i].modulePath) {
            const char* mod = strrchr(frames[i].modulePath, '/');
            oss << " [" << (mod ? mod + 1 : frames[i].modulePath);
            if (frames[i].moduleBase != 0) {
                uintptr_t a = reinterpret_cast<uintptr_t>(buffer[i]);
                if (a > frames[i].moduleBase)
                    oss << " +0x" << std::hex << (a - frames[i].moduleBase) << std::dec;
            }
            oss << "]";
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
    const ssize_t ignored = write(STDERR_FILENO, crashLog.c_str(), crashLog.size());
    (void)ignored;

    // Re-raise with default handler
    signal(sig, SIG_DFL);
    raise(sig);
}

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------
namespace ve {
namespace service {

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

} // namespace service
} // namespace ve
