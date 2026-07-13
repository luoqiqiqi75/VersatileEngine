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
//   asio2's server.stop() posts _do_stop to the io thread and returns
//   immediately. Destroying the C++ server object before _do_stop finishes
//   would let the io thread dereference freed memory. So we don't destroy
//   the server until _do_stop has run to completion — bind_stop() fires at
//   the end of that chain (_fire_stop), and stopAndWait() blocks on it.
//
//   Callers use ServerModule::closeServer() which does stopAndWait() then
//   resets the unique_ptr. Server dtors on the standalone path (destructor
//   without deinit) still work: ~xxx_server() calls stop() and the pool is
//   alive because it was constructed before the server member.
#pragma once

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <asio2/asio2.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <chrono>
#include <future>

namespace ve {
namespace service {

// The one iopool every asio2 server in ve shares. Owned by ServerModule;
// see server_module.h for lifecycle. Servers pass this to their asio2
// constructor:  asio2::tcp_server server{sharedIopool()};
asio2::iopool& sharedIopool();

// Stop an asio2 server and block until its _do_stop chain has fully run,
// so it's safe to destroy the object right after this returns.
//
// asio2 fires bind_stop at the tail of _fire_stop (last step in the shutdown
// chain), which we hook to a promise here. If the server is already fully
// stopped (never started, or a previous stop() already completed) we skip
// the wait — arming bind_stop would never resolve because _fire_stop won't
// run again. A 3s wall-clock cap catches any pathological path where
// bind_stop somehow doesn't fire; session disconnect_timeout is 2s so 3s
// is enough slack.
template <typename Server>
inline void stopAndWait(Server& server)
{
    if (server.is_stopped()) {
        // Never started, or already fully stopped — no shutdown chain in
        // flight, so nothing to wait for.
        return;
    }

    auto done = std::make_shared<std::promise<void>>();
    auto fut = done->get_future();
    server.bind_stop([done]() {
        try { done->set_value(); } catch (...) {} // idempotent: set_value on satisfied promise throws
    });
    server.stop();
    fut.wait_for(std::chrono::seconds(3));
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
