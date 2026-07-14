// node_tcp_server.cpp — ve::service::NodeTcpServer (multi-session, line-delimited JSON)
#include "ve/service/node_service.h"
#include "ve/core/node.h"
#include "ve/core/schema.h"
#include "ve/core/command.h"
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
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

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

    asio2::tcp_server server{sharedIopool()};
    std::mutex mtx;
    std::atomic<int> connCount{0};

    struct ConnState {
        std::string recvBuf;
        std::unique_ptr<Session> session;
    };
    std::unordered_map<std::size_t, ConnState> connections;

    std::unique_ptr<Session> makeSession(uint64_t sid)
    {
        return std::make_unique<Session>(root, root, [this, sid](std::string msg) {
            server.post([this, sid, msg = std::move(msg)]() {
                server.foreach_session([&](auto& session_ptr) {
                    if (static_cast<uint64_t>(session_ptr->hash_key()) == sid)
                        session_ptr->async_send(msg + "\n");
                });
            });
        });
    }

    template<typename SessionPtr>
    void processLines(std::size_t connKey, SessionPtr& session_ptr)
    {
        ConnState* state = nullptr;
        {
            std::lock_guard<std::mutex> lock(mtx);
            auto it = connections.find(connKey);
            if (it != connections.end()) state = &it->second;
        }
        if (!state) return;

        auto& buf = state->recvBuf;
        std::string::size_type pos;
        while ((pos = buf.find('\n')) != std::string::npos) {
            std::string line = buf.substr(0, pos);
            buf.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;

            Pipeline pipe;
            if (!schema::toNode<schema::JsonS>(pipe.contextNode(), line)) {
                Node err;
                err.set("code", int64_t(ERR_INVALID));
                err.set("message", std::string("invalid JSON"));
                session_ptr->async_send(toJson(err) + "\n");
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
                        session_ptr->async_send(toJson(*pipe.contextNode()) + "\n");
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
                    session_ptr->async_send(toJson(*pipe.contextNode()) + "\n");
                    continue;
                }
                Command* c = pipe.add(command::create(*ref.factory, ref.key));
                c->setContextNodes(pipe.contextNode(), pipe.contextNode()->at("params"), pipe.contextNode()->at("data"));
            }

            pipe.sync();
            if (finalizeReply(pipe))
                session_ptr->async_send(toJson(*pipe.contextNode()) + "\n");
        }
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
        {
            std::lock_guard<std::mutex> lock(_p->mtx);
            _p->connections[key].session = _p->makeSession(static_cast<uint64_t>(key));
        }
        _p->connCount.fetch_add(1, std::memory_order_relaxed);
    });

    _p->server.bind_recv([this](auto& session_ptr, std::string_view data) {
        auto key = session_ptr->hash_key();
        {
            std::lock_guard<std::mutex> lock(_p->mtx);
            auto it = _p->connections.find(key);
            if (it != _p->connections.end())
                it->second.recvBuf.append(data);
        }
        _p->processLines(key, session_ptr);
    });

    _p->server.bind_disconnect([this](auto& session_ptr) {
        auto key = session_ptr->hash_key();
        {
            std::lock_guard<std::mutex> lock(_p->mtx);
            _p->connections.erase(key);
        }
        _p->connCount.fetch_sub(1, std::memory_order_relaxed);
    });

    ve::service::disableWindowsPortReuse(_p->server);
    return _p->server.start("0.0.0.0", _p->port);
}

void NodeTcpServer::stop(bool wait)
{
    if (!wait) {
        _p->server.stop();
        return;
    }
    stopAndWait(_p->server);
    std::lock_guard<std::mutex> lock(_p->mtx);
    _p->connections.clear();
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
