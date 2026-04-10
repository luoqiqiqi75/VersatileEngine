// node_tcp_server.cpp — ve::service::NodeTcpServer
//
// JSON text protocol over TCP, newline-delimited.
// Request/response use Node + schema::exportAs<JsonS> for serialization.
//   {"cmd":"get","path":"...","id":1}
//   {"cmd":"set","path":"...","value":...,"id":1}
//   {"cmd":"subscribe","path":"...","id":1}
//   {"cmd":"unsubscribe","path":"...","id":1}

#include "ve/service/node_service.h"
#include "subscribe_service.h"
#include "ve/core/node.h"
#include "ve/core/var.h"
#include "ve/core/command.h"
#include "ve/core/schema.h"
#include "ve/core/log.h"
#include "server_util.h"

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <asio2/tcp/tcp_server.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <mutex>
#include <unordered_map>

namespace ve {
namespace service {

// ============================================================================
// Shared Node-based JSON command handling
// ============================================================================

static std::string nodeToJson(Node& n)
{
    return schema::exportAs<schema::JsonS>(&n, schema::ExportOptions{0});
}

std::string handleNodeJsonCmd(Node* root, Node* reqNode)
{
    std::string cmd = reqNode->get("cmd").toString();
    std::string path = reqNode->get("path").toString();

    Node resp("r");
    if (!reqNode->get("id").isNull()) resp.at("id")->set(reqNode->get("id"));

    if (cmd == "get") {
        Node* target = path.empty() ? root : root->find(path);
        if (!target) {
            resp.set("type", "error");
            resp.set("msg", "not found");
            return nodeToJson(resp);
        }
        resp.set("type", "data");
        resp.set("path", target->path(root));
        resp.at("value")->copy(target);
        return nodeToJson(resp);
    }
    else if (cmd == "set") {
        if (path.empty()) {
            resp.set("type", "error");
            resp.set("msg", "path required");
            return nodeToJson(resp);
        }
        Node* target = root->at(path);
        if (!target) {
            resp.set("type", "error");
            resp.set("msg", "cannot create node");
            return nodeToJson(resp);
        }
        Node* valNode = reqNode->find("value");
        if (valNode) {
            target->copy(valNode);
        }
        resp.set("type", "ok");
        resp.set("path", target->path(root));
        return nodeToJson(resp);
    }
    else if (cmd == "list") {
        Node* target = path.empty() ? root : root->find(path);
        if (!target) {
            resp.set("type", "error");
            resp.set("msg", "not found");
            return nodeToJson(resp);
        }
        resp.set("type", "data");
        resp.set("path", target->path(root));
        Var::ListV children;
        for (int i = 0; i < target->count(); ++i) {
            children.push_back(Var(target->child(i)->name()));
        }
        resp.set("children", Var(std::move(children)));
        return nodeToJson(resp);
    }
    else if (cmd == "command.run") {
        std::string name = reqNode->get("name").toString();
        if (name.empty()) {
            resp.set("type", "error");
            resp.set("msg", "command name required");
            return nodeToJson(resp);
        }
        if (!command::has(name)) {
            resp.set("type", "error");
            resp.set("msg", "unknown command: " + name);
            return nodeToJson(resp);
        }

        // Convert args to Var (should be a list)
        Var args;
        if (reqNode->find("args")) {
            args = schema::exportAs<schema::VarS>(reqNode->find("args"));
        }

        Result result = command::call(name, args);
        if (result.isSuccess() || result.isAccepted()) {
            resp.set("type", "ok");
            resp.at("result")->set(result.content());
        } else {
            resp.set("type", "error");
            resp.set("msg", result.content().toString());
        }
        return nodeToJson(resp);
    }

    resp.set("type", "error");
    resp.set("msg", "unknown command: " + cmd);
    return nodeToJson(resp);
}

// ============================================================================
// Private
// ============================================================================

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

    template<typename SessionPtr>
    void processLines(std::size_t connKey, SessionPtr& session_ptr)
    {
        ConnState* cs = nullptr;
        {
            std::lock_guard<std::mutex> lock(mtx);
            auto it = connections.find(connKey);
            if (it != connections.end()) cs = &it->second;
        }
        if (!cs) return;

        auto& buf = cs->recvBuf;
        std::string::size_type pos;
        while ((pos = buf.find('\n')) != std::string::npos) {
            std::string line = buf.substr(0, pos);
            buf.erase(0, pos + 1);

            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.empty()) continue;

            // Parse request via schema
            Node req("req");
            if (!schema::importAs<schema::JsonS>(&req, line)) {
                session_ptr->async_send("{\"type\":\"error\",\"msg\":\"invalid json\"}\n");
                continue;
            }

            std::string cmd = req.get("cmd").toString();

            if (cmd == "subscribe") {
                std::string path = req.get("path").toString();
                auto sid = static_cast<uint64_t>(connKey);
                if (subscribeSvc) subscribeSvc->subscribe(sid, path);
                Node resp("r");
                resp.set("type", "subscribed");
                resp.set("path", path);
                if (!req.get("id").isNull()) resp.at("id")->set(req.get("id"));
                session_ptr->async_send(nodeToJson(resp) + "\n");
            }
            else if (cmd == "unsubscribe") {
                std::string path = req.get("path").toString();
                auto sid = static_cast<uint64_t>(connKey);
                if (subscribeSvc) subscribeSvc->unsubscribe(sid, path);
                Node resp("r");
                resp.set("type", "unsubscribed");
                resp.set("path", path);
                if (!req.get("id").isNull()) resp.at("id")->set(req.get("id"));
                session_ptr->async_send(nodeToJson(resp) + "\n");
            }
            else {
                std::string response = handleNodeJsonCmd(root, &req);
                session_ptr->async_send(response + "\n");
            }
        }
    }
};

// ============================================================================
// NodeTcpServer
// ============================================================================

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
    _p->subscribeSvc->setPushCallback(
        [this](uint64_t sessionId, const std::string& path, const Var& value) {
            Node evt("e");
            evt.set("type", "event");
            evt.set("path", path);
            evt.at("value")->set(value);
            std::string pushMsg = nodeToJson(evt) + "\n";

            _p->server.post([this, sessionId, pushMsg = std::move(pushMsg)]() {
                _p->server.foreach_session(
                    [&](auto& session_ptr) {
                        if (static_cast<uint64_t>(session_ptr->hash_key()) == sessionId)
                            session_ptr->async_send(pushMsg);
                    });
            });
        });
    _p->subscribeSvc->start();

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

    return startServerWithPortFallback(_p->server, "NodeTcpServer", _p->port);
}

void NodeTcpServer::stop()
{
    _p->server.stop();
    if (_p->subscribeSvc) {
        _p->subscribeSvc->stop();
        _p->subscribeSvc.reset();
    }
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
