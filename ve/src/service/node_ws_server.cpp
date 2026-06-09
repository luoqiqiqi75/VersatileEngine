// node_ws_service.cpp — ve::service::NodeWsServer
#include "ve/service/node_service.h"
#include "ve/core/node.h"
#include "ve/core/schema.h"
#include "node_session.h"
#include "node_protocol.h"
#include "node_task_service.h"
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

static const schema::ExportOptions compactJson{0};

static std::string toJson(const Node& n)
{
    return schema::exportAs<schema::JsonS>(&n, compactJson);
}

static void fillError(Node* rep, const std::string& code, const std::string& error)
{
    rep->clear();
    rep->set(Var());
    rep->set("ok", false);
    rep->set("code", code);
    rep->set("error", error);
}

struct NodeWsServer::Private
{
    Node*    root = nullptr;
    uint16_t port = 12100;
    asio2::ws_server server;
    std::atomic<int> connCount{0};
    std::mutex mtx;
    std::unordered_map<uint64_t, std::unique_ptr<Session>> sessions;
    std::unique_ptr<NodeTaskService> taskSvc;

    void postToSession(uint64_t sid, std::string message)
    {
        server.post([this, sid, message = std::move(message)]() {
            server.foreach_session([&](auto& session_ptr) {
                if (static_cast<uint64_t>(session_ptr->hash_key()) == sid) {
                    session_ptr->async_send(message);
                }
            });
        });
    }

    // Creates a Session whose pushes are framed and sent back to this connection.
    std::unique_ptr<Session> makeSession(uint64_t sid)
    {
        return std::make_unique<Session>(root, [this, sid](const std::string& path, const Var& value) {
            Node event("event");
            event.set("event", "node.changed");
            event.set("path", path);
            event.at("value")->set(value);
            postToSession(sid, toJson(event));
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
    _p->port = config_n->get("port").toInt(0); // default stop
}

NodeWsServer::~NodeWsServer()
{
    stop();
}

bool NodeWsServer::start()
{
    _p->taskSvc = std::make_unique<NodeTaskService>(_p->root);

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
        std::string msg(data);

        Node req("req");
        if (!schema::importAs<schema::JsonS>(&req, msg)) {
            Node reply("rep");
            fillError(&reply, "invalid_request", "invalid JSON request");
            session_ptr->async_send(toJson(reply));
            return;
        }

        Node reply("rep");
        dispatchNodeProtocol(_p->root, &req, &reply,
                             _p->sessionFor(sid), _p->taskSvc.get(), 500, true,
                             [this, sid](const Node& event) {
                                 _p->postToSession(sid, toJson(event));
                             });
        session_ptr->async_send(toJson(reply));
    });

    _p->server.bind_disconnect([this](auto& session_ptr) {
        auto sid = static_cast<uint64_t>(session_ptr->hash_key());
        {
            std::lock_guard<std::mutex> lock(_p->mtx);
            _p->sessions.erase(sid);   // Session dtor unsubscribes everything
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
    _p->taskSvc.reset();
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
