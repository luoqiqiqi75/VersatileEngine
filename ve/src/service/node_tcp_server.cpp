// node_tcp_server.cpp — ve::service::NodeTcpServer
//
// JSON text protocol over TCP, newline-delimited.
// Same command format as NodeWsServer JSON mode:
//   {"cmd":"get","path":"...","id":1}
//   {"cmd":"set","path":"...","value":...,"id":1}
//   {"cmd":"subscribe","path":"...","id":1}
//   {"cmd":"unsubscribe","path":"...","id":1}

#include "ve/service/node_service.h"
#include "subscribe_service.h"
#include "ve/core/node.h"
#include "ve/core/var.h"
#include "ve/core/command.h"
#include "ve/core/impl/json.h"
#include "ve/core/log.h"

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
// Shared JSON command handling
// ============================================================================

static Var parseJsonMsg(const std::string& raw)
{
    return impl::json::parse(raw);
}

std::string handleNodeJsonCmd(Node* root, const Var& parsed)
{
    if (!parsed.isDict()) {
        return "{\"type\":\"error\",\"msg\":\"invalid json\"}";
    }

    auto& dict = parsed.toDict();
    std::string cmd = dict.has("cmd") ? dict["cmd"].toString() : "";
    std::string path = dict.has("path") ? dict["path"].toString() : "";
    std::string idField;
    if (dict.has("id")) {
        idField = ",\"id\":" + impl::json::stringify(dict["id"]);
    }

    if (cmd == "get") {
        Node* target = path.empty() ? root : root->find(path);
        if (!target) {
            return "{\"type\":\"error\",\"msg\":\"not found\"" + idField + "}";
        }
        return "{\"type\":\"data\",\"path\":"
            + impl::json::stringify(Var(target->path(root)))
            + ",\"value\":" + impl::json::exportTree(target) + idField + "}";
    }
    else if (cmd == "set") {
        if (path.empty()) {
            return "{\"type\":\"error\",\"msg\":\"path required\"" + idField + "}";
        }
        Node* target = root->at(path);
        if (!target) {
            return "{\"type\":\"error\",\"msg\":\"cannot create node\"" + idField + "}";
        }
        if (dict.has("value")) {
            target->set(dict["value"]);
        }
        return "{\"type\":\"ok\",\"path\":"
            + impl::json::stringify(Var(target->path(root))) + idField + "}";
    }
    else if (cmd == "list") {
        Node* target = path.empty() ? root : root->find(path);
        if (!target) {
            return "{\"type\":\"error\",\"msg\":\"not found\"" + idField + "}";
        }
        std::string json = "{\"type\":\"data\",\"path\":"
            + impl::json::stringify(Var(target->path(root)))
            + ",\"children\":[";
        int total = target->count();
        for (int i = 0; i < total; ++i) {
            auto* c = target->child(i);
            if (i > 0) json += ",";
            json += "\"" + c->name() + "\"";
        }
        json += "]" + idField + "}";
        return json;
    }

    return "{\"type\":\"error\",\"msg\":\"unknown command: " + cmd + "\"" + idField + "}";
}

// ============================================================================
// Private
// ============================================================================

struct NodeTcpServer::Private
{
    Node*    root = nullptr;
    uint16_t port = 5082;

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

            // Trim \r
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.empty()) continue;

            auto parsed = parseJsonMsg(line);
            if (!parsed.isDict()) {
                session_ptr->async_send("{\"type\":\"error\",\"msg\":\"invalid json\"}\n");
                continue;
            }

            auto& dict = parsed.toDict();
            std::string cmd = dict.has("cmd") ? dict["cmd"].toString() : "";

            if (cmd == "subscribe") {
                std::string path = dict.has("path") ? dict["path"].toString() : "";
                auto sid = static_cast<uint64_t>(connKey);
                if (subscribeSvc) subscribeSvc->subscribe(sid, path);
                std::string idField;
                if (dict.has("id")) idField = ",\"id\":" + impl::json::stringify(dict["id"]);
                session_ptr->async_send("{\"type\":\"subscribed\",\"path\":"
                    + impl::json::stringify(Var(path)) + idField + "}\n");
            }
            else if (cmd == "unsubscribe") {
                std::string path = dict.has("path") ? dict["path"].toString() : "";
                auto sid = static_cast<uint64_t>(connKey);
                if (subscribeSvc) subscribeSvc->unsubscribe(sid, path);
                std::string idField;
                if (dict.has("id")) idField = ",\"id\":" + impl::json::stringify(dict["id"]);
                session_ptr->async_send("{\"type\":\"unsubscribed\",\"path\":"
                    + impl::json::stringify(Var(path)) + idField + "}\n");
            }
            else {
                std::string response = handleNodeJsonCmd(root, parsed);
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
            std::string pushMsg = "{\"type\":\"event\",\"path\":"
                + impl::json::stringify(Var(path))
                + ",\"value\":" + impl::json::stringify(value) + "}\n";

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

    bool ok = _p->server.start("0.0.0.0", _p->port);
    if (ok)
        veLogIs("NodeTcpServer started on port", _p->port);
    else
        veLogEs("NodeTcpServer failed to start on port", _p->port);
    return ok;
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
