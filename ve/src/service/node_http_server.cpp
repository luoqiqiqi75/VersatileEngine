// node_http_service.cpp — ve::service::NodeHttpServer
#include "ve/service/node_service.h"
#include "ve/core/node.h"
#include "ve/core/var.h"
#include "ve/core/command.h"
#include "ve/core/pipeline.h"
#include "ve/core/schema.h"
#include "ve/core/log.h"
#include "server_util.h"

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <asio2/http/http_server.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <algorithm>
#include <string>
#include <string_view>

namespace ve {
namespace service {

// ============================================================================
// Helpers
// ============================================================================

static std::string strip_prefix(std::string_view path, std::string_view prefix)
{
    if (path.size() >= prefix.size() && path.substr(0, prefix.size()) == prefix)
        path.remove_prefix(prefix.size());
    while (!path.empty() && path.front() == '/') path.remove_prefix(1);
    return std::string(path);
}

// ============================================================================
// Schema-based JSON response helpers
// ============================================================================

static const schema::ExportOptions compactJson{0};

static std::string J(Node& n)
{
    return schema::exportAs<schema::JsonS>(&n, compactJson);
}

static std::string jsonOk(const std::string& path)
{
    Node r("r");
    r.set("ok", true);
    r.set("path", path);
    return J(r);
}

static std::string jsonError(const std::string& msg)
{
    Node r("r");
    r.set("error", msg);
    return J(r);
}

// ============================================================================
// JSON-RPC 2.0 Error Codes
// ============================================================================

enum JsonRpcError
{
    JRpcParseError     = -32700,
    JRpcInvalidRequest = -32600,
    JRpcMethodNotFound = -32601,
    JRpcInvalidParams  = -32602,
    JRpcInternalError  = -32603
};

// ============================================================================
// Private
// ============================================================================

struct NodeHttpServer::Private
{
    Node*    root = nullptr;
    uint16_t port = 12000;
    asio2::http_server server;
    std::chrono::steady_clock::time_point startTime;

    // JSON-RPC 2.0
    std::string handleJsonRpc(const std::string& requestJson);
    std::string jrpcResponse(const Var& id, const Var& result);
    std::string jrpcError(const Var& id, int code, const std::string& message);
};

// ============================================================================
// NodeHttpServer
// ============================================================================

NodeHttpServer::NodeHttpServer(Node* root, uint16_t port)
    : _p(std::make_unique<Private>())
{
    _p->root = root;
    _p->port = port;
}

NodeHttpServer::~NodeHttpServer()
{
    stop();
}

bool NodeHttpServer::start()
{
    _p->startTime = std::chrono::steady_clock::now();

    // GET /health
    _p->server.bind<http::verb::get>("/health",
        [this](http::web_request& req, http::web_response& rep) {
            auto elapsed = std::chrono::steady_clock::now() - _p->startTime;
            auto sec = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
            Node r("r");
            r.set("status", "ok");
            r.set("uptime_s", static_cast<int64_t>(sec));
            rep.fill_json(J(r), http::status::ok);
        });

    // POST /jsonrpc
    _p->server.bind<http::verb::post>("/jsonrpc",
        [this](http::web_request& req, http::web_response& rep) {
            std::string response = _p->handleJsonRpc(std::string(req.body()));
            rep.fill_json(std::move(response), http::status::ok);
        });

    // GET /api/node/* — get node as tree (value if leaf, subtree if has children)
    _p->server.bind<http::verb::get>("/api/node/*",
        [this](http::web_request& req, http::web_response& rep) {
            auto nodePath = strip_prefix(req.path(), "/api/node");
            Node* target = nodePath.empty() ? _p->root : _p->root->find(nodePath);
            if (!target) {
                rep.fill_json(jsonError("not found"), http::status::not_found);
                return;
            }
            Node r("r");
            r.set("path", target->path(_p->root));
            r.at("value")->copy(target);
            rep.fill_json(J(r), http::status::ok);
        });

    // PUT /api/node/* — import subtree
    _p->server.bind<http::verb::put>("/api/node/*",
        [this](http::web_request& req, http::web_response& rep) {
            auto nodePath = strip_prefix(req.path(), "/api/node");
            if (nodePath.empty()) {
                rep.fill_json(jsonError("path required"), http::status::bad_request);
                return;
            }
            Node* target = _p->root->at(nodePath);
            if (!target) {
                rep.fill_json(jsonError("cannot create node"), http::status::internal_server_error);
                return;
            }
            std::string body(req.body());
            if (!body.empty()) {
                schema::importAs<schema::JsonS>(target, body);
            }
            rep.fill_json(jsonOk(target->path(_p->root)), http::status::ok);
        });

    // POST /api/node/*
    _p->server.bind<http::verb::post>("/api/node/*",
        [this](http::web_request& req, http::web_response& rep) {
            auto nodePath = strip_prefix(req.path(), "/api/node");
            if (nodePath.empty()) {
                rep.fill_json(jsonError("path required"), http::status::bad_request);
                return;
            }
            Node* target = _p->root->at(nodePath);
            if (!target) {
                rep.fill_json(jsonError("not found"), http::status::not_found);
                return;
            }
            std::string body(req.body());
            if (body.empty()) {
                // Empty body = trigger
                target->trigger<Node::NODE_CHANGED>();
                if (target->isWatching()) target->activate(Node::NODE_CHANGED, target);
            } else {
                // body != empty = set (including "null" or "{}" for clear)
                Node tmp("tmp");
                if (schema::importAs<schema::JsonS>(&tmp, body)) {
                    if (tmp.count() > 0) {
                        target->copy(&tmp);
                    } else {
                        target->set(tmp.get());
                    }
                }
            }
            rep.fill_json(jsonOk(target->path(_p->root)), http::status::ok);
        });

    // GET /api/tree/*
    _p->server.bind<http::verb::get>("/api/tree/*",
        [this](http::web_request& req, http::web_response& rep) {
            auto nodePath = strip_prefix(req.path(), "/api/tree");
            Node* target = nodePath.empty() ? _p->root : _p->root->at(nodePath);
            if (!target) {
                rep.fill_json(jsonError("not found"), http::status::not_found);
                return;
            }
            rep.fill_json(schema::exportAs<schema::JsonS>(target, compactJson), http::status::ok);
        });

    // POST /api/tree/*
    _p->server.bind<http::verb::post>("/api/tree/*",
        [this](http::web_request& req, http::web_response& rep) {
            auto nodePath = strip_prefix(req.path(), "/api/tree");
            Node* target = nodePath.empty() ? _p->root : _p->root->at(nodePath);
            if (!target) {
                rep.fill_json(jsonError("cannot create node"), http::status::internal_server_error);
                return;
            }
            std::string body(req.body());
            if (body.empty()) {
                rep.fill_json(jsonError("empty body"), http::status::bad_request);
                return;
            }
            if (schema::importAs<schema::JsonS>(target, body)) {
                rep.fill_json(jsonOk(target->path(_p->root)), http::status::ok);
            } else {
                rep.fill_json(jsonError("invalid JSON"), http::status::bad_request);
            }
        });

    // GET /api/cmd
    _p->server.bind<http::verb::get>("/api/cmd",
        [this](http::web_request& req, http::web_response& rep) {
            auto cmds = command::keys();
            std::sort(cmds.begin(), cmds.end());
            cmds.erase(std::unique(cmds.begin(), cmds.end()), cmds.end());

            Node r("r");
            for (size_t i = 0; i < cmds.size(); ++i) {
                Node* item = r.at("commands")->append("");
                item->set("name", cmds[i]);
                item->set("help", command::help(cmds[i]));
            }
            rep.fill_json(J(r), http::status::ok);
        });

    // POST /api/cmd/*
    _p->server.bind<http::verb::post>("/api/cmd/*",
        [this](http::web_request& req, http::web_response& rep) {
            Node reqNode("req");
            std::string bodyStr(req.body());
            if (!bodyStr.empty()) {
                schema::importAs<schema::JsonS>(&reqNode, bodyStr);
            }

            std::string cmdKey = strip_prefix(req.path(), "/api/cmd");
            if (cmdKey.empty() || cmdKey.front() == '{') {
                cmdKey = reqNode.get("cmd").toString();
            }
            if (cmdKey.empty()) {
                rep.fill_json(jsonError("command key required"), http::status::bad_request);
                return;
            }
            if (!command::has(cmdKey)) {
                rep.fill_json(jsonError("unknown command: " + cmdKey), http::status::not_found);
                return;
            }

            Var cmdBody;
            if (reqNode.find("body")) {
                cmdBody = schema::exportAs<schema::VarS>(reqNode.find("body"));
            } else if (reqNode.find("args")) {
                cmdBody = schema::exportAs<schema::VarS>(reqNode.find("args"));
            } else {
                cmdBody = schema::exportAs<schema::VarS>(&reqNode);
            }

            const bool waitCmd = reqNode.get("wait").toBool(false);

            Node*     cmdCtx = command::context(cmdKey);
            command::parseArgs(cmdCtx, cmdBody);
            Pipeline* detached = nullptr;
            Result    result   = command::call(cmdKey, cmdCtx, waitCmd, waitCmd ? nullptr : &detached);

            if (detached) {
                detached->setResultHandler([cmdCtx, detached](const Result&) {
                    delete detached;
                    delete cmdCtx;
                });
                Node r("r");
                r.set("ok", true);
                r.set("accepted", true);
                if (!reqNode.get("id").isNull()) {
                    r.at("id")->set(reqNode.get("id"));
                }
                rep.fill_json(J(r), http::status::accepted);
                return;
            }

            delete cmdCtx;

            Node r("r");
            if (result.isSuccess() || result.isAccepted()) {
                r.set("ok", true);
                r.at("result")->set(result.content());
            } else {
                r.set("error", Var(result).toString());
            }
            if (!reqNode.get("id").isNull()) {
                r.at("id")->set(reqNode.get("id"));
            }
            rep.fill_json(J(r), result.isSuccess() || result.isAccepted()
                ? http::status::ok : http::status::internal_server_error);
        });

    // GET /api/children/*
    _p->server.bind<http::verb::get>("/api/children/*",
        [this](http::web_request& req, http::web_response& rep) {
            auto nodePath = strip_prefix(req.path(), "/api/children");
            Node* target = nodePath.empty() ? _p->root : _p->root->at(nodePath);
            if (!target) {
                rep.fill_json(jsonError("not found"), http::status::not_found);
                return;
            }
            Node r("r");
            for (int i = 0; i < target->count(); ++i) {
                r.append("")->set(target->child(i)->name());
            }
            rep.fill_json(schema::exportAs<schema::JsonS>(&r, compactJson), http::status::ok);
        });

    _p->server.bind_not_found(
        [](http::web_request&, http::web_response& rep) {
            rep.fill_json("{\"error\":\"not found\"}", http::status::not_found);
        });

    ve::service::disableWindowsPortReuse(_p->server);
    return _p->server.start("0.0.0.0", _p->port);
}

void NodeHttpServer::stop()
{
    _p->server.stop();
}

bool NodeHttpServer::isRunning() const
{
    return _p->server.is_started();
}

// ============================================================================
// JSON-RPC 2.0 Implementation
// ============================================================================

std::string NodeHttpServer::Private::handleJsonRpc(const std::string& requestJson)
{
    try {
        Node req("req");
        if (!schema::importAs<schema::JsonS>(&req, requestJson)) {
            return jrpcError(Var(), JRpcParseError, "Parse error");
        }

        Var jsonrpc = req.get("jsonrpc");
        if (jsonrpc.toString() != "2.0") {
            return jrpcError(Var(), JRpcInvalidRequest, "Invalid jsonrpc version");
        }

        Var id = req.get("id");
        std::string method = req.get("method").toString();
        if (method.empty()) {
            return jrpcError(id, JRpcInvalidRequest, "Missing or invalid method");
        }

        Node* paramsNode = req.find("params");

        if (method == "node.get") {
            std::string path = paramsNode ? paramsNode->get("path").toString() : "";
            Node* node = root->find(path);

            Node r("r");
            if (!node) {
                r.set("found", false);
            } else {
                r.set("found", true);
                r.at("value")->set(node->get());
                r.set("path", node->path());
            }
            return jrpcResponse(id, schema::exportAs<schema::VarS>(&r));
        }
        else if (method == "node.set") {
            std::string path = paramsNode ? paramsNode->get("path").toString() : "";
            Var value = paramsNode ? paramsNode->get("value") : Var();

            Node* node = root->find(path);
            if (!node) node = root->at(path);
            if (node) node->set(value);

            Node r("r");
            r.set("success", node != nullptr);
            if (node) r.set("path", node->path());
            return jrpcResponse(id, schema::exportAs<schema::VarS>(&r));
        }
        else if (method == "node.list") {
            std::string path = paramsNode ? paramsNode->get("path").toString() : "";
            Node* node = root->find(path);

            Node r("r");
            if (!node) {
                r.set("found", false);
            } else {
                r.set("found", true);
                r.set("path", node->path());
                for (auto* child : node->children()) {
                    Node* item = r.at("children")->append("");
                    item->set("name", child->name());
                    item->set("path", child->path());
                    item->set("hasValue", !child->get().isNull());
                    item->set("childCount", static_cast<int64_t>(child->children().size()));
                }
            }
            return jrpcResponse(id, schema::exportAs<schema::VarS>(&r));
        }
        else if (method == "command.run") {
            std::string name = paramsNode ? paramsNode->get("name").toString() : "";
            Var args = paramsNode ? paramsNode->get("args") : Var(Var::DictV{});

            Var cmdResult = command::call(name, args);

            Node r("r");
            r.at("result")->set(cmdResult);
            return jrpcResponse(id, schema::exportAs<schema::VarS>(&r));
        }

        return jrpcError(id, JRpcMethodNotFound, "Method not found: " + method);
    }
    catch (const std::exception& e) {
        return jrpcError(Var(), JRpcInternalError, e.what());
    }
}

std::string NodeHttpServer::Private::jrpcResponse(const Var& id, const Var& result)
{
    Node r("r");
    r.set("jsonrpc", "2.0");
    r.at("result")->set(result);
    if (!id.isNull()) r.at("id")->set(id);
    return J(r);
}

std::string NodeHttpServer::Private::jrpcError(const Var& id, int code, const std::string& message)
{
    Node r("r");
    r.set("jsonrpc", "2.0");
    r.at("error")->set("code", static_cast<int64_t>(code));
    r.at("error")->set("message", message);
    if (!id.isNull()) {
        r.at("id")->set(id);
    } else {
        r.at("id")->set(Var());
    }
    return J(r);
}

} // namespace service
} // namespace ve
