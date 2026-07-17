// server_util.h — internal helpers for asio2-based ve::service servers.
//
// Private (under src/), not exported.
//
// Threading model:
//   ServerModule owns one asio2::iopool for the whole ve process. Every ve
//   Server passes sharedIopool() to its asio2::xxx_server constructor, so all
//   servers share the same worker threads. The pool starts when ServerModule
//   is constructed and stops in its destructor — by which time deinit() has
//   already stopped every ve Server, so no handler is left running.
//
// Shutdown discipline:
//   A failed start schedules asio2's shutdown before start() returns. Every
//   wrapper therefore arms ServerStopBarrier before calling start(). On stop,
//   we wait for that pre-bound notification and then flush the shared iopool,
//   ensuring the rest of _handle_stop has returned before object destruction.
#pragma once

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <asio2/asio2.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <condition_variable>
#include <cstdint>
#include <future>
#include <mutex>
#include <vector>

namespace ve {
namespace service {

// The one iopool every asio2 server in ve shares. Owned by ServerModule;
// see server_module.h for lifecycle. Servers pass this to their asio2
// constructor:  asio2::tcp_server server{sharedIopool()};
asio2::iopool& sharedIopool();

// A generation-counted stop notification. arm() must run before start(), when
// no server callback can be firing. This avoids both the missed-notification
// race and listener mutation concurrent with asio2::_fire_stop().
class ServerStopBarrier
{
    struct State
    {
        std::mutex mutex;
        std::condition_variable cv;
        std::uint64_t expected = 0;
        std::uint64_t completed = 0;
    };

public:
    template <typename Server>
    void arm(Server& server)
    {
        auto state = _state;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            ++state->expected;
        }
        server.bind_stop([state]() {
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                ++state->completed;
            }
            state->cv.notify_all();
        });
    }

    void wait()
    {
        auto state = _state;
        std::unique_lock<std::mutex> lock(state->mutex);
        const std::uint64_t target = state->expected;
        if (target == 0) return; // constructed but never started
        state->cv.wait(lock, [&] { return state->completed >= target; });
    }

private:
    std::shared_ptr<State> _state = std::make_shared<State>();
};

// Wait until every handler already queued on every shared-pool worker has
// returned. Called only from the process/module thread, never an iopool worker.
inline void flushSharedIopool()
{
    auto& pool = sharedIopool();
    if (pool.stopped()) return;

    std::vector<std::future<void>> futures;
    futures.reserve(pool.size());
    for (std::size_t i = 0; i < pool.size(); ++i) {
        auto done = std::make_shared<std::promise<void>>();
        futures.emplace_back(done->get_future());
        auto io = pool.get(i);
        asio::post(io->context(), [done]() { done->set_value(); });
    }
    for (auto& future : futures) future.wait();
}

template <typename Server>
inline void stopAndWait(Server& server, ServerStopBarrier& barrier)
{
    server.stop();
    if (sharedIopool().stopped()) return;
    barrier.wait();
    // _fire_stop notifies from inside _handle_stop. A queue barrier posted
    // after that notification runs after the remainder of _handle_stop.
    flushSharedIopool();
}

namespace detail {
    template <typename T, typename = void>
    struct has_acceptor : std::false_type {};

    template <typename T>
    struct has_acceptor<T, std::void_t<decltype(std::declval<T>().acceptor())>> : std::true_type {};
}

template <typename AsioServer>
void disableWindowsPortReuse(AsioServer& server) {
#ifdef _WIN32
    // Windows: disable port reuse so bind() fails if the port is taken by
    // another process (default Windows behavior would silently accept).
    server.bind_init([&server]() {
        asio::error_code ec;
        if constexpr (detail::has_acceptor<AsioServer>::value) {
            server.acceptor().set_option(asio::socket_base::reuse_address(false), ec);
        } else {
            server.socket().set_option(asio::socket_base::reuse_address(false), ec);
        }
    });
#endif
}

} // namespace service
} // namespace ve
