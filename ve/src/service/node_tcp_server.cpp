// node_tcp_server.cpp — ve::service::NodeTcpServer
#include "ve/service/node_service.h"
#include "ve/core/node.h"
#include "ve/core/schema.h"
#include "subscribe_service.h"
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
    };
    std::unordered_map<std::size_t, ConnState> connections;
    std::unique_ptr<SubscribeService> subscribeSvc;
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
                                 subscribeSvc.get(), taskSvc.get(), 500,
                                 true, static_cast<uint64_t>(connKey), true,
                                 [this, sid = static_cast<uint64_t>(connKey)](const Node& event) {
                                     postToSession(sid, toJson(event) + "\n");
                                 });
            session_ptr->async_send(toJson(reply) + "\n");
        }
    }
};

NodeTcpServer::NodeTcpServer(Node* root, uint16_t port)
    : _p(std::make_unique<Private>())
{
    _p->root = root;
    _p->port = port;
}

NodeTcpServer::~NodeTcpServer()
{
    stop();
}

bool NodeTcpServer::start()
{
    _p->subscribeSvc = std::make_unique<SubscribeService>(_p->root);
    _p->subscribeSvc->setPushCallback([this](uint64_t sessionId, const std::string& path, const Var& value) {
        Node event("event");
        event.set("event", "node.changed");
        event.set("path", path);
        event.at("value")->set(value);
        _p->postToSession(sessionId, toJson(event) + "\n");
    });
    _p->subscribeSvc->start();
    _p->taskSvc = std::make_unique<NodeTaskService>(_p->root);

    _p->server.bind_connect([this](auto& session_ptr) {
        auto key = session_ptr->hash_key();
        {
            std::lock_guard<std::mutex> lock(_p->mtx);
            _p->connections[key] = {};
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
        if (_p->subscribeSvc) {
            _p->subscribeSvc->removeSession(static_cast<uint64_t>(key));
        }
        {
            std::lock_guard<std::mutex> lock(_p->mtx);
            _p->connections.erase(key);
        }
        _p->connCount.fetch_sub(1, std::memory_order_relaxed);
    });

    ve::service::disableWindowsPortReuse(_p->server);
    return _p->server.start("0.0.0.0", _p->port);
}

void NodeTcpServer::stop()
{
    _p->server.stop();
    if (_p->subscribeSvc) {
        _p->subscribeSvc->stop();
        _p->subscribeSvc.reset();
    }
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
