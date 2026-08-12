#include "src/process_signal.h"

#include <atomic>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <mutex>
#include <thread>
#include <unistd.h>

namespace {

std::mutex g_mutex;
std::thread g_thread;
std::function<void()> g_callback;
std::atomic<bool> g_stopping{false};
int g_pipe[2] = {-1, -1};
struct sigaction g_previousInt {};
struct sigaction g_previousTerm {};

void terminationHandler(int signal) noexcept
{
    const int fd = g_pipe[1];
    if (fd < 0) return;
    const unsigned char value = static_cast<unsigned char>(signal);
    const int savedErrno = errno;
    (void)::write(fd, &value, sizeof(value));
    errno = savedErrno;
}

} // namespace

namespace ve::platform {

bool startProcessSignalWatcher(std::function<void()> callback)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_thread.joinable() || !callback) return false;
    if (::pipe(g_pipe) != 0) return false;
    const int flags = fcntl(g_pipe[1], F_GETFL, 0);
    if (flags < 0 || fcntl(g_pipe[1], F_SETFL, flags | O_NONBLOCK) != 0) {
        ::close(g_pipe[0]);
        ::close(g_pipe[1]);
        g_pipe[0] = g_pipe[1] = -1;
        return false;
    }

    struct sigaction action {};
    action.sa_handler = terminationHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    if (sigaction(SIGINT, &action, &g_previousInt) != 0) {
        ::close(g_pipe[0]);
        ::close(g_pipe[1]);
        g_pipe[0] = g_pipe[1] = -1;
        return false;
    }
    if (sigaction(SIGTERM, &action, &g_previousTerm) != 0) {
        sigaction(SIGINT, &g_previousInt, nullptr);
        ::close(g_pipe[0]);
        ::close(g_pipe[1]);
        g_pipe[0] = g_pipe[1] = -1;
        return false;
    }

    g_callback = std::move(callback);
    g_stopping.store(false, std::memory_order_release);
    g_thread = std::thread([] {
        unsigned char signal = 0;
        while (::read(g_pipe[0], &signal, sizeof(signal)) < 0 && errno == EINTR) {}
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
        const unsigned char wake = 0;
        (void)::write(g_pipe[1], &wake, sizeof(wake));
        thread = std::move(g_thread);
    }
    thread.join();

    std::lock_guard<std::mutex> lock(g_mutex);
    sigaction(SIGINT, &g_previousInt, nullptr);
    sigaction(SIGTERM, &g_previousTerm, nullptr);
    ::close(g_pipe[0]);
    ::close(g_pipe[1]);
    g_pipe[0] = g_pipe[1] = -1;
    g_callback = {};
}

} // namespace ve::platform
