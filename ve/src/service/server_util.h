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
// The state owns its loop and waits for its async requests through waitAsync().
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
        // Finish request registration on the connection thread before waiting.
        auto done = std::make_shared<std::promise<void>>();
        auto ready = done->get_future().share();
        state->loop->post([done] { done->set_value(); });
        _loop.post([state = std::move(state), ready] {
            ready.wait();
            state->waitAsync();
            state->loop->stop();
        });
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

struct ExecutionConnection
{
    std::unique_ptr<AsioLoop> loop;
    std::atomic<bool> connected{true};

    void waitAsync() {}
};

// Transport callbacks only enqueue work. Each peer owns its execution thread,
// and disconnect joins that thread on the cleanup loop.
template<typename Socket>
std::size_t connectionKey(const std::shared_ptr<Socket>& socket)
{
    return reinterpret_cast<std::size_t>(socket.get());
}

template<typename State>
class ConnectionLoops
{
public:
    explicit ConnectionLoops(std::string name)
        : _name(std::move(name)), _cleanup(_name + ".cleanup") {}

    ~ConnectionLoops() { stop(true); }

    bool add(std::size_t key, std::shared_ptr<State> state)
    {
        state->loop = std::make_unique<AsioLoop>(_name + "." + std::to_string(key));
        if (!state->loop->start()) return false;
        std::lock_guard<std::mutex> lock(_mutex);
        _states.emplace(key, std::move(state));
        return true;
    }

    std::shared_ptr<State> get(std::size_t key)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _states.find(key);
        return it == _states.end() ? nullptr : it->second;
    }

    void remove(std::size_t key)
    {
        std::shared_ptr<State> state;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            auto it = _states.find(key);
            if (it == _states.end()) return;
            state = std::move(it->second);
            _states.erase(it);
        }
        retire(std::move(state));
    }

    void stop(bool wait)
    {
        std::unordered_map<std::size_t, std::shared_ptr<State>> states;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            states.swap(_states);
        }
        for (auto& item : states) retire(std::move(item.second));
        if (wait) _cleanup.drain();
    }

    int count()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return static_cast<int>(_states.size());
    }

private:
    void retire(std::shared_ptr<State> state)
    {
        state->connected.store(false, std::memory_order_release);
        _cleanup.retire(std::move(state));
    }

    std::string _name;
    ConnectionLoopCleanup _cleanup;
    std::mutex _mutex;
    std::unordered_map<std::size_t, std::shared_ptr<State>> _states;
};

template<typename State, typename Socket>
void sendConnection(const std::weak_ptr<State>& weak_state,
                    const std::weak_ptr<Socket>& weak_socket, std::string message)
{
    if (auto socket = weak_socket.lock()) {
        socket->post([weak_state, socket, message = std::move(message)] {
            auto state = weak_state.lock();
            if (state && state->connected.load(std::memory_order_acquire)
                && socket->is_started()) socket->async_send(message);
        });
    }
}

// Copy request/response data across threads; only the transport thread touches
// asio2's response object and releases its deferred-send guard.
template<typename State, typename Socket, typename Handler>
void postHttpRequest(const std::shared_ptr<State>& state, std::shared_ptr<Socket> socket,
                     http::web_request& request, http::web_response& response, Handler handler)
{
    if (!state) {
        response.fill_text("connection closed", http::status::service_unavailable);
        return;
    }
    // asio2's forwarding constructor copies only the HTTP message. Select the
    // copy constructor to preserve the parsed request URL as well.
    auto req = std::make_shared<http::web_request>(std::as_const(request));
    auto rep = std::make_shared<http::web_response>();
    rep->base() = response.base();
    auto guard = response.defer();
    std::weak_ptr<State> weak_state = state;
    state->loop->post([weak_state, socket, req, rep, guard = std::move(guard),
                       response_ptr = &response, handler = std::move(handler)]() mutable {
        auto state = weak_state.lock();
        if (state && state->connected.load(std::memory_order_acquire)) {
            try {
                handler(*req, *rep, state);
            } catch (const std::exception& e) {
                rep->fill_text(e.what(), http::status::internal_server_error);
            } catch (...) {
                rep->fill_text("request failed", http::status::internal_server_error);
            }
        }
        socket->post([weak_state, socket, rep, response_ptr, guard = std::move(guard)] {
            auto state = weak_state.lock();
            if (state && state->connected.load(std::memory_order_acquire)
                && socket->is_started()) response_ptr->base() = std::move(rep->base());
        });
    });
}

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
