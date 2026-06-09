// node_tcp_server.cpp — ve::service::NodeTcpServer
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

struct NodeTcpServer::Private
{
    Node*    root = nullptr;
    uint16_t port = 12200;

    asio2::tcp_server server;
    std::mutex mtx;
    std::atomic<int> connCount{0};

    struct ConnState {
        std::string recvBuf;
        std::unique_ptr<Session> session;
    };
    std::unordered_map<std::size_t, ConnState> connections;
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

    std::unique_ptr<Session> makeSession(uint64_t sid)
    {
        return std::make_unique<Session>(root, [this, sid](const std::string& path, const Var& value) {
            Node event("event");
            event.set("event", "node.changed");
            event.set("path", path);
            event.at("value")->set(value);
            postToSession(sid, toJson(event) + "\n");
        });
    }

    template<typename SessionPtr>
    void processLines(std::size_t connKey, SessionPtr& session_ptr)
    {
        ConnState* state = nullptr;
        {
            std::lock_guard<std::mutex> lock(mtx);
            auto it = connections.find(connKey);
            if (it != connections.end()) {
                state = &it->second;
            }
        }
        if (!state) {
            return;
        }

        auto& buf = state->recvBuf;
        std::string::size_type pos;
        while ((pos = buf.find('\n')) != std::string::npos) {
            std::string line = buf.substr(0, pos);
            buf.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.empty()) {
                continue;
            }

            Node req("req");
            if (!schema::importAs<schema::JsonS>(&req, line)) {
                Node reply("rep");
                fillError(&reply, "invalid_request", "invalid JSON request");
                session_ptr->async_send(toJson(reply) + "\n");
                continue;
            }

            Node reply("rep");
            dispatchNodeProtocol(root, &req, &reply,
                                 state->session.get(), taskSvc.get(), 500, true,
                                 [this, sid = static_cast<uint64_t>(connKey)](const Node& event) {
                                     postToSession(sid, toJson(event) + "\n");
                                 });
            session_ptr->async_send(toJson(reply) + "\n");
        }
    }
};

NodeTcpServer::NodeTcpServer(const Node* config_n) : _p(std::make_unique<Private>())
{
    _p->root = ve::n(config_n->get("root").toString("/"));
    _p->port = config_n->get("port").toInt(0); // default stop
}

NodeTcpServer::~NodeTcpServer()
{
    stop();
}

bool NodeTcpServer::start()
{
    _p->taskSvc = std::make_unique<NodeTaskService>(_p->root);

    _p->server.bind_connect([this](auto& session_ptr) {
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
            if (it != _p->connections.end()) {
                it->second.recvBuf.append(data);
            }
        }
        _p->processLines(key, session_ptr);
    });

    _p->server.bind_disconnect([this](auto& session_ptr) {
        auto key = session_ptr->hash_key();
        {
            std::lock_guard<std::mutex> lock(_p->mtx);
            _p->connections.erase(key);   // Session dtor unsubscribes everything
        }
        _p->connCount.fetch_sub(1, std::memory_order_relaxed);
    });

    ve::service::disableWindowsPortReuse(_p->server);
    return _p->server.start("0.0.0.0", _p->port);
}

void NodeTcpServer::stop()
{
    _p->server.stop();
    _p->taskSvc.reset();
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
