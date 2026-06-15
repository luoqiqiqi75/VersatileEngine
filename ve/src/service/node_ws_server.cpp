// node_ws_service.cpp — ve::service::NodeWsServer (multi-session)
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
#include <asio2/http/ws_server.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace ve {
namespace service {

static std::string toJson(const Node& n)
{
    return schema::exportAs<schema::JsonS>(&n, schema::JsonS::compact());
}

struct NodeWsServer::Private
{
    Node*    root = nullptr;
    uint16_t port = 12100;
    asio2::ws_server server;
    std::atomic<int> connCount{0};
    std::mutex mtx;
    std::unordered_map<uint64_t, std::unique_ptr<Session>> sessions;

    std::unique_ptr<Session> makeSession(uint64_t sid)
    {
        return std::make_unique<Session>(root, root, [this, sid](std::string msg) {
            server.post([this, sid, msg = std::move(msg)]() {
                server.foreach_session([&](auto& session_ptr) {
                    if (static_cast<uint64_t>(session_ptr->hash_key()) == sid)
                        session_ptr->async_send(msg);
                });
            });
        });
    }

    Session* sessionFor(uint64_t sid)
    {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = sessions.find(sid);
        return it == sessions.end() ? nullptr : it->second.get();
    }
};

NodeWsServer::NodeWsServer(const Node* config_n) : _p(std::make_unique<Private>())
{
    _p->root = ve::n(config_n->get("root").toString("/"));
    _p->port = config_n->get("port").toInt(0);
}

NodeWsServer::~NodeWsServer()
{
    stop();
}

bool NodeWsServer::start()
{
    registerNodeCommands();

    _p->server.bind_connect([this](auto& session_ptr) {
        auto sid = static_cast<uint64_t>(session_ptr->hash_key());
        {
            std::lock_guard<std::mutex> lock(_p->mtx);
            _p->sessions[sid] = _p->makeSession(sid);
        }
        _p->connCount.fetch_add(1, std::memory_order_relaxed);
    });

    _p->server.bind_recv([this](auto& session_ptr, std::string_view data) {
        auto sid = static_cast<uint64_t>(session_ptr->hash_key());

        Pipeline pipe;
        if (!schema::importAs<schema::JsonS>(pipe.contextNode(), std::string(data))) {
            Node err;
            err.set("code", int64_t(ERR_INVALID));
            err.set("message", std::string("invalid JSON"));
            session_ptr->async_send(toJson(err));
            return;
        }

        std::string cmd_str = pipe.contextNode()->get("cmd").toString();
        auto ref = resolveCmd(cmd_str);
        if (!ref.factory) {
            pipe.contextNode()->erase("params");
            pipe.contextNode()->set("code", int64_t(cmd_str.empty() ? ERR_INVALID : ERR_NOT_FOUND));
            pipe.contextNode()->set("message", cmd_str.empty() ? std::string("cmd required") : "unknown: " + cmd_str);
            session_ptr->async_send(toJson(*pipe.contextNode()));
            return;
        }

        pipe.contextNode()->set("_session", Var::ptr(_p->sessionFor(sid)));
        Command* c = pipe.add(command::create(*ref.factory, ref.key));
        c->setContextNodes(pipe.contextNode(), pipe.contextNode()->at("params"), pipe.contextNode()->at("data"));
        pipe.sync();

        if (finalizeReply(pipe))
            session_ptr->async_send(toJson(*pipe.contextNode()));
    });

    _p->server.bind_disconnect([this](auto& session_ptr) {
        auto sid = static_cast<uint64_t>(session_ptr->hash_key());
        {
            std::lock_guard<std::mutex> lock(_p->mtx);
            _p->sessions.erase(sid);
        }
        _p->connCount.fetch_sub(1, std::memory_order_relaxed);
    });

    ve::service::disableWindowsPortReuse(_p->server);
    return _p->server.start("0.0.0.0", _p->port);
}

void NodeWsServer::stop()
{
    _p->server.stop();
    {
        std::lock_guard<std::mutex> lock(_p->mtx);
        _p->sessions.clear();
    }
}

bool NodeWsServer::isRunning() const
{
    return _p->server.is_started();
}

int NodeWsServer::connectionCount() const
{
    return _p->connCount.load(std::memory_order_relaxed);
}

} // namespace service
} // namespace ve
