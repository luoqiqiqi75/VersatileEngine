// node_http_service.cpp — ve::service::NodeHttpServer
#include "ve/service/node_service.h"
#include "ve/core/command.h"
#include "ve/core/node.h"
#include "ve/core/schema.h"
#include "ve/core/impl/json.h"
#include "server_util.h"
#include "subscribe_service.h"
#include "node_protocol.h"
#include "node_task_service.h"

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <asio2/http/http_server.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <chrono>
#include <memory>
#include <string>
#include <string_view>

namespace ve {
namespace service {

static const schema::ExportOptions compactJson{0};

enum JsonRpcError
{
    JRpcParseError     = -32700,
    JRpcInvalidRequest = -32600,
    JRpcMethodNotFound = -32601,
    JRpcInvalidParams  = -32602,
    JRpcInternalError  = -32603,
    JRpcServerError    = -32000
};

static std::string toJson(Node& n)
{
    return schema::exportAs<schema::JsonS>(&n, compactJson);
}

static void fillError(Node* rep, const std::string& code, const std::string& error, const Var& id = {})
{
    rep->clear();
    rep->set(Var());
    rep->set("ok", false);
    if (!id.isNull()) {
        rep->at("id")->set(id);
    }
    rep->set("code", code);
    rep->set("error", error);
}

static std::string stripPrefix(std::string_view path, std::string_view prefix)
{
    if (path.size() >= prefix.size() && path.substr(0, prefix.size()) == prefix) {
        path.remove_prefix(prefix.size());
    }
    while (!path.empty() && path.front() == '/') {
        path.remove_prefix(1);
    }
    return std::string(path);
}

static std::string getQueryParam(std::string_view query, std::string_view key)
{
    size_t pos = 0;
    while (pos < query.size()) {
        size_t eq = query.find('=', pos);
        size_t amp = query.find('&', pos);
        if (amp == std::string_view::npos) {
            amp = query.size();
        }
        if (eq == std::string_view::npos || eq > amp) {
            if (query.substr(pos, amp - pos) == key) {
                return "1";
            }
        } else if (query.substr(pos, eq - pos) == key) {
            return std::string(query.substr(eq + 1, amp - eq - 1));
        }
        pos = amp + 1;
    }
    return {};
}

static bool queryBool(std::string_view query, std::string_view key, bool def = false)
{
    std::string value = getQueryParam(query, key);
    if (value.empty()) {
        return def;
    }
    if (value == "0" || value == "false" || value == "False" || value == "FALSE"
        || value == "no" || value == "No" || value == "NO") {
        return false;
    }
    return true;
}

static int queryInt(std::string_view query, std::string_view key, int def)
{
    std::string value = getQueryParam(query, key);
    if (value.empty()) {
        return def;
    }
    try {
        return std::stoi(value);
    } catch (...) {
        return def;
    }
}

static void fillMeta(Node* out, Node* root, SubscribeService* subscribe, Node* target)
{
    out->set("path", target->path(root));
    out->set("type", static_cast<int64_t>(target->get().type()));
    out->set("child_count", static_cast<int64_t>(target->count()));
    out->set("has_shadow", target->shadow() != nullptr);
    out->set("subscribers", static_cast<int64_t>(
        subscribe ? subscribe->getSubscriberCount(target->path(root)) : 0));
    if (target->parent()) {
        out->set("parent_path", target->parent()->path(root));
    }
}

static Var stripValues(const Var& value)
{
    if (value.isDict()) {
        Var::DictV out;
        for (const auto& [key, child] : value.toDict()) {
            if (key == "_value") {
                continue;
            }
            out[key] = stripValues(child);
        }
        return Var(std::move(out));
    }
    if (value.isList()) {
        Var::ListV out;
        out.reserve(value.toList().size());
        for (const auto& child : value.toList()) {
            out.push_back(stripValues(child));
        }
        return Var(std::move(out));
    }
    return Var();
}

static http::status statusFromReply(Node* reply)
{
    if (reply->get("ok").toBool(false)) {
        return reply->get("accepted").toBool(false) ? http::status::accepted : http::status::ok;
    }
    std::string code = reply->get("code").toString();
    if (code == "not_found") {
        return http::status::not_found;
    }
    if (code == "invalid_request" || code == "invalid_params") {
        return http::status::bad_request;
    }
    if (code == "unsupported") {
        return http::status::method_not_allowed;
    }
    return http::status::internal_server_error;
}

static int jsonRpcErrorCode(Node* reply)
{
    std::string code = reply->get("code").toString();
    if (code == "unknown_op") {
        return JRpcMethodNotFound;
    }
    if (code == "invalid_request") {
        return JRpcInvalidRequest;
    }
    if (code == "invalid_params" || code == "unsupported") {
        return JRpcInvalidParams;
    }
    return JRpcServerError;
}

struct NodeHttpServer::Private
{
    Node*    root = nullptr;
    Node*    config = nullptr;
    uint16_t port = 12000;
    asio2::http_server server;
    std::chrono::steady_clock::time_point startTime;
    std::unique_ptr<SubscribeService> subscribeSvc;
    std::unique_ptr<NodeTaskService> taskSvc;

    std::string handleJsonRpc(const std::string& requestJson) const
    {
        Node req("req");
        if (!schema::importAs<schema::JsonS>(&req, requestJson)) {
            Node err("r");
            err.set("jsonrpc", "2.0");
            err.at("error")->set("code", static_cast<int64_t>(JRpcParseError));
            err.at("error")->set("message", "Parse error");
            err.at("id")->set(Var());
            return toJson(err);
        }

        Var jsonrpc = req.get("jsonrpc");
        Var id = req.get("id");
        if (jsonrpc.toString() != "2.0") {
            Node err("r");
            err.set("jsonrpc", "2.0");
            err.at("error")->set("code", static_cast<int64_t>(JRpcInvalidRequest));
            err.at("error")->set("message", "Invalid jsonrpc version");
            err.at("id")->set(id.isNull() ? Var() : id);
            return toJson(err);
        }

        std::string method = req.get("method").toString();
        if (method.empty()) {
            Node err("r");
            err.set("jsonrpc", "2.0");
            err.at("error")->set("code", static_cast<int64_t>(JRpcInvalidRequest));
            err.at("error")->set("message", "Missing or invalid method");
            err.at("id")->set(id.isNull() ? Var() : id);
            return toJson(err);
        }

        Node protocolReq("req");
        if (!id.isNull()) {
            protocolReq.at("id")->set(id);
        }
        if (Node* params = req.find("params")) {
            protocolReq.copy(params, true, true, true);
        }
        protocolReq.set("op", method);

        Node protocolRep("rep");
        dispatchNodeProtocol(root, &protocolReq, &protocolRep,
                             subscribeSvc.get(), taskSvc.get(),
                             config ? config->get("batch_limit").toInt(500) : 500);

        Node out("r");
        out.set("jsonrpc", "2.0");
        if (protocolRep.get("ok").toBool(false)) {
            if (protocolRep.get("accepted").toBool(false)) {
                out.at("result")->set("accepted", true);
                out.at("result")->set("task_id", protocolRep.get("task_id"));
            } else {
                if (Node* data = protocolRep.find("data")) {
                    out.at("result")->copy(data, true, true, true);
                } else {
                    out.at("result")->set(Var());
                }
            }
        } else {
            out.at("error")->set("code", static_cast<int64_t>(jsonRpcErrorCode(&protocolRep)));
            out.at("error")->set("message", protocolRep.get("error").toString());
        }
        out.at("id")->set(id.isNull() ? Var() : id);
        return toJson(out);
    }
};

NodeHttpServer::NodeHttpServer(Node* root, uint16_t port)
    : _p(std::make_unique<Private>())
{
    _p->root = root;
    _p->port = port;

    Node* serverNode = root ? root->find("ve/server/node/http") : nullptr;
    if (serverNode) {
        _p->config = serverNode->find("config");
        if (!_p->config) {
            _p->config = serverNode->at("config");
        }
    }
    if (_p->config && _p->config->find("batch_limit") == nullptr) {
        _p->config->set("batch_limit", 500);
    }
}

NodeHttpServer::~NodeHttpServer()
{
    stop();
}

bool NodeHttpServer::start()
{
    _p->startTime = std::chrono::steady_clock::now();
    _p->subscribeSvc = std::make_unique<SubscribeService>(_p->root);
    _p->subscribeSvc->start();
    _p->taskSvc = std::make_unique<NodeTaskService>(_p->root);

    _p->server.bind<http::verb::get>("/health",
        [this](http::web_request&, http::web_response& rep) {
            auto elapsed = std::chrono::steady_clock::now() - _p->startTime;
            auto seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
            Node out("r");
            out.set("status", "ok");
            out.set("uptime_s", static_cast<int64_t>(seconds));
            rep.fill_json(toJson(out), http::status::ok);
        });

    _p->server.bind<http::verb::post>("/ve",
        [this](http::web_request& req, http::web_response& rep) {
            Node protocolReq("req");
            if (!schema::importAs<schema::JsonS>(&protocolReq, std::string(req.body()))) {
                Node protocolRep("rep");
                fillError(&protocolRep, "invalid_request", "invalid JSON request");
                rep.fill_json(toJson(protocolRep), http::status::bad_request);
                return;
            }
            Node protocolRep("rep");
            dispatchNodeProtocol(_p->root, &protocolReq, &protocolRep,
                                 _p->subscribeSvc.get(), _p->taskSvc.get(),
                                 _p->config ? _p->config->get("batch_limit").toInt(500) : 500);
            rep.fill_json(toJson(protocolRep), statusFromReply(&protocolRep));
        });

    auto bindAtGet = [this](const std::string& nodePath, http::web_request& req, http::web_response& rep) {
        Node* target = ve::n(nodePath, false);
        const bool autoIgnore = queryBool(req.query(), "auto_ignore", true);
        const bool wantChildren = queryBool(req.query(), "children", false);
        const bool wantMeta = queryBool(req.query(), "meta", false);
        const bool wantStructure = queryBool(req.query(), "structure", false);
        const int depth = queryInt(req.query(), "depth", -1);

        if (!target) {
            if (wantChildren) {
                rep.fill_json("[]", http::status::ok);
            } else {
                rep.fill_json("null", http::status::ok);
            }
            return;
        }

        if (wantChildren) {
            if (wantMeta) {
                Node out("r");
                fillMeta(out.at("meta"), _p->root, _p->subscribeSvc.get(), target);
                Node* children = out.at("children");
                for (auto* child : target->children()) {
                    children->append()->set(target->keyOf(child));
                }
                rep.fill_json(toJson(out), http::status::ok);
                return;
            }

            Node out("r");
            for (auto* child : target->children()) {
                out.append()->set(target->keyOf(child));
            }
            rep.fill_json(toJson(out), http::status::ok);
            return;
        }

        if (wantMeta) {
            Node out("r");
            fillMeta(&out, _p->root, _p->subscribeSvc.get(), target);
            rep.fill_json(toJson(out), http::status::ok);
            return;
        }

        if (wantStructure) {
            schema::ExportOptions options = compactJson;
            options.auto_ignore = autoIgnore;
            Var base = (depth >= 0)
                ? impl::json::parse(impl::json::exportTree(target, depth, options))
                : schema::exportAs<schema::VarS>(target, options);
            Var out = stripValues(base);
            rep.fill_json(impl::json::stringify(out), http::status::ok);
            return;
        }

        schema::ExportOptions options = compactJson;
        options.auto_ignore = autoIgnore;
        if (depth >= 0) {
            rep.fill_json(impl::json::exportTree(target, depth, options), http::status::ok);
        } else {
            rep.fill_json(schema::exportAs<schema::JsonS>(target, options), http::status::ok);
        }
    };

    auto bindAtPut = [this](const std::string& nodePath, http::web_request& req, http::web_response& rep) {
        std::string body(req.body());
        if (body.empty()) {
            Node protocolRep("rep");
            fillError(&protocolRep, "invalid_params", "request body is required");
            rep.fill_json(toJson(protocolRep), http::status::bad_request);
            return;
        }

        Node* target = ve::n(nodePath);
        schema::ImportOptions options;
        options.auto_insert = queryBool(req.query(), "auto_insert", true);
        options.auto_remove = queryBool(req.query(), "auto_remove", false);
        options.auto_update = queryBool(req.query(), "auto_update", false);
        if (!schema::importAs<schema::JsonS>(target, body, options)) {
            Node protocolRep("rep");
            fillError(&protocolRep, "invalid_request", "invalid JSON body");
            rep.fill_json(toJson(protocolRep), http::status::bad_request);
            return;
        }

        Node out("r");
        out.set("ok", true);
        out.set("path", target->path(_p->root));
        rep.fill_json(toJson(out), http::status::ok);
    };

    auto bindAtPost = [this](const std::string& nodePath, http::web_request& req, http::web_response& rep) {
        const bool trigger = queryBool(req.query(), "trigger", false);
        std::string body(req.body());
        Node* target = trigger || body.empty() ? ve::n(nodePath, false) : ve::n(nodePath);

        if (!target) {
            Node protocolRep("rep");
            fillError(&protocolRep, "not_found", "node not found");
            rep.fill_json(toJson(protocolRep), http::status::not_found);
            return;
        }

        if (trigger || body.empty()) {
            target->trigger<Node::NODE_CHANGED>();
            if (target->isWatching()) {
                target->activate(Node::NODE_CHANGED, target);
            }
        } else {
            target->set(impl::json::parse(body));
        }

        Node out("r");
        out.set("ok", true);
        out.set("path", target->path(_p->root));
        rep.fill_json(toJson(out), http::status::ok);
    };

    auto bindAtDelete = [this](const std::string& nodePath, http::web_response& rep) {
        if (nodePath.empty()) {
            Node reply("rep");
            fillError(&reply, "invalid_params", "cannot remove root");
            rep.fill_json(toJson(reply), http::status::bad_request);
            return;
        }
        if (!_p->root->erase(nodePath)) {
            Node reply("rep");
            fillError(&reply, "not_found", "node not found");
            rep.fill_json(toJson(reply), http::status::not_found);
            return;
        }
        Node out("r");
        out.set("ok", true);
        out.set("path", nodePath);
        rep.fill_json(toJson(out), http::status::ok);
    };

    auto bindCmdPost = [this](const std::string& rawCmdKey, http::web_request& req, http::web_response& rep) {
        std::string cmdKey = rawCmdKey;
        if (cmdKey.empty()) {
            Node reply("rep");
            fillError(&reply, "invalid_params", "command key required");
            rep.fill_json(toJson(reply), http::status::bad_request);
            return;
        }
        if (!command::has(cmdKey)) {
            Node reply("rep");
            fillError(&reply, "not_found", "unknown command: " + cmdKey);
            rep.fill_json(toJson(reply), http::status::not_found);
            return;
        }

        Node* current = nullptr;
        std::string contextPath = getQueryParam(req.query(), "context");
        if (!contextPath.empty()) {
            current = ve::n(contextPath, false);
            if (!current) {
                Node reply("rep");
                fillError(&reply, "not_found", "context not found: " + contextPath);
                rep.fill_json(toJson(reply), http::status::not_found);
                return;
            }
        }

        Node* ctx = command::context(cmdKey, current);
        std::string body(req.body());
        if (!body.empty()) {
            Node input("input");
            if (!schema::importAs<schema::JsonS>(&input, body)) {
                delete ctx;
                Node reply("rep");
                fillError(&reply, "invalid_request", "invalid JSON body");
                rep.fill_json(toJson(reply), http::status::bad_request);
                return;
            }
            command::parseArgs(ctx, schema::exportAs<schema::VarS>(&input));
        }

        const bool async = queryBool(req.query(), "async", false);
        Pipeline* detached = nullptr;
        Result result = command::call(cmdKey, ctx, !async, async ? &detached : nullptr);
        if (detached) {
            if (!_p->taskSvc) {
                delete detached;
                delete ctx;
                Node reply("rep");
                fillError(&reply, "internal_error", "task service unavailable");
                rep.fill_json(toJson(reply), http::status::internal_server_error);
                return;
            }

            std::string taskId = _p->taskSvc->attach(cmdKey, Var(), ctx, detached, {});
            if (taskId.empty()) {
                Node reply("rep");
                fillError(&reply, "internal_error", "failed to start task");
                rep.fill_json(toJson(reply), http::status::internal_server_error);
            } else {
                Node out("r");
                out.set("ok", true);
                out.set("accepted", true);
                out.set("task_id", taskId);
                rep.fill_json(toJson(out), http::status::accepted);
            }
            return;
        }

        delete ctx;
        if (result.isSuccess() || result.isAccepted()) {
            Node out("r");
            out.set("ok", true);
            out.at("result")->set(result.content());
            rep.fill_json(toJson(out), http::status::ok);
        } else {
            Node reply("rep");
            fillError(&reply, "command_failed", result.content().toString());
            rep.fill_json(toJson(reply), http::status::internal_server_error);
        }
    };

    _p->server.bind<http::verb::get>("/at",
        [bindAtGet](http::web_request& req, http::web_response& rep) {
            bindAtGet("", req, rep);
        });
    _p->server.bind<http::verb::get>("/at/*",
        [bindAtGet](http::web_request& req, http::web_response& rep) {
            bindAtGet(stripPrefix(req.path(), "/at"), req, rep);
        });
    _p->server.bind<http::verb::post>("/at",
        [bindAtPost](http::web_request& req, http::web_response& rep) {
            bindAtPost("", req, rep);
        });
    _p->server.bind<http::verb::post>("/at/*",
        [bindAtPost](http::web_request& req, http::web_response& rep) {
            bindAtPost(stripPrefix(req.path(), "/at"), req, rep);
        });
    _p->server.bind<http::verb::put>("/at",
        [bindAtPut](http::web_request& req, http::web_response& rep) {
            bindAtPut("", req, rep);
        });
    _p->server.bind<http::verb::put>("/at/*",
        [bindAtPut](http::web_request& req, http::web_response& rep) {
            bindAtPut(stripPrefix(req.path(), "/at"), req, rep);
        });
    _p->server.bind<http::verb::delete_>("/at/*",
        [bindAtDelete](http::web_request& req, http::web_response& rep) {
            bindAtDelete(stripPrefix(req.path(), "/at"), rep);
        });
    _p->server.bind<http::verb::post>("/cmd/*",
        [bindCmdPost](http::web_request& req, http::web_response& rep) {
            bindCmdPost(stripPrefix(req.path(), "/cmd"), req, rep);
        });

    _p->server.bind<http::verb::post>("/jsonrpc",
        [this](http::web_request& req, http::web_response& rep) {
            rep.fill_json(_p->handleJsonRpc(std::string(req.body())), http::status::ok);
        });

    _p->server.bind_not_found([](http::web_request&, http::web_response& rep) {
        Node reply("rep");
        fillError(&reply, "not_found", "not found");
        rep.fill_json(toJson(reply), http::status::not_found);
    });

    ve::service::disableWindowsPortReuse(_p->server);
    return _p->server.start("0.0.0.0", _p->port);
}

void NodeHttpServer::stop()
{
    _p->server.stop();
    if (_p->subscribeSvc) {
        _p->subscribeSvc->stop();
        _p->subscribeSvc.reset();
    }
    _p->taskSvc.reset();
}

bool NodeHttpServer::isRunning() const
{
    return _p->server.is_started();
}

} // namespace service
} // namespace ve
