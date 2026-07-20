#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "ve/service/rescue.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>

#if defined(VE_RESCUE_HAS_LIBUNWIND)
#define UNW_LOCAL_ONLY
#include <libunwind.h>
#endif

#if defined(__linux__) || defined(__ANDROID__)
#include <sys/syscall.h>
#endif

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

constexpr std::size_t kMaxFrames = 128;
constexpr int kSignals[] = {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS};

struct Trace {
    std::uintptr_t frames[kMaxFrames] = {};
    std::size_t size = 0;
};

struct ContextRegisters {
    std::uintptr_t instruction = 0;
    std::uintptr_t stack = 0;
    std::uintptr_t frame = 0;
};

std::atomic<bool> g_installed{false};
volatile sig_atomic_t g_handlingCrash = 0;
int g_output = STDERR_FILENO;
struct sigaction g_previousActions[sizeof(kSignals) / sizeof(kSignals[0])] = {};

template<std::size_t N>
void advanceOffset(std::size_t& offset, int used) noexcept
{
    if (used <= 0 || offset >= N - 1) return;
    const std::size_t count = static_cast<std::size_t>(used);
    const std::size_t available = N - offset - 1;
    offset += count < available ? count : available;
}

void writeAll(const char* data, std::size_t size) noexcept
{
    while (size > 0) {
        const ssize_t written = write(g_output, data, size);
        if (written > 0) {
            data += written;
            size -= static_cast<std::size_t>(written);
        } else if (written < 0 && errno == EINTR) {
            continue;
        } else {
            break;
        }
    }
}

void writeReport(const char* text) noexcept
{
    if (text) writeAll(text, std::strlen(text));
}

const char* signalName(int signal) noexcept
{
    switch (signal) {
    case SIGSEGV: return "SIGSEGV (segmentation fault)";
    case SIGABRT: return "SIGABRT (abort)";
    case SIGFPE:  return "SIGFPE (arithmetic exception)";
    case SIGILL:  return "SIGILL (illegal instruction)";
    case SIGBUS:  return "SIGBUS (bus error)";
    default:      return "unknown signal";
    }
}

long currentThreadId() noexcept
{
#if defined(SYS_gettid)
    return static_cast<long>(syscall(SYS_gettid));
#else
    return static_cast<long>(getpid());
#endif
}

ContextRegisters contextRegisters(void* rawContext) noexcept
{
    ContextRegisters registers;
    if (!rawContext) return registers;
    auto* context = static_cast<ucontext_t*>(rawContext);

#if defined(__APPLE__) && defined(__x86_64__)
    registers.instruction = static_cast<std::uintptr_t>(context->uc_mcontext->__ss.__rip);
    registers.stack = static_cast<std::uintptr_t>(context->uc_mcontext->__ss.__rsp);
    registers.frame = static_cast<std::uintptr_t>(context->uc_mcontext->__ss.__rbp);
#elif defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
    registers.instruction = static_cast<std::uintptr_t>(context->uc_mcontext->__ss.__pc);
    registers.stack = static_cast<std::uintptr_t>(context->uc_mcontext->__ss.__sp);
    registers.frame = static_cast<std::uintptr_t>(context->uc_mcontext->__ss.__fp);
#elif defined(__linux__) && defined(__x86_64__)
    registers.instruction = static_cast<std::uintptr_t>(context->uc_mcontext.gregs[REG_RIP]);
    registers.stack = static_cast<std::uintptr_t>(context->uc_mcontext.gregs[REG_RSP]);
    registers.frame = static_cast<std::uintptr_t>(context->uc_mcontext.gregs[REG_RBP]);
#elif defined(__linux__) && defined(__i386__)
    registers.instruction = static_cast<std::uintptr_t>(context->uc_mcontext.gregs[REG_EIP]);
    registers.stack = static_cast<std::uintptr_t>(context->uc_mcontext.gregs[REG_ESP]);
    registers.frame = static_cast<std::uintptr_t>(context->uc_mcontext.gregs[REG_EBP]);
#elif (defined(__linux__) || defined(__ANDROID__)) && (defined(__aarch64__) || defined(__arm64__))
    registers.instruction = static_cast<std::uintptr_t>(context->uc_mcontext.pc);
    registers.stack = static_cast<std::uintptr_t>(context->uc_mcontext.sp);
    registers.frame = static_cast<std::uintptr_t>(context->uc_mcontext.regs[29]);
#elif (defined(__linux__) || defined(__ANDROID__)) && defined(__arm__)
    registers.instruction = static_cast<std::uintptr_t>(context->uc_mcontext.arm_pc);
    registers.stack = static_cast<std::uintptr_t>(context->uc_mcontext.arm_sp);
    registers.frame = static_cast<std::uintptr_t>(context->uc_mcontext.arm_fp);
#else
    (void)context;
#endif
    return registers;
}

void appendFrame(Trace& trace, std::uintptr_t address) noexcept
{
    if (address == 0 || trace.size >= kMaxFrames) return;
    if (trace.size == 0 || trace.frames[trace.size - 1] != address) {
        trace.frames[trace.size++] = address;
    }
}

std::uintptr_t normalizeReturnAddress(std::uintptr_t address) noexcept
{
#if defined(__GNUC__) || defined(__clang__)
    return reinterpret_cast<std::uintptr_t>(
        __builtin_extract_return_addr(reinterpret_cast<void*>(address)));
#else
    return address;
#endif
}

Trace collectFramePointerCallers(const ContextRegisters& registers) noexcept
{
    Trace trace;
#if !((defined(__x86_64__) || defined(__i386__)) || defined(__aarch64__) || defined(__arm64__))
    (void)registers;
    return trace;
#else
    if (registers.stack == 0 || registers.frame < registers.stack) return trace;

    constexpr std::uintptr_t maxStackSpan = 64u * 1024u * 1024u;
    const std::uintptr_t stackLimit = registers.stack > UINTPTR_MAX - maxStackSpan
        ? UINTPTR_MAX : registers.stack + maxStackSpan;
    std::uintptr_t frameAddress = registers.frame;

    while (trace.size < kMaxFrames - 1) {
        if ((frameAddress % alignof(std::uintptr_t)) != 0 ||
            frameAddress < registers.stack ||
            frameAddress > stackLimit - 2 * sizeof(std::uintptr_t)) {
            break;
        }

        // x86/x64 and AArch64 frame records store the previous frame pointer
        // followed by the caller's return address.
        const auto* record = reinterpret_cast<const std::uintptr_t*>(frameAddress);
        const std::uintptr_t nextFrame = record[0];
        const std::uintptr_t returnAddress = normalizeReturnAddress(record[1]);
        if (returnAddress < 4096) break;
        appendFrame(trace, returnAddress);

        if (nextFrame <= frameAddress || nextFrame > stackLimit) break;
        frameAddress = nextFrame;
    }
    return trace;
#endif
}

Trace collectContextCallers(void* rawContext, const ContextRegisters& registers) noexcept
{
#if defined(VE_RESCUE_HAS_LIBUNWIND)
    if (rawContext) {
        Trace trace;
        unw_cursor_t cursor;
        auto* context = reinterpret_cast<unw_context_t*>(rawContext);
        if (unw_init_local2(&cursor, context, UNW_INIT_SIGNAL_FRAME) >= 0) {
            // The initialized cursor is the fault frame already emitted from
            // ucontext. Step first so this list contains callers only.
            while (trace.size < kMaxFrames - 1 && unw_step(&cursor) > 0) {
                unw_word_t address = 0;
                if (unw_get_reg(&cursor, UNW_REG_IP, &address) < 0) break;
                appendFrame(trace, static_cast<std::uintptr_t>(address));
            }
            if (trace.size > 0) return trace;
        }
    }
#else
    (void)rawContext;
#endif
    return collectFramePointerCallers(registers);
}

const char* baseName(const char* path) noexcept
{
    if (!path) return nullptr;
    const char* slash = std::strrchr(path, '/');
    return slash ? slash + 1 : path;
}

void writeFrame(std::uintptr_t address, unsigned index, bool exactAddress) noexcept
{
    if (address == 0) return;

    // Unwinders normally report a return address. Moving it into the calling
    // instruction gives dladdr the correct symbol at function boundaries.
    const std::uintptr_t lookupAddress = (!exactAddress && address > 0) ? address - 1 : address;
    Dl_info info = {};
    const bool resolved = dladdr(reinterpret_cast<void*>(lookupAddress), &info) != 0;

    char line[2048] = {};
    std::size_t offset = 0;
    int used = std::snprintf(line, sizeof(line), "  #%u ", index);
    if (used < 0) return;
    offset = static_cast<std::size_t>(used);

    if (resolved && info.dli_fname) {
        const char* module = baseName(info.dli_fname);
        used = std::snprintf(line + offset, sizeof(line) - offset, "%s", module ? module : info.dli_fname);
        advanceOffset<sizeof(line)>(offset, used);

        if (info.dli_sname) {
            const std::uintptr_t symbolAddress = reinterpret_cast<std::uintptr_t>(info.dli_saddr);
            used = std::snprintf(line + offset, sizeof(line) - offset, "!%s+0x%llx",
                                 info.dli_sname,
                                 static_cast<unsigned long long>(lookupAddress - symbolAddress));
            advanceOffset<sizeof(line)>(offset, used);
        } else if (info.dli_fbase) {
            const std::uintptr_t moduleAddress = reinterpret_cast<std::uintptr_t>(info.dli_fbase);
            used = std::snprintf(line + offset, sizeof(line) - offset, "+0x%llx",
                                 static_cast<unsigned long long>(lookupAddress - moduleAddress));
            advanceOffset<sizeof(line)>(offset, used);
        }
    } else {
        used = std::snprintf(line + offset, sizeof(line) - offset, "<unknown>");
        advanceOffset<sizeof(line)>(offset, used);
    }

    std::snprintf(line + offset, sizeof(line) - offset, " [0x%llx]\n",
                  static_cast<unsigned long long>(address));
    writeReport(line);
}

const struct sigaction* previousActionFor(int signal) noexcept
{
    for (std::size_t i = 0; i < sizeof(kSignals) / sizeof(kSignals[0]); ++i) {
        if (kSignals[i] == signal) return &g_previousActions[i];
    }
    return nullptr;
}

void restoreAndRaise(int signal) noexcept
{
    const struct sigaction* previous = previousActionFor(signal);
    if (previous) {
        sigaction(signal, previous, nullptr);
    } else {
        struct sigaction action = {};
        sigemptyset(&action.sa_mask);
        action.sa_handler = SIG_DFL;
        sigaction(signal, &action, nullptr);
    }

    sigset_t unblocked;
    sigemptyset(&unblocked);
    sigaddset(&unblocked, signal);
    sigprocmask(SIG_UNBLOCK, &unblocked, nullptr);
    raise(signal);
    _exit(128 + signal);
}

void crashSignalHandler(int signal, siginfo_t* info, void* rawContext) noexcept
{
    if (g_handlingCrash != 0) {
        writeReport("[VE CRASH] recursive failure while producing crash report\n");
        restoreAndRaise(signal);
    }
    g_handlingCrash = 1;

    const ContextRegisters registers = contextRegisters(rawContext);
    char header[1024] = {};
    std::snprintf(header, sizeof(header),
                  "\n============================================================\n"
                  "[VE CRASH] signal %d (%s)\n"
                  "process: %ld, thread: %ld, fault address: %p\n"
                  "stack:\n",
                  signal, signalName(signal), static_cast<long>(getpid()), currentThreadId(),
                  info ? info->si_addr : nullptr);
    writeReport(header);

    unsigned outputIndex = 0;
    if (registers.instruction != 0) {
        // Emit the authoritative crash frame before attempting any memory
        // reads while walking a possibly corrupted stack.
        writeFrame(registers.instruction, outputIndex++, true);
    }

    const Trace callers = collectContextCallers(rawContext, registers);
    for (std::size_t i = 0; i < callers.size; ++i) {
        writeFrame(callers.frames[i], outputIndex++, false);
    }

    if (outputIndex == 0) writeReport("  <stack unavailable>\n");
    writeReport("============================================================\n");
    restoreAndRaise(signal);
}

} // namespace

namespace ve {
namespace service {

void setupRescue()
{
    (void)trySetupRescue();
}

bool trySetupRescue() noexcept
{
    bool expected = false;
    if (!g_installed.compare_exchange_strong(expected, true)) return true;

    bool openedFallbackOutput = false;
    if (fcntl(STDERR_FILENO, F_GETFD) == -1) {
        g_output = open("ve-crash.log", O_WRONLY | O_CREAT | O_APPEND, 0600);
        if (g_output == -1) {
            g_output = STDERR_FILENO;
            g_installed.store(false);
            return false;
        }
        openedFallbackOutput = true;
    }

    struct sigaction action = {};
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO | SA_RESTART;
    action.sa_sigaction = crashSignalHandler;

    std::size_t installedCount = 0;
    for (; installedCount < sizeof(kSignals) / sizeof(kSignals[0]); ++installedCount) {
        if (sigaction(kSignals[installedCount], &action, &g_previousActions[installedCount]) != 0) {
            break;
        }
    }

    if (installedCount != sizeof(kSignals) / sizeof(kSignals[0])) {
        while (installedCount > 0) {
            --installedCount;
            sigaction(kSignals[installedCount], &g_previousActions[installedCount], nullptr);
        }
        if (openedFallbackOutput) {
            close(g_output);
            g_output = STDERR_FILENO;
        }
        g_installed.store(false);
        return false;
    }
    return true;
}

} // namespace service
} // namespace ve
