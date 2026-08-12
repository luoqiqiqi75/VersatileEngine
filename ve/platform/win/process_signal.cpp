#include "src/process_signal.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <atomic>
#include <mutex>
#include <thread>

namespace {

std::mutex g_mutex;
std::thread g_thread;
std::function<void()> g_callback;
std::atomic<bool> g_stopping{false};
HANDLE g_event = nullptr;

BOOL WINAPI consoleHandler(DWORD type)
{
    switch (type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        if (g_event) SetEvent(g_event);
        return TRUE;
    default:
        return FALSE;
    }
}

} // namespace

namespace ve::platform {

bool startProcessSignalWatcher(std::function<void()> callback)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_thread.joinable() || !callback) return false;
    g_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_event) return false;
    if (!SetConsoleCtrlHandler(consoleHandler, TRUE)) {
        CloseHandle(g_event);
        g_event = nullptr;
        return false;
    }

    g_callback = std::move(callback);
    g_stopping.store(false, std::memory_order_release);
    g_thread = std::thread([] {
        WaitForSingleObject(g_event, INFINITE);
        if (!g_stopping.load(std::memory_order_acquire) && g_callback) g_callback();
    });
    return true;
}

void stopProcessSignalWatcher()
{
    std::thread thread;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_thread.joinable()) return;
        g_stopping.store(true, std::memory_order_release);
        SetEvent(g_event);
        thread = std::move(g_thread);
    }
    thread.join();

    std::lock_guard<std::mutex> lock(g_mutex);
    SetConsoleCtrlHandler(consoleHandler, FALSE);
    CloseHandle(g_event);
    g_event = nullptr;
    g_callback = {};
}

} // namespace ve::platform
