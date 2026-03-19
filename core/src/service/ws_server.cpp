// ws_server.cpp — ve::WsServer: WebSocket transport using SubscribeService + command::call()
#include "ve/service/ws_server.h"
#include "ve/service/subscribe_service.h"
#include "ve/core/node.h"
#include "ve/core/var.h"
#include "ve/core/command.h"
#include "ve/core/impl/json.h"
#include "ve/core/log.h"

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <asio2/http/ws_server.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <mutex>
#include <unordered_map>

namespace ve {

// ============================================================================
// Helpers
// ============================================================================

struct WsCommand
{
    std::string cmd;
    std::string path;
    std::string value;
};

static WsCommand parseWsCommand(const std::string& raw)
{
    WsCommand r;
    Var parsed = json::parse(raw);
    if (parsed.isDict()) {
        auto& dict = parsed.toDict();
        if (dict.has("cmd"))   r.cmd   = dict["cmd"].toString();
        if (dict.has("path"))  r.path  = dict["path"].toString();
        if (dict.has("value")) r.value = json::stringify(dict["value"]);
    }
    return r;
}

// ============================================================================
// Private
// ============================================================================

struct WsServer::Private
{
    Node*    root = nullptr;
    uint16_t port = 8081;

    asio2::ws_server server;
    std::mutex mtx;
    std::atomic<int> connCount{0};

    std::unique_ptr<SubscribeService> subscribeSvc;
};

// ============================================================================
// WsServer
// ============================================================================

WsServer::WsServer(Node* root, uint16_t port)
    : _p(std::make_unique<Private>())
{
    _p->root = root;
    _p->port = port;
}

WsServer::~WsServer()
{
    stop();
}

bool WsServer::start()
{
    // --- SubscribeService setup ---
    _p->subscribeSvc = std::make_unique<SubscribeService>(_p->root);
    _p->subscribeSvc->setPushCallback(
        [this](uint64_t sessionId, const std::string& path, const Var& value) {
            std::string eventMsg = "{\"type\":\"event\",\"path\":"
                + json::stringify(Var(path))
                + ",\"value\":" + json::stringify(value) + "}";

            _p->server.post([this, sessionId, eventMsg = std::move(eventMsg)]() {
                _p->server.foreach_session(
                    [&](auto& session_ptr) {
                        if (static_cast<uint64_t>(session_ptr->hash_key()) == sessionId)
                            session_ptr->async_send(eventMsg);
                    });
            });
        });
    _p->subscribeSvc->start();

    // --- Connection handling ---
    _p->server.bind_connect([this](auto& session_ptr) {
        _p->connCount.fetch_add(1, std::memory_order_relaxed);
    });

    _p->server.bind_recv([this](auto& session_ptr, std::string_view data) {
        std::string msg(data);
        auto cmd = parseWsCommand(msg);
        auto sessionId = static_cast<uint64_t>(session_ptr->hash_key());

        if (cmd.cmd == "get") {
            Var input = cmd.path.empty() ? Var(std::string{}) : Var(cmd.path);
            Node* target = cmd.path.empty() ? _p->root : _p->root->resolve(cmd.path);
            if (!target) {
                session_ptr->async_send("{\"type\":\"error\",\"msg\":\"node not found\"}");
                return;
            }
            std::string resp = "{\"type\":\"data\",\"path\":" + json::stringify(Var(target->path(_p->root)))
                + ",\"value\":" + json::exportTree(target) + "}";
            session_ptr->async_send(std::move(resp));
        }
        else if (cmd.cmd == "set") {
            if (cmd.path.empty()) {
                session_ptr->async_send("{\"type\":\"error\",\"msg\":\"path required\"}");
                return;
            }
            Node* target = _p->root->ensure(cmd.path);
            if (!target) {
                session_ptr->async_send("{\"type\":\"error\",\"msg\":\"cannot create node\"}");
                return;
            }
            if (!cmd.value.empty()) {
                target->set(json::parse(cmd.value));
            }
            session_ptr->async_send("{\"type\":\"ok\",\"path\":" + json::stringify(Var(target->path(_p->root))) + "}");
        }
        else if (cmd.cmd == "subscribe") {
            _p->subscribeSvc->subscribe(sessionId, cmd.path);
            session_ptr->async_send("{\"type\":\"subscribed\",\"path\":" + json::stringify(Var(cmd.path)) + "}");
        }
        else if (cmd.cmd == "unsubscribe") {
            _p->subscribeSvc->unsubscribe(sessionId, cmd.path);
            session_ptr->async_send("{\"type\":\"unsubscribed\",\"path\":" + json::stringify(Var(cmd.path)) + "}");
        }
        else {
            session_ptr->async_send("{\"type\":\"error\",\"msg\":\"unknown command: " + cmd.cmd + "\"}");
        }
    });

    _p->server.bind_disconnect([this](auto& session_ptr) {
        auto sessionId = static_cast<uint64_t>(session_ptr->hash_key());
        _p->subscribeSvc->removeSession(sessionId);
        _p->connCount.fetch_sub(1, std::memory_order_relaxed);
    });

    bool ok = _p->server.start("0.0.0.0", _p->port);
    if (ok)
        veLogIs("WsServer started on port", _p->port);
    else
        veLogEs("WsServer failed to start on port", _p->port);
    return ok;
}

void WsServer::stop()
{
    if (_p->subscribeSvc) {
        _p->subscribeSvc->stop();
        _p->subscribeSvc.reset();
    }
    _p->server.stop();
}

bool WsServer::isRunning() const
{
    return _p->server.is_started();
}

int WsServer::connectionCount() const
{
    return _p->connCount.load(std::memory_order_relaxed);
}

} // namespace ve
