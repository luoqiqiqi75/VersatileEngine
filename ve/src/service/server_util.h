// server_util.h — internal helpers for asio2-based ve::service servers.
//
// Private (under src/), not exported.
//
// AsioServerPool wraps "the io_context an asio2 server runs on". Two modes:
//   - Shared: constructed from an external asio::io_context& (owned elsewhere,
//     typically ServerModule). No worker threads owned; drain() is a no-op.
//   - Own:    default constructor. Spins up its own io_context + worker
//             threads; drain() releases the work_guard and joins them.
//
// Each Server's Private has one AsioServerPool member (constructed before the
// asio2::xxx_server value member) and, in its dtor, calls server.stop()
// followed by pool.drain() — so that any _do_stop handler posted by stop() is
// guaranteed to have completed before the server value is destroyed.
//
// Under ServerModule (the normal ve.entry path) every server uses the module's
// io_context; the module drains its own pool in deinit() before letting the
// server unique_ptrs reset, so the per-server drain() is a no-op there. When
// a caller instantiates a Server standalone (no ve.server module registered),
// the pool owns its threads and the per-server drain fires — same shutdown
// safety, no shared static state.
//
// Factory: server_module.h provides makeServerPool() which returns a shared
// AsioServerPool when ServerModule is registered, else an owned one. Servers
// call that (not AsioServerPool ctor directly) so the "shared vs own" choice
// stays in one place.
#pragma once

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <asio2/asio2.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <optional>
#include <thread>
#include <vector>

namespace ve {
namespace service {

class AsioServerPool
{
public:
    // Own-mode: allocate an io_context + worker threads.
    explicit AsioServerPool(unsigned threads = 4)
        : _external(nullptr)
        , _owned_io(std::make_unique<asio::io_context>(1))
    {
        _owned_guard.emplace(asio::make_work_guard(*_owned_io));
        unsigned n = threads ? threads : 1;
        _owned_workers.reserve(n);
        for (unsigned i = 0; i < n; ++i)
            _owned_workers.emplace_back([&io = *_owned_io] { io.run(); });
    }

    // Shared-mode: refer to an io_context owned by the caller.
    explicit AsioServerPool(asio::io_context& external) : _external(&external) {}

    ~AsioServerPool() { drain(); }

    // Movable so factory helpers can return by value.
    AsioServerPool(AsioServerPool&&) noexcept = default;
    AsioServerPool& operator=(AsioServerPool&&) noexcept = default;

    AsioServerPool(const AsioServerPool&) = delete;
    AsioServerPool& operator=(const AsioServerPool&) = delete;

    // Drain the owned io pool if any. Safe to call multiple times; no-op in
    // shared mode. Called by Server::Private dtor after server.stop() so any
    // handler posted by stop() completes against a still-live server.
    void drain()
    {
        if (!_owned_io) return;
        _owned_guard.reset();
        for (auto& w : _owned_workers) if (w.joinable()) w.join();
        _owned_workers.clear();
    }

    asio::io_context& io() noexcept
    {
        return _external ? *_external : *_owned_io;
    }

private:
    asio::io_context* _external;
    std::unique_ptr<asio::io_context> _owned_io;
    std::optional<asio::executor_work_guard<asio::io_context::executor_type>> _owned_guard;
    std::vector<std::thread> _owned_workers;
};

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

