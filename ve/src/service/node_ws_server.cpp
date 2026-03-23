// node_ws_service.cpp — ve::service::NodeWsServer
//
// Dual-mode protocol:
//   1. MozHelper text: {channel}:{fn}:{info}:{key}:{param}
//      Functions: g (get), s (set), t (trigger), w (watch), c (command)
//   2. JSON (backward compat): {"cmd":"get","path":"...","value":"...","id":"..."}
//
// Auto-detection: message starting with '{' -> JSON, otherwise -> text protocol.

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
#include <asio2/http/ws_server.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <mutex>
#include <unordered_map>

namespace ve {
namespace service {

// ============================================================================
// Text Protocol Parser (MozHelper compatible)
// ============================================================================

struct MozLine
{
    std::string channel;
    std::string fn;
    std::string info;
    std::string key;
    std::string param;
};

static MozLine parseMozLine(const std::string& raw)
{
    MozLine m;
    size_t pos = 0;
    int field = 0;
    for (size_t i = 0; i <= raw.size() && field < 4; ++i) {
        if (i == raw.size() || raw[i] == ':') {
            std::string_view sv(raw.data() + pos, i - pos);
            switch (field) {
            case 0: m.channel = std::string(sv); break;
            case 1: m.fn      = std::string(sv); break;
            case 2: m.info    = std::string(sv); break;
            case 3: m.key     = std::string(sv); break;
            }
            ++field;
            pos = i + 1;
        }
    }
    if (field >= 4 && pos <= raw.size()) {
        m.param = raw.substr(pos);
    }
    return m;
}

// ============================================================================
// JSON Protocol Parser (backward compat)
// ============================================================================

struct JsonCommand
{
    std::string cmd;
    std::string path;
    std::string value;
    std::string id;
};

static JsonCommand parseJsonCommand(const std::string& raw)
{
    JsonCommand r;
    Var parsed = impl::json::parse(raw);
    if (parsed.isDict()) {
        auto& dict = parsed.toDict();
        if (dict.has("cmd"))   r.cmd   = dict["cmd"].toString();
        if (dict.has("path"))  r.path  = dict["path"].toString();
        if (dict.has("value")) r.value = impl::json::stringify(dict["value"]);
        if (dict.has("id"))    r.id    = impl::json::stringify(dict["id"]);
    }
    return r;
}

static std::string appendId(const std::string& jsonStr, const std::string& id)
{
    if (id.empty()) {
        return jsonStr;
    }
    auto pos = jsonStr.rfind('}');
    if (pos == std::string::npos) {
        return jsonStr;
    }
    return jsonStr.substr(0, pos) + ",\"id\":" + id + "}";
}

// ============================================================================
// Private
// ============================================================================

struct NodeWsServer::Private
{
    Node*    root = nullptr;
    uint16_t port = 8081;

    asio2::ws_server server;
    std::atomic<int> connCount{0};
    std::unique_ptr<SubscribeService> subscribeSvc;

    struct SessionState {
        int64_t requestIdCounter = 0;
        bool textProtocol = false;
    };

    std::mutex mtx;
    std::unordered_map<uint64_t, SessionState> sessions;

    int64_t nextRequestId(uint64_t sid) {
        std::lock_guard<std::mutex> lock(mtx);
        return ++sessions[sid].requestIdCounter;
    }

    bool isTextSession(uint64_t sid) {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = sessions.find(sid);
        return it != sessions.end() && it->second.textProtocol;
    }

    void markTextProtocol(uint64_t sid) {
        std::lock_guard<std::mutex> lock(mtx);
        sessions[sid].textProtocol = true;
    }

    // --- Text protocol handlers ---

    template<typename SP>
    void textGet(SP& sp, uint64_t sid, const MozLine& m) {
        int64_t rid = nextRequestId(sid);
        Node* target = m.key.empty() ? root : root->find(m.key);
        if (!target) {
            sp->async_send("{\"requestId\":" + std::to_string(rid) + ",\"error\":\"not found\"}");
            return;
        }
        sp->async_send("{\"requestId\":" + std::to_string(rid)
            + ",\"data\":" + impl::json::exportTree(target) + "}");
    }

    template<typename SP>
    void textSet(SP& sp, uint64_t sid, const MozLine& m) {
        int64_t rid = nextRequestId(sid);
        if (m.key.empty()) {
            sp->async_send("{\"requestId\":" + std::to_string(rid) + ",\"error\":\"path required\"}");
            return;
        }
        Node* target = root->find(m.key);
        if (!target) {
            sp->async_send("{\"requestId\":" + std::to_string(rid) + ",\"error\":\"cannot create node\"}");
            return;
        }
        if (!m.param.empty()) {
            target->set(impl::json::parse(m.param));
        }
        sp->async_send("{\"requestId\":" + std::to_string(rid) + ",\"data\":\"ok\"}");
    }

    template<typename SP>
    void textTrigger(SP& sp, uint64_t sid, const MozLine& m) {
        int64_t rid = nextRequestId(sid);
        if (m.key.empty()) {
            sp->async_send("{\"requestId\":" + std::to_string(rid) + ",\"error\":\"path required\"}");
            return;
        }
        Node* target = root->find(m.key);
        if (!target) {
            sp->async_send("{\"requestId\":" + std::to_string(rid) + ",\"error\":\"not found\"}");
            return;
        }
        if (!m.param.empty()) {
            target->set(impl::json::parse(m.param));
        } else {
            target->trigger<Node::NODE_CHANGED>();
            target->activate(Node::NODE_CHANGED, target);
        }
        sp->async_send("{\"requestId\":" + std::to_string(rid) + ",\"data\":\"ok\"}");
    }

    template<typename SP>
    void textWatch(SP& sp, uint64_t sid, const MozLine& m) {
        int64_t rid = nextRequestId(sid);
        subscribeSvc->subscribe(sid, m.key);
        sp->async_send("{\"requestId\":" + std::to_string(rid) + ",\"data\":\"subscribed\"}");
    }

    template<typename SP>
    void textCommand(SP& sp, uint64_t /*sid*/, const MozLine& m) {
        Var paramVar = m.param.empty() ? Var() : impl::json::parse(m.param);

        int64_t cmdId = 0;
        Var body;
        if (paramVar.isDict()) {
            auto& dict = paramVar.toDict();
            if (dict.has("id"))   cmdId = dict["id"].toInt64();
            if (dict.has("body")) body  = dict["body"];
        }

        std::string idStr = std::to_string(cmdId);

        if (!command::has(m.key)) {
            sp->async_send("{\"id\":" + idStr
                + ",\"error\":\"Unknown command: " + m.key + "\"}");
            return;
        }

        auto result = command::call(m.key, body);
        if (result.isSuccess() || result.isAccepted()) {
            std::string data = impl::json::stringify(result.content());
            sp->async_send("{\"id\":" + idStr
                + ",\"c\":{" + impl::json::stringify(Var(m.key)) + ":" + data + "}}");
        } else {
            sp->async_send("{\"id\":" + idStr
                + ",\"error\":" + impl::json::stringify(Var(result).toString()) + "}");
        }
    }

    // --- JSON protocol handlers (backward compat) ---

    template<typename SP>
    void jsonMessage(SP& sp, uint64_t sid, const std::string& msg) {
        auto cmd = parseJsonCommand(msg);

        if (cmd.cmd == "get") {
            Node* target = cmd.path.empty() ? root : root->at(cmd.path);
            if (!target) {
                sp->async_send(appendId("{\"type\":\"error\",\"msg\":\"node not found\"}", cmd.id));
                return;
            }
            std::string resp = "{\"type\":\"data\",\"path\":"
                + impl::json::stringify(Var(target->path(root)))
                + ",\"value\":" + impl::json::exportTree(target) + "}";
            sp->async_send(appendId(std::move(resp), cmd.id));
        }
        else if (cmd.cmd == "set") {
            if (cmd.path.empty()) {
                sp->async_send(appendId("{\"type\":\"error\",\"msg\":\"path required\"}", cmd.id));
                return;
            }
            Node* target = root->at(cmd.path);
            if (!target) {
                sp->async_send(appendId("{\"type\":\"error\",\"msg\":\"cannot create node\"}", cmd.id));
                return;
            }
            if (!cmd.value.empty()) {
                target->set(impl::json::parse(cmd.value));
            }
            sp->async_send(appendId("{\"type\":\"ok\",\"path\":"
                + impl::json::stringify(Var(target->path(root))) + "}", cmd.id));
        }
        else if (cmd.cmd == "subscribe") {
            subscribeSvc->subscribe(sid, cmd.path);
            sp->async_send(appendId("{\"type\":\"subscribed\",\"path\":"
                + impl::json::stringify(Var(cmd.path)) + "}", cmd.id));
        }
        else if (cmd.cmd == "unsubscribe") {
            subscribeSvc->unsubscribe(sid, cmd.path);
            sp->async_send(appendId("{\"type\":\"unsubscribed\",\"path\":"
                + impl::json::stringify(Var(cmd.path)) + "}", cmd.id));
        }
        else {
            sp->async_send(appendId("{\"type\":\"error\",\"msg\":\"unknown command: "
                + cmd.cmd + "\"}", cmd.id));
        }
    }
};

// ============================================================================
// NodeWsServer
// ============================================================================

NodeWsServer::NodeWsServer(Node* root, uint16_t port)
    : _p(std::make_unique<Private>())
{
    _p->root = root;
    _p->port = port;
}

NodeWsServer::~NodeWsServer()
{
    stop();
}

bool NodeWsServer::start()
{
    // --- SubscribeService setup ---
    _p->subscribeSvc = std::make_unique<SubscribeService>(_p->root);
    _p->subscribeSvc->setPushCallback(
        [this](uint64_t sessionId, const std::string& path, const Var& value) {
            std::string pushMsg;
            if (_p->isTextSession(sessionId)) {
                pushMsg = "{\"key\":" + impl::json::stringify(Var(path))
                    + ",\"data\":" + impl::json::stringify(value) + "}";
            } else {
                pushMsg = "{\"type\":\"event\",\"path\":"
                    + impl::json::stringify(Var(path))
                    + ",\"value\":" + impl::json::stringify(value) + "}";
            }

            _p->server.post([this, sessionId, pushMsg = std::move(pushMsg)]() {
                _p->server.foreach_session(
                    [&](auto& session_ptr) {
                        if (static_cast<uint64_t>(session_ptr->hash_key()) == sessionId)
                            session_ptr->async_send(pushMsg);
                    });
            });
        });
    _p->subscribeSvc->start();

    // --- Connection handling ---
    _p->server.bind_connect([this](auto& session_ptr) {
        auto sid = static_cast<uint64_t>(session_ptr->hash_key());
        {
            std::lock_guard<std::mutex> lock(_p->mtx);
            _p->sessions[sid] = {};
        }
        _p->connCount.fetch_add(1, std::memory_order_relaxed);
    });

    _p->server.bind_recv([this](auto& session_ptr, std::string_view data) {
        std::string msg(data);
        auto sid = static_cast<uint64_t>(session_ptr->hash_key());

        if (!msg.empty() && msg[0] == '{') {
            _p->jsonMessage(session_ptr, sid, msg);
        } else {
            _p->markTextProtocol(sid);
            auto m = parseMozLine(msg);

            if      (m.fn == "g") { _p->textGet(session_ptr, sid, m); }
            else if (m.fn == "s") { _p->textSet(session_ptr, sid, m); }
            else if (m.fn == "t") { _p->textTrigger(session_ptr, sid, m); }
            else if (m.fn == "w") { _p->textWatch(session_ptr, sid, m); }
            else if (m.fn == "c") { _p->textCommand(session_ptr, sid, m); }
            else {
                int64_t rid = _p->nextRequestId(sid);
                session_ptr->async_send("{\"requestId\":" + std::to_string(rid)
                    + ",\"error\":\"unknown function: " + m.fn + "\"}");
            }
        }
    });

    _p->server.bind_disconnect([this](auto& session_ptr) {
        auto sid = static_cast<uint64_t>(session_ptr->hash_key());
        _p->subscribeSvc->removeSession(sid);
        {
            std::lock_guard<std::mutex> lock(_p->mtx);
            _p->sessions.erase(sid);
        }
        _p->connCount.fetch_sub(1, std::memory_order_relaxed);
    });

    bool ok = _p->server.start("0.0.0.0", _p->port);
    if (ok)
        veLogIs("NodeWsServer started on port", _p->port);
    else
        veLogEs("NodeWsServer failed to start on port", _p->port);
    return ok;
}

void NodeWsServer::stop()
{
    if (_p->subscribeSvc) {
        _p->subscribeSvc->stop();
        _p->subscribeSvc.reset();
    }
    _p->server.stop();
    std::lock_guard<std::mutex> lock(_p->mtx);
    _p->sessions.clear();
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
