// node_ws_service.cpp — ve::service::NodeWsServer
//
// Dual-mode protocol:
//   1. MozHelper text: {channel}:{fn}:{info}:{key}:{param}
//      Functions: g (get), s (set), t (trigger), w (watch), c (command)
//   2. JSON: {"cmd":"get",...} etc. Auto-detect: first byte '{' -> JSON.
//
// JSON command.run:
//   wait true  -> one frame {type:"ok"|"error", id?, result|msg} (blocks until pipeline done).
//   wait false -> default; async: frame {type:"accepted", id?} then {type:"result", id?, ok, result|msg}.
//   Sync completion (no main-loop defer): single {type:"ok",...} as with wait true.

#include "ve/service/node_service.h"
#include "subscribe_service.h"
#include "ve/core/node.h"
#include "ve/core/var.h"
#include "ve/core/command.h"
#include "ve/core/pipeline.h"
#include "ve/core/schema.h"
#include "ve/core/impl/json.h"
#include "ve/core/log.h"
#include "server_util.h"

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

// from node_tcp_server.cpp
extern std::string handleNodeJsonCmd(Node* root, Node* reqNode);

// ============================================================================
// Schema-based JSON helpers
// ============================================================================

static const schema::ExportOptions compactJson{0};

static std::string J(Node& n)
{
    return schema::exportAs<schema::JsonS>(&n, compactJson);
}

// ============================================================================
// Text protocol parser (legacy MozHelper)
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
// Private
// ============================================================================

struct NodeWsServer::Private
{
    Node*    root = nullptr;
    uint16_t port = 12100;

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

    // --- Text protocol handlers (legacy, kept as-is for compat) ---

    template<typename SP>
    void textGet(SP& sp, uint64_t sid, const MozLine& m) {
        int64_t rid = nextRequestId(sid);
        Node* target = m.key.empty() ? root : root->find(m.key);
        if (!target) {
            Node r("r");
            r.set("requestId", rid);
            r.set("error", "not found");
            sp->async_send(J(r));
            return;
        }
        Node r("r");
        r.set("requestId", rid);
        r.at("data")->copy(target);
        sp->async_send(J(r));
    }

    template<typename SP>
    void textSet(SP& sp, uint64_t sid, const MozLine& m) {
        int64_t rid = nextRequestId(sid);
        if (m.key.empty()) {
            Node r("r"); r.set("requestId", rid); r.set("error", "path required");
            sp->async_send(J(r));
            return;
        }
        Node* target = root->find(m.key);
        if (!target) {
            Node r("r"); r.set("requestId", rid); r.set("error", "cannot create node");
            sp->async_send(J(r));
            return;
        }
        if (!m.param.empty()) {
            target->set(impl::json::parse(m.param));
        }
        Node r("r"); r.set("requestId", rid); r.set("data", "ok");
        sp->async_send(J(r));
    }

    template<typename SP>
    void textTrigger(SP& sp, uint64_t sid, const MozLine& m) {
        int64_t rid = nextRequestId(sid);
        if (m.key.empty()) {
            Node r("r"); r.set("requestId", rid); r.set("error", "path required");
            sp->async_send(J(r));
            return;
        }
        Node* target = root->find(m.key);
        if (!target) {
            Node r("r"); r.set("requestId", rid); r.set("error", "not found");
            sp->async_send(J(r));
            return;
        }
        if (!m.param.empty()) {
            target->set(impl::json::parse(m.param));
        } else {
            target->trigger<Node::NODE_CHANGED>();
            target->activate(Node::NODE_CHANGED, target);
        }
        Node r("r"); r.set("requestId", rid); r.set("data", "ok");
        sp->async_send(J(r));
    }

    template<typename SP>
    void textWatch(SP& sp, uint64_t sid, const MozLine& m) {
        int64_t rid = nextRequestId(sid);
        if (subscribeSvc) subscribeSvc->subscribe(sid, m.key);
        Node r("r"); r.set("requestId", rid); r.set("data", "subscribed");
        sp->async_send(J(r));
    }

    template<typename SP>
    void textCommand(SP& sp, uint64_t sid, const MozLine& m) {
        Var paramVar = m.param.empty() ? Var() : impl::json::parse(m.param);

        int64_t cmdId = 0;
        Var body;
        bool waitCmd = false;
        if (paramVar.isDict()) {
            auto& dict = paramVar.toDict();
            if (dict.has("id")) {
                cmdId = dict["id"].toInt64();
            }
            if (dict.has("body")) {
                body = dict["body"];
            }
            if (dict.has("wait")) {
                waitCmd = dict["wait"].toBool();
            }
        }

        if (!command::has(m.key)) {
            Node r("r");
            r.set("id", cmdId);
            r.set("error", "Unknown command: " + m.key);
            sp->async_send(J(r));
            return;
        }

        if (waitCmd) {
            auto result = command::call(m.key, body, true);
            Node r("r");
            r.set("id", cmdId);
            if (result.isSuccess() || result.isAccepted()) {
                r.at("c")->at(m.key)->set(result.content());
            } else {
                r.set("error", Var(result).toString());
            }
            sp->async_send(J(r));
            return;
        }

        Node* ctx = command::context(m.key);
        command::parseArgs(ctx, body);
        Pipeline* detached = nullptr;
        auto        result = command::call(m.key, ctx, false, &detached);
        if (detached) {
            std::string cmdKey = m.key;
            detached->setResultHandler([sp, cmdId, cmdKey, ctx, detached](const Result& res) {
                Node r2("r");
                r2.set("id", cmdId);
                r2.set("type", "result");
                if (res.isSuccess() || res.isAccepted()) {
                    r2.at("c")->at(cmdKey)->set(res.content());
                } else {
                    r2.set("error", Var(res).toString());
                }
                sp->async_send(J(r2));
                delete detached;
                delete ctx;
            });

            Node r("r");
            r.set("id", cmdId);
            r.set("type", "accepted");
            r.set("accepted", true);
            sp->async_send(J(r));
            return;
        }

        delete ctx;
        Node r("r");
        r.set("id", cmdId);
        if (result.isSuccess() || result.isAccepted()) {
            r.at("c")->at(m.key)->set(result.content());
        } else {
            r.set("error", Var(result).toString());
        }
        sp->async_send(J(r));
    }

    /// JSON command.run with optional wait (default false = async two-frame when deferred to main loop).
    template<typename SP>
    void jsonCommandRun(SP& sp, Node& req)
    {
        std::string name = req.get("name").toString();
        Var         idVar = req.get("id");

        auto sendErr = [&](const std::string& msg) {
            Node r("r");
            r.set("type", "error");
            r.set("msg", msg);
            if (!idVar.isNull()) {
                r.at("id")->set(idVar);
            }
            sp->async_send(J(r));
        };

        if (name.empty()) {
            sendErr("command name required");
            return;
        }
        if (!command::has(name)) {
            sendErr("unknown command: " + name);
            return;
        }

        Var args;
        if (req.find("args")) {
            args = schema::exportAs<schema::VarS>(req.find("args"));
        }

        const bool waitCmd = req.get("wait").toBool(false);

        if (waitCmd) {
            Result       result = command::call(name, args, true);
            Node         r("r");
            if (!idVar.isNull()) {
                r.at("id")->set(idVar);
            }
            if (result.isSuccess() || result.isAccepted()) {
                r.set("type", "ok");
                r.at("result")->set(result.content());
            } else {
                r.set("type", "error");
                r.set("msg", result.content().toString());
            }
            sp->async_send(J(r));
            return;
        }

        Node*     ctx = command::context(name);
        command::parseArgs(ctx, args);
        Pipeline* detached = nullptr;
        Result    r0       = command::call(name, ctx, false, &detached);

        if (detached) {
            detached->setResultHandler([sp, ctx, detached, idVar](const Result& res) {
                Node out("r");
                out.set("type", "result");
                if (!idVar.isNull()) {
                    out.at("id")->set(idVar);
                }
                if (res.isSuccess() || res.isAccepted()) {
                    out.set("ok", true);
                    out.at("result")->set(res.content());
                } else {
                    out.set("ok", false);
                    out.set("msg", res.content().toString());
                }
                sp->async_send(J(out));
                delete detached;
                delete ctx;
            });

            Node acc("r");
            acc.set("type", "accepted");
            acc.set("accepted", true);
            if (!idVar.isNull()) {
                acc.at("id")->set(idVar);
            }
            sp->async_send(J(acc));
            return;
        }

        delete ctx;
        Node r("r");
        if (!idVar.isNull()) {
            r.at("id")->set(idVar);
        }
        if (r0.isSuccess() || r0.isAccepted()) {
            r.set("type", "ok");
            r.at("result")->set(r0.content());
        } else {
            r.set("type", "error");
            r.set("msg", r0.content().toString());
        }
        sp->async_send(J(r));
    }

    // --- JSON protocol handlers ---

    template<typename SP>
    void jsonMessage(SP& sp, uint64_t sid, const std::string& msg) {
        // Parse via schema
        Node req("req");
        if (!schema::importAs<schema::JsonS>(&req, msg)) {
            Node r("r"); r.set("type", "error"); r.set("msg", "invalid json");
            sp->async_send(J(r));
            return;
        }

        std::string cmd = req.get("cmd").toString();
        std::string path = req.get("path").toString();

        if (cmd == "subscribe") {
            if (subscribeSvc) subscribeSvc->subscribe(sid, path);
            Node r("r"); r.set("type", "subscribed"); r.set("path", path);
            if (!req.get("id").isNull()) r.at("id")->set(req.get("id"));
            sp->async_send(J(r));
        }
        else if (cmd == "unsubscribe") {
            if (subscribeSvc) subscribeSvc->unsubscribe(sid, path);
            Node r("r"); r.set("type", "unsubscribed"); r.set("path", path);
            if (!req.get("id").isNull()) r.at("id")->set(req.get("id"));
            sp->async_send(J(r));
        }
        else if (cmd == "command.run") {
            jsonCommandRun(sp, req);
        }
        else {
            // Reuse shared handler (get/set/list/...; not command.run — handled above for WS)
            std::string response = handleNodeJsonCmd(root, &req);
            sp->async_send(std::move(response));
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
    _p->subscribeSvc = std::make_unique<SubscribeService>(_p->root);
    _p->subscribeSvc->setPushCallback(
        [this](uint64_t sessionId, const std::string& path, const Var& value) {
            Node evt("e");
            if (_p->isTextSession(sessionId)) {
                evt.set("key", path);
                evt.at("data")->set(value);
            } else {
                evt.set("type", "event");
                evt.set("path", path);
                evt.at("value")->set(value);
            }
            std::string pushMsg = J(evt);

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
                Node r("r"); r.set("requestId", rid); r.set("error", "unknown function: " + m.fn);
                session_ptr->async_send(J(r));
            }
        }
    });

    _p->server.bind_disconnect([this](auto& session_ptr) {
        auto sid = static_cast<uint64_t>(session_ptr->hash_key());
        if (_p->subscribeSvc) {
            _p->subscribeSvc->removeSession(sid);
        }
        {
            std::lock_guard<std::mutex> lock(_p->mtx);
            _p->sessions.erase(sid);
        }
        _p->connCount.fetch_sub(1, std::memory_order_relaxed);
    });

    return startServerWithPortFallback(_p->server, "NodeWsServer", _p->port);
}

void NodeWsServer::stop()
{
    _p->server.stop();
    if (_p->subscribeSvc) {
        _p->subscribeSvc->stop();
        _p->subscribeSvc.reset();
    }
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
