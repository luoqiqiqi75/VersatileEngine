// server_util.h — internal helpers for asio2-based ve::service servers.
//
// Private (under src/), not exported.
//
// Service I/O is intentionally separate from ve::Loop. Loop is the abstract
// scheduler for VE tasks and may be backed by Qt, Asio, or an application
// event loop. ServerRuntime instead owns asio2-specific transport resources:
// one shared iopool plus every server wrapper constructed on it.
//
// Keeping wrappers alive in the runtime is essential. asio2 registers server
// addresses in an external iopool and unregisters them asynchronously. The
// runtime therefore releases wrappers only after it has requested every stop
// and stopped/joined the pool.
#pragma once

#include "ve/core/loop.h"

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <asio2/asio2.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ve {
namespace service {

class ServerRuntime
{
    struct Resource
    {
        std::shared_ptr<void> keepAlive;
        std::function<void()> requestStop;
    };

public:
    explicit ServerRuntime(std::size_t threads = 4) : _pool(threads)
    {
        _pool.start();
    }

    ~ServerRuntime()
    {
        shutdown();
    }

    ServerRuntime(const ServerRuntime&) = delete;
    ServerRuntime& operator=(const ServerRuntime&) = delete;

    asio2::iopool& pool() noexcept { return _pool; }

    template <typename Server, typename... Args>
    std::shared_ptr<Server> make(Args&&... args)
    {
        auto server = std::make_shared<Server>(std::forward<Args>(args)...);
        std::weak_ptr<Server> weak = server;
        std::lock_guard<std::mutex> lock(_mutex);
        if (_shutdown) {
            throw std::logic_error("ServerRuntime is shut down");
        }
        _resources.push_back(Resource{
            server,
            [weak]() {
                if (auto server = weak.lock()) server->stop(false);
            }
        });
        return server;
    }

    void shutdown()
    {
        std::vector<Resource> resources;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_shutdown) return;
            _shutdown = true;
            resources = _resources;
        }

        // Keep the snapshot alive throughout pool shutdown. This includes
        // failed fallback attempts that their caller no longer references.
        for (auto& resource : resources) {
            if (resource.requestStop) resource.requestStop();
        }
        _pool.stop();

        std::lock_guard<std::mutex> lock(_mutex);
        _resources.clear();
    }

private:
    asio2::iopool _pool;
    std::mutex _mutex;
    std::vector<Resource> _resources;
    bool _shutdown = false;
};

// Joins per-connection loops away from asio2's shared transport workers.
// The state type is expected to own its loop through a `loop` member.
class ConnectionLoopCleanup
{
public:
    explicit ConnectionLoopCleanup(std::string name) : _loop(std::move(name))
    {
        _loop.start();
    }

    ~ConnectionLoopCleanup()
    {
        drain();
        _loop.stop();
    }

    template<typename State>
    void retire(std::shared_ptr<State> state)
    {
        state->loop->quit();
        _loop.post([state = std::move(state)] { state->loop->stop(); });
    }

    void drain()
    {
        auto done = std::make_shared<std::promise<void>>();
        auto future = done->get_future();
        _loop.post([done] { done->set_value(); });
        future.wait();
    }

private:
    AsioLoop _loop;
};

// Active runtime for built-in server wrappers. It is module-scoped and is
// published before any wrapper is constructed.
ServerRuntime& serverRuntime();

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
