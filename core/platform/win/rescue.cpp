//
// Created by lqi on 2025/7/14.
//

#include "rescue.h"

#include <windows.h>
#include <tchar.h>
#include <dbghelp.h>
#include <stdio.h>
#include <TlHelp32.h>

#pragma comment(lib, "dbghelp.lib")

std::vector<DWORD> GetAllThreadIds() {
    std::vector<DWORD> threadIds;
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);

    if (hSnapshot != INVALID_HANDLE_VALUE) {
        THREADENTRY32 threadEntry = { 0 };
        threadEntry.dwSize = sizeof(THREADENTRY32);

        if (Thread32First(hSnapshot, &threadEntry)) {
            do {
                if (threadEntry.th32OwnerProcessID == GetCurrentProcessId()) {
                    threadIds.push_back(threadEntry.th32ThreadID);
                }
            } while (Thread32Next(hSnapshot, &threadEntry));
        }

        CloseHandle(hSnapshot);
    }

    return threadIds;
}

std::string PrintThreadStack(DWORD threadId) {
    HANDLE hThread = OpenThread(THREAD_ALL_ACCESS, FALSE, threadId);
    if (!hThread) {
        return "cannot open thread " + std::to_string(threadId) + ", error code: " + std::to_string(GetLastError());
    }

    CONTEXT context = { 0 };
    context.ContextFlags = CONTEXT_FULL;

    if (!GetThreadContext(hThread, &context)) {
        CloseHandle(hThread);
        return "cannot get thread " + std::to_string(threadId) + " context, error code: " + std::to_string(GetLastError());
    }

    STACKFRAME64 stackFrame = { 0 };
    HANDLE hProcess = GetCurrentProcess();

    stackFrame.AddrPC.Offset = context.Rip;
    stackFrame.AddrPC.Mode = AddrModeFlat;
    stackFrame.AddrStack.Offset = context.Rsp;
    stackFrame.AddrStack.Mode = AddrModeFlat;
    stackFrame.AddrFrame.Offset = context.Rbp;
    stackFrame.AddrFrame.Mode = AddrModeFlat;

    SymInitialize(hProcess, NULL, TRUE);
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);

    std::ostringstream oss;

    oss << "thread " << threadId << " trace stack:" << std::endl;

    // 遍历堆栈
    while (StackWalk64(
        IMAGE_FILE_MACHINE_AMD64,
        hProcess,
        hThread,
        &stackFrame,
        &context,
        NULL,
        SymFunctionTableAccess64,
        SymGetModuleBase64,
        NULL)) {

        char symbolName[256] = { 0 };
        SYMBOL_INFO symbolInfo = { 0 };
        symbolInfo.SizeOfStruct = sizeof(SYMBOL_INFO);
        symbolInfo.MaxNameLen = sizeof(symbolName) - 1;

        DWORD64 displacement = 0;
        if (SymFromAddr(hProcess, stackFrame.AddrPC.Offset, &displacement, &symbolInfo)) {
            // 获取行号信息
            IMAGEHLP_LINE64 lineInfo = { 0 };
            lineInfo.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
            DWORD lineDisplacement = 0;

            std::string lineInfoStr = "...";
            if (SymGetLineFromAddr64(hProcess, stackFrame.AddrPC.Offset, &lineDisplacement, &lineInfo)) {
                lineInfoStr = std::string(lineInfo.FileName) + ":" + std::to_string(lineInfo.LineNumber);
            }

            oss << "  " << symbolInfo.Name << " (addr: 0x"
                      << std::hex << stackFrame.AddrPC.Offset << std::dec
                      << ", " << lineInfoStr << ")" << std::endl;
        } else {
            oss << "  ... (addr: 0x"
                      << std::hex << stackFrame.AddrPC.Offset << std::dec << ")" << std::endl;
        }
    }

    oss << std::endl;

    SymCleanup(hProcess);
    CloseHandle(hThread);

    return oss.str();
}

#include "StackWalker/interface.h"

void setupRescue(ve::Data* d)
{
    set_default_handler();

    QObject::connect(ve::d(d, "export"), &ve::Data::changed, [] {
        ve::data::Manager::writeToBin("root.bin", ve::data::manager().rootMobj()->exportToBin(false));
    });
    QObject::connect(ve::d(d, "dump"), &ve::Data::changed, [rescue_d = d] {
        std::vector<DWORD> threadIds = GetAllThreadIds();
        std::ostringstream oss;
        oss << "Current thread: " << std::this_thread::get_id() << " Thread count: " << std::to_string(threadIds.size()) << "\n";
        for (DWORD threadId : threadIds) {
            oss << PrintThreadStack(threadId);
        }
        qCritical() << oss.str().c_str();
        ve::d(rescue_d, "result")->set(nullptr, QString::fromStdString(oss.str()));
    });
}