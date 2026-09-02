// node_tcp_server.cpp — ve::service::NodeTcpServer (multi-session, line-delimited JSON)
#include "ve/service/node_service.h"
#include "ve/core/node.h"
#include "ve/core/schema.h"
#include "ve/core/command.h"
#include "ve/core/log.h"
#include "ve/core/pipeline.h"
#include "node_commands.h"
#include "server_util.h"

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <asio2/tcp/tcp_server.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <atomic>
#include <exception>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace ve {
namespace service {

static std::string toJson(const Node& n)
{
    return schema::fromNode<schema::JsonS>(&n, schema::JsonS::compact());
}

struct NodeTcpServer::Private
{
    Node*    root = nullptr;
    uint16_t port = 12200;

    asio2::tcp_server server{serverRuntime().pool()};
    ConnectionLoopCleanup loopCleanup{"node.tcp.cleanup"};
    std::mutex mtx;
    std::atomic<int> connCount{0};

    struct ConnState {
        std::string recvBuf;
        std::unique_ptr<Session> session;
        std::unique_ptr<AsioLoop> loop;
        std::atomic<bool> connected{true};
    };
    std::unordered_map<std::size_t, std::shared_ptr<ConnState>> connections;

    template<typename SocketSession>
    static void send(const std::weak_ptr<ConnState>& weak_state,
                     std::weak_ptr<SocketSession> weak_socket, std::string reply)
    {
        if (reply.empty()) return;
        if (auto socket = weak_socket.lock()) {
            socket->post([weak_state, socket, reply = std::move(reply)]() mutable {
                auto state = weak_state.lock();
                if (!state || !state->connected.load(std::memory_order_acquire)
                    || !socket->is_started()) return;
                socket->async_send(reply);
            });
        }
    }

    template<typename SocketSession>
    static void processLines(const std::shared_ptr<ConnState>& state,
                             const std::weak_ptr<SocketSession>& weak_socket)
    {
        if (!state || !state->connected.load(std::memory_order_acquire)) return;

        auto& buf = state->recvBuf;
        std::string::size_type pos;
        while ((pos = buf.find('\n')) != std::string::npos) {
            if (!state->connected.load(std::memory_order_acquire)) return;
            std::string line = buf.substr(0, pos);
            buf.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;

            Pipeline pipe;
            if (!schema::toNode<schema::JsonS>(pipe.contextNode(), line)) {
                Node err;
                err.set("code", int64_t(ERR_INVALID));
                err.set("message", std::string("invalid JSON"));
                send(std::weak_ptr<ConnState>(state), weak_socket, toJson(err) + "\n");
                continue;
            }

            pipe.contextNode()->set("_session", Var::ptr(state->session.get()));

            Node* batch_n = pipe.contextNode()->find("batch");
            if (batch_n) {
                Node* out = pipe.contextNode()->at("data");
                bool valid = true;
                for (auto* item : batch_n->children()) {
                    auto ref = resolveCmd(item);
                    if (!ref.factory) {
                        pipe.contextNode()->erase("batch");
                        pipe.contextNode()->set("code", int64_t(ERR_NOT_FOUND));
                        pipe.contextNode()->set("message", "unknown: " + ref.key);
                        send(std::weak_ptr<ConnState>(state), weak_socket,
                             toJson(*pipe.contextNode()) + "\n");
                        valid = false;
                        break;
                    }
                    Command* c = pipe.add(command::create(*ref.factory, ref.key));
                    c->setContextNodes(pipe.contextNode(), item->at("params"), out->append());
                }
                if (!valid) continue;
            } else {
                auto ref = resolveCmd(pipe.contextNode());
                if (!ref.factory) {
                    pipe.contextNode()->set("code", int64_t(ref.key.empty() ? ERR_INVALID : ERR_NOT_FOUND));
                    pipe.contextNode()->set("message", ref.key.empty() ? std::string("op or cmd required") : "unknown: " + ref.key);
                    send(std::weak_ptr<ConnState>(state), weak_socket,
                         toJson(*pipe.contextNode()) + "\n");
                    continue;
                }
                Command* c = pipe.add(command::create(*ref.factory, ref.key));
                c->setContextNodes(pipe.contextNode(), pipe.contextNode()->at("params"), pipe.contextNode()->at("data"));
            }

            auto completed = std::make_shared<std::promise<std::string>>();
            auto reply = completed->get_future();
            pipe.onFinished(nullptr, [completed](Pipeline& finished) {
                try {
                    completed->set_value(finalizeReply(finished)
                        ? toJson(*finished.contextNode()) + "\n"
                        : std::string{});
                } catch (...) {
                    completed->set_exception(std::current_exception());
                }
            });
            pipe.async();
            try {
                send(std::weak_ptr<ConnState>(state), weak_socket, reply.get());
            } catch (const std::exception& e) {
                Node err;
                err.set("code", int64_t(ERR_INVALID));
                err.set("message", std::string("command response failed: ") + e.what());
                send(std::weak_ptr<ConnState>(state), weak_socket, toJson(err) + "\n");
            } catch (...) {
                Node err;
                err.set("code", int64_t(ERR_INVALID));
                err.set("message", std::string("command response failed"));
                send(std::weak_ptr<ConnState>(state), weak_socket, toJson(err) + "\n");
            }
        }
    }

    void retire(std::shared_ptr<ConnState> state)
    {
        if (!state || !state->connected.exchange(false, std::memory_order_acq_rel)) return;
        loopCleanup.retire(std::move(state));
    }

    void stopConnections(bool wait)
    {
        std::vector<std::shared_ptr<ConnState>> states;
        {
            std::lock_guard<std::mutex> lock(mtx);
            states.reserve(connections.size());
            for (auto& [_, state] : connections) states.push_back(state);
            connections.clear();
        }
        for (auto& state : states) retire(std::move(state));
        if (!states.empty())
            connCount.fetch_sub(static_cast<int>(states.size()), std::memory_order_relaxed);
        if (wait) loopCleanup.drain();
    }
};

NodeTcpServer::NodeTcpServer(const Node* config_n) : _p(std::make_unique<Private>())
{
    _p->root = ve::n(config_n->get("root").toString("/"));
    _p->port = config_n->get("port").toInt(0);
}

NodeTcpServer::~NodeTcpServer()
{
    stop();
}

bool NodeTcpServer::start()
{
    _p->server.bind_connect([this](auto& session_ptr) {
        session_ptr->set_disconnect_timeout(std::chrono::seconds(2));
        auto key = session_ptr->hash_key();
        auto state = std::make_shared<Private::ConnState>();
        std::weak_ptr<typename std::decay_t<decltype(session_ptr)>::element_type> weak_session = session_ptr;
        std::weak_ptr<Private::ConnState> weak_state = state;
        state->session = std::make_unique<Session>(_p->root, _p->root,
            [weak_state, weak_session](std::string msg) mutable {
                if (auto session = weak_session.lock()) {
                    session->post([weak_state, session, msg = std::move(msg)]() mutable {
                        auto state = weak_state.lock();
                        if (state && state->connected.load(std::memory_order_acquire)
                            && session->is_started()) session->async_send(msg + "\n");
                    });
                }
            });
        state->loop = std::make_unique<AsioLoop>("node.tcp." + std::to_string(key));
        if (!state->loop->start()) {
            session_ptr->stop();
            return;
        }
        {
            std::lock_guard<std::mutex> lock(_p->mtx);
            _p->connections[key] = std::move(state);
            _p->connCount.fetch_add(1, std::memory_order_relaxed);
        }
    });

    _p->server.bind_recv([this](auto& session_ptr, std::string_view data) {
        auto key = session_ptr->hash_key();
        std::shared_ptr<Private::ConnState> state;
        {
            std::lock_guard<std::mutex> lock(_p->mtx);
            auto it = _p->connections.find(key);
            if (it != _p->connections.end()) state = it->second;
        }
        if (!state) return;

        std::weak_ptr<Private::ConnState> weak_state = state;
        std::weak_ptr<typename std::decay_t<decltype(session_ptr)>::element_type> weak_session = session_ptr;
        state->loop->post([weak_state, weak_session, chunk = std::string(data)]() mutable {
            auto state = weak_state.lock();
            if (!state || !state->connected.load(std::memory_order_acquire)) return;
            state->recvBuf.append(chunk);
            Private::processLines(state, weak_session);
        });
    });

    _p->server.bind_disconnect([this](auto& session_ptr) {
        const auto error = asio2::get_last_error();
        const auto remoteAddress = session_ptr->remote_address();
        const auto remotePort = session_ptr->remote_port();
        auto key = session_ptr->hash_key();
        std::shared_ptr<Private::ConnState> state;
        {
            std::lock_guard<std::mutex> lock(_p->mtx);
            auto it = _p->connections.find(key);
            if (it != _p->connections.end()) {
                state = std::move(it->second);
                _p->connections.erase(it);
            }
        }
        if (state) {
            _p->retire(std::move(state));
            _p->connCount.fetch_sub(1, std::memory_order_relaxed);
        }
        veLogDs("[node/tcp] client disconnected:", remoteAddress, ":", remotePort,
                "reason:", error.value(), error.message());
    });

    ve::service::disableWindowsPortReuse(_p->server);
    return _p->server.start("0.0.0.0", _p->port);
}

void NodeTcpServer::stop(bool wait)
{
    _p->server.stop();
    _p->stopConnections(wait);
}

bool NodeTcpServer::isRunning() const
{
    return _p->server.is_started();
}

int NodeTcpServer::connectionCount() const
{
    return _p->connCount.load(std::memory_order_relaxed);
}

} // namespace service
} // namespace ve
