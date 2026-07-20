#include "ve/service/rescue.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dbghelp.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>

namespace {

constexpr unsigned kMaxFrames = 128;

std::atomic<bool> g_installed{false};
volatile LONG g_handlingCrash = 0;
LPTOP_LEVEL_EXCEPTION_FILTER g_previousFilter = nullptr;
HANDLE g_output = INVALID_HANDLE_VALUE;

template<std::size_t N>
void advanceOffset(std::size_t& offset, int used) noexcept
{
    if (used <= 0 || offset >= N - 1) return;
    const std::size_t count = static_cast<std::size_t>(used);
    const std::size_t available = N - offset - 1;
    offset += count < available ? count : available;
}

void writeReport(const char* text) noexcept
{
    if (!text) return;

    const DWORD size = static_cast<DWORD>(std::strlen(text));
    if (g_output != nullptr && g_output != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(g_output, text, size, &written, nullptr);
    }
    OutputDebugStringA(text);
}

HANDLE openReportOutput() noexcept
{
    HANDLE output = GetStdHandle(STD_ERROR_HANDLE);
    if (output != nullptr && output != INVALID_HANDLE_VALUE) {
        SetLastError(ERROR_SUCCESS);
        if (GetFileType(output) != FILE_TYPE_UNKNOWN || GetLastError() == ERROR_SUCCESS) return output;
    }

    wchar_t path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        wchar_t* slash = std::wcsrchr(path, L'\\');
        if (slash) {
            slash[1] = L'\0';
            constexpr wchar_t fileName[] = L"ve-crash.log";
            const std::size_t directoryLength = static_cast<std::size_t>(slash + 1 - path);
            if (directoryLength + (sizeof(fileName) / sizeof(fileName[0])) <= MAX_PATH) {
                std::memcpy(path + directoryLength, fileName, sizeof(fileName));
                output = CreateFileW(path, FILE_APPEND_DATA,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     nullptr, OPEN_ALWAYS,
                                     FILE_ATTRIBUTE_NORMAL, nullptr);
            }
        }
    }
    return output;
}

const char* exceptionName(DWORD code) noexcept
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:         return "access violation";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    return "array bounds exceeded";
    case EXCEPTION_BREAKPOINT:               return "breakpoint";
    case EXCEPTION_DATATYPE_MISALIGNMENT:    return "datatype misalignment";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:       return "floating-point divide by zero";
    case EXCEPTION_FLT_INVALID_OPERATION:    return "invalid floating-point operation";
    case EXCEPTION_ILLEGAL_INSTRUCTION:      return "illegal instruction";
    case EXCEPTION_IN_PAGE_ERROR:            return "in-page error";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:       return "integer divide by zero";
    case EXCEPTION_INVALID_HANDLE:           return "invalid handle";
    case EXCEPTION_STACK_OVERFLOW:           return "stack overflow";
    default:                                 return "unknown exception";
    }
}

void writeFrame(HANDLE process, DWORD64 address, unsigned index) noexcept
{
    if (address == 0) return;

    char line[2048] = {};
    alignas(SYMBOL_INFO) char symbolStorage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME] = {};
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolStorage);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = MAX_SYM_NAME;

    DWORD64 symbolDisplacement = 0;
    const BOOL hasSymbol = SymFromAddr(process, address, &symbolDisplacement, symbol);

    IMAGEHLP_LINE64 source = {};
    source.SizeOfStruct = sizeof(source);
    DWORD lineDisplacement = 0;
    const BOOL hasSource = SymGetLineFromAddr64(process, address, &lineDisplacement, &source);

    IMAGEHLP_MODULE64 module = {};
    module.SizeOfStruct = sizeof(module);
    const BOOL hasModule = SymGetModuleInfo64(process, address, &module);

    int used = std::snprintf(line, sizeof(line), "  #%u ", index);
    if (used < 0) return;
    std::size_t offset = static_cast<std::size_t>(used);

    if (hasModule && module.ModuleName[0] != '\0') {
        used = std::snprintf(line + offset, sizeof(line) - offset, "%s!", module.ModuleName);
        advanceOffset<sizeof(line)>(offset, used);
    }
    if (hasSymbol) {
        used = std::snprintf(line + offset, sizeof(line) - offset, "%s+0x%llx",
                             symbol->Name,
                             static_cast<unsigned long long>(symbolDisplacement));
        advanceOffset<sizeof(line)>(offset, used);
    } else if (hasModule && module.BaseOfImage != 0) {
        used = std::snprintf(line + offset, sizeof(line) - offset, "+0x%llx",
                             static_cast<unsigned long long>(address - module.BaseOfImage));
        advanceOffset<sizeof(line)>(offset, used);
    } else {
        used = std::snprintf(line + offset, sizeof(line) - offset, "<unknown>");
        advanceOffset<sizeof(line)>(offset, used);
    }

    if (hasSource && source.FileName) {
        used = std::snprintf(line + offset, sizeof(line) - offset, " (%s:%lu)",
                             source.FileName,
                             static_cast<unsigned long>(source.LineNumber));
        advanceOffset<sizeof(line)>(offset, used);
    }
    std::snprintf(line + offset, sizeof(line) - offset, " [0x%llx]\n",
                  static_cast<unsigned long long>(address));
    writeReport(line);
}

void writeStack(PEXCEPTION_POINTERS exception) noexcept
{
    if (!exception || !exception->ContextRecord) {
        writeReport("  <exception context unavailable>\n");
        return;
    }

    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();
    CONTEXT context = *exception->ContextRecord;
    STACKFRAME64 frame = {};
    DWORD machine = 0;

#if defined(_M_X64)
    machine = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset = context.Rip;
    frame.AddrFrame.Offset = context.Rsp;
    frame.AddrStack.Offset = context.Rsp;
#elif defined(_M_IX86)
    machine = IMAGE_FILE_MACHINE_I386;
    frame.AddrPC.Offset = context.Eip;
    frame.AddrFrame.Offset = context.Ebp;
    frame.AddrStack.Offset = context.Esp;
#elif defined(_M_ARM64)
    machine = IMAGE_FILE_MACHINE_ARM64;
    frame.AddrPC.Offset = context.Pc;
    frame.AddrFrame.Offset = context.Fp;
    frame.AddrStack.Offset = context.Sp;
#else
    writeReport("  <stack walking is unsupported on this Windows architecture>\n");
    return;
#endif

    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Mode = AddrModeFlat;

    // The exception context is the authoritative crash frame. StackWalk64 may
    // advance before returning its first frame, so emit #0 explicitly.
    DWORD64 previousAddress = frame.AddrPC.Offset;
    writeFrame(process, previousAddress, 0);

    unsigned outputIndex = 1;
    unsigned repeatedFrames = 0;
    for (unsigned walkCount = 0; walkCount < kMaxFrames * 2 && outputIndex < kMaxFrames; ++walkCount) {
        if (!StackWalk64(machine, process, thread, &frame, &context, nullptr,
                         SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {
            break;
        }
        if (frame.AddrPC.Offset == 0) break;
        if (frame.AddrPC.Offset == previousAddress) {
            // DbgHelp commonly reports the supplied context once before it
            // advances. Allow that duplicate, but still guard against a stuck
            // unwinder on corrupted stacks.
            if (++repeatedFrames > 1) break;
            continue;
        }
        repeatedFrames = 0;
        previousAddress = frame.AddrPC.Offset;
        writeFrame(process, previousAddress, outputIndex++);
    }
}

LONG WINAPI crashFilter(PEXCEPTION_POINTERS exception) noexcept
{
    if (InterlockedCompareExchange(&g_handlingCrash, 1, 0) != 0) {
        writeReport("[VE CRASH] recursive failure while producing crash report\n");
        return EXCEPTION_CONTINUE_SEARCH;
    }

    const EXCEPTION_RECORD* record = exception ? exception->ExceptionRecord : nullptr;
    const DWORD code = record ? record->ExceptionCode : 0;
    const void* address = record ? record->ExceptionAddress : nullptr;

    char header[1024] = {};
    std::snprintf(header, sizeof(header),
                  "\n============================================================\n"
                  "[VE CRASH] Windows exception 0x%08lx (%s)\n"
                  "thread: %lu, fault address: %p\n",
                  static_cast<unsigned long>(code), exceptionName(code),
                  static_cast<unsigned long>(GetCurrentThreadId()), address);
    writeReport(header);

    if (record && (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR) &&
        record->NumberParameters >= 2) {
        const char* operation = "read";
        if (record->ExceptionInformation[0] == 1) operation = "write";
        else if (record->ExceptionInformation[0] == 8) operation = "execute";
        std::snprintf(header, sizeof(header), "operation: %s address 0x%llx\n",
                      operation,
                      static_cast<unsigned long long>(record->ExceptionInformation[1]));
        writeReport(header);
    }

    writeReport("stack:\n");
    writeStack(exception);
    writeReport("============================================================\n");

    if (g_output != nullptr && g_output != INVALID_HANDLE_VALUE &&
        GetFileType(g_output) == FILE_TYPE_DISK) {
        FlushFileBuffers(g_output);
    }

    if (g_previousFilter && g_previousFilter != crashFilter) {
        return g_previousFilter(exception);
    }
    return EXCEPTION_CONTINUE_SEARCH;
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

    g_output = openReportOutput();

    HANDLE process = GetCurrentProcess();
    SymSetOptions(SymGetOptions() | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS |
                  SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
    // Even if another component already initialized DbgHelp (and this call
    // reports failure), the exception filter can still produce raw addresses.
    SymInitialize(process, nullptr, TRUE);

    g_previousFilter = SetUnhandledExceptionFilter(crashFilter);
    return true;
}

} // namespace service
} // namespace ve
