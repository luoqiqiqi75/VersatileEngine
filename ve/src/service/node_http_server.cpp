// node_http_service.cpp — ve::service::NodeHttpServer
#include "ve/service/node_service.h"
#include "ve/core/command.h"
#include "ve/core/node.h"
#include "ve/core/pipeline.h"
#include "ve/core/schema.h"
#include "ve/core/impl/json.h"
#include "server_util.h"
#include "subscribe_service.h"
#include "node_task_service.h"

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <asio2/http/http_server.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <chrono>
#include <algorithm>
#include <memory>
#include <string>
#include <string_view>

namespace ve {

// defines
namespace service {

static const schema::ExportOptions compactJson{0};

// HTTP-specific key separator: ':' instead of '#' so that
// /at/leo/mu:1 is reachable from a browser without URL-encoding
// (URL '#' is the fragment delimiter, truncated by browsers).
static constexpr char HTTP_KEY_SEP = ':';

enum JsonRpcError
{
    JRpcParseError     = -32700,
    JRpcInvalidRequest = -32600,
    JRpcMethodNotFound = -32601,
    JRpcInvalidParams  = -32602,
    JRpcInternalError  = -32603,
    JRpcServerError    = -32000
};

}

namespace service {

static std::string toJson(Node& n)
{
    return schema::exportAs<schema::JsonS>(&n, compactJson);
}

static void okReply(Node* rep, const Var& id)
{
    rep->clear();
    rep->set("ok", true);
    if (!id.isNull()) {
        rep->at("id")->set(id);
    }
}

static void okReply(Node* rep, const Var& id, Node* data)
{
    okReply(rep, id);
    if (data) {
        rep->at("data")->copy(data, true, true, true);
    }
}

static void fillError(Node* rep, const std::string& code, const std::string& error)
{
    if (!rep) return;
    rep->clear();
    rep->set(Var());
    rep->set("ok", false);
    rep->set("code", code);
    rep->set("error", error);
}

static void errorReply(Node* rep, const Var& id, const std::string& code, const std::string& error)
{
    fillError(rep, code, error);
    if (!id.isNull()) {
        rep->at("id")->set(id);
    }
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
    if (reply && reply->get("ok").toBool(false)) {
        return reply->get("accepted").toBool(false) ? http::status::accepted : http::status::ok;
    }
    std::string code = reply ? reply->get("code").toString() : std::string{};
    if (code == "not_found") return http::status::not_found;
    if (code == "invalid_request" || code == "invalid_params") return http::status::bad_request;
    if (code == "unsupported") return http::status::method_not_allowed;
    return http::status::internal_server_error;
}

static int jsonRpcErrorCode(Node* reply)
{
    std::string code = reply->get("code").toString();
    if (code == "not_found") {
        return JRpcServerError;
    }
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

static int jsonRpcErrorCode(const std::string& code)
{
    Node reply("reply");
    reply.set("code", code);
    return jsonRpcErrorCode(&reply);
}

static Node* findNode(Node* root, Node* req)
{
    return root->find(req->get("path").toString());
}

static Node* ensureNode(Node* root, Node* req)
{
    return root->at(req->get("path").toString());
}

static void makeChildInfo(Node* out, Node* root, SubscribeService* subscribe, Node* child, bool withMeta)
{
    std::string childPath = child->path(root);
    out->set("name", child->name());
    out->set("path", childPath);
    out->set("has_value", !child->get().isNull());
    out->set("child_count", static_cast<int64_t>(child->count()));
    if (withMeta) {
        out->set("type", static_cast<int64_t>(child->get().type()));
        out->set("subscribers", static_cast<int64_t>(
            subscribe ? subscribe->getSubscriberCount(childPath) : 0));
    }
}

static Node* pointerValue(Node* in, const std::string& path)
{
    if (!in) return nullptr;
    if (auto* n = in->find(path)) {
        if (auto* p = n->get().toPointer()) {
            return static_cast<Node*>(p);
        }
    }
    return nullptr;
}

static void setHttpError(Node* ctx, const std::string& code,
                         http::status status, const std::string& message)
{
    if (!ctx) return;
    ctx->set("http/error/code", code);
    ctx->set("http/error/status", static_cast<int64_t>(status));
    ctx->set("http/error/message", message);
}

static Result httpRequestToInput(Node* ctx, Node* in, Node* out)
{
    if (!ctx || !in || !out) return Result::fail("http request input is null");

    out->clear();

    std::string body = in->get("body").toString();
    if (!body.empty()) {
        if (!schema::importAs<schema::JsonS>(out, body)) {
            setHttpError(ctx, "invalid_request", http::status::bad_request, "invalid JSON body");
            return Result::fail("invalid JSON body");
        }
    }

    Node* root = pointerValue(in, "root");
    if (root) {
        out->at("root", false)->set(Var::ptr(root));
        Node* current = root;
        std::string contextPath = getQueryParam(in->get("query").toString(), "context");
        if (!contextPath.empty()) {
            std::string rel = contextPath;
            if (!rel.empty() && rel.front() == '/') {
                rel.erase(rel.begin());
            }
            current = rel.empty() ? root : root->find(rel);
            if (!current) {
                setHttpError(ctx, "invalid_params", http::status::bad_request,
                             "context not found: " + contextPath);
                return Result::fail("context not found: " + contextPath);
            }
        }
        out->at("current", false)->set(Var::ptr(current));
    }

    return Result::ok();
}

static Result outputToHttpResponse(Node* ctx, Node* in, Node* outNode)
{
    if (!ctx) return Result::fail("http render ctx is null");
    auto* rep = static_cast<http::web_response*>(ctx->get("http/response").toPointer());
    if (!rep) return Result::fail("http response is null");

    Node out("r");
    if (auto* error = ctx->find("http/error")) {
        fillError(&out, error->get("code").toString("internal_error"),
                  error->get("message").toString("command failed"));
        rep->fill_json(toJson(out), static_cast<http::status>(
            error->get("status").toInt(static_cast<int>(http::status::internal_server_error))));
    } else {
        out.set("ok", true);
        if (ctx->get("http/accepted").toBool(false)) {
            out.set("accepted", true);
        }
        if (in) {
            out.at("result")->copy(in, true, true, true);
        } else {
            out.at("result")->set(Var());
        }
        rep->fill_json(toJson(out), out.get("accepted").toBool(false) ? http::status::accepted : http::status::ok);
    }
    if (outNode) {
        outNode->copy(&out, true, true, true);
    }
    ctx->set("http/responded", true);
    return Result::ok();
}

static void finishHttpPipeline(Pipeline& pipe)
{
    Node* ctx = pipe.context();
    if (!ctx || ctx->get("http/responded").toBool(false)) return;

    if (!ctx->find("http/error") && pipe.lastResult().isError()) {
        setHttpError(ctx, "command_failed", http::status::internal_server_error,
                     pipe.lastResult().message);
    }
    if (pipe.lastResult().isAccepted()) {
        ctx->set("http/accepted", true);
    }

    Node* output = ctx->find("output", false);
    if (!output) {
        output = ctx->find("reply", false);
    }
    outputToHttpResponse(ctx, output, ctx->at("http/render"));
}

static void prepareHttpCommandContext(Node* ctx,
                                      const std::string& cmdKey,
                                      http::web_request& req,
                                      http::web_response& rep,
                                      Node* root)
{
    ctx->clear();
    ctx->set("http/cmd", cmdKey);
    ctx->set("http/response", Var::ptr(&rep));
    Node* request = ctx->at("http/request");
    request->set("body", std::string(req.body()));
    request->set("query", std::string(req.query()));
    request->set("root", Var::ptr(root));
}

static std::string normalizeNodePath(std::string path)
{
    while (!path.empty() && path.front() == VE_NODE_PATH_SEP) {
        path.erase(path.begin());
    }
    return path;
}

static std::string normalizeHttpNodePath(std::string path)
{
    path = normalizeNodePath(std::move(path));
    std::replace(path.begin(), path.end(), HTTP_KEY_SEP, VE_NODE_KEY_SEP);
    return path;
}

static Var exportNodeTreeVar(Node* target, int depth, const schema::ExportOptions& options)
{
    if (depth < 0) {
        return schema::exportAs<schema::VarS>(target, options);
    }
    return impl::json::parse(impl::json::exportTree(target, depth, options));
}

static Node* commandTarget(Pipeline& pipe)
{
    Node* ctx = pipe.context();
    return ctx ? ctx->get("http/target").as<Node*>() : nullptr;
}

static void writeNodeMeta(Node* out, Node* root, SubscribeService* subscribe, Node* target)
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

static void writeNodeGetData(Node* data, Node* root, SubscribeService* subscribe,
                             Node* request, Node* target)
{
    data->set("path", target->path(root));
    data->at("value")->set(target->get());
    if (request && request->find("depth")) {
        data->at("tree")->set(exportNodeTreeVar(target, request->get("depth").toInt(-1), compactJson));
    }
    if (request && request->get("meta").toBool(false)) {
        writeNodeMeta(data->at("meta"), root, subscribe, target);
    }
}

// step1: ve request -> command input (current/path + flags + ptrs the builtin needs)
static Result veNodeGetRequestToInput(Node* ctx, Node* in, Node* out)
{
    auto* root = static_cast<Node*>(ctx->get("ve/root").toPointer());
    out->clear();
    out->set("current", Var::ptr(root));
    out->set("root", Var::ptr(root));
    if (auto* sub = ctx->get("ve/subscribe").toPointer()) out->set("subscribe", Var::ptr(sub));
    out->set("data", true);
    out->set("path", normalizeNodePath(in->get("path").toString()));
    if (in->find("depth")) out->at("depth")->set(in->get("depth"));
    if (in->get("meta").toBool(false)) out->set("meta", true);

    return Result::ok();
}

// step3: builtin already shaped ctx/output -> just envelope it
static void finishVeNodeGet(Pipeline& pipe, const Var& id, Node* rep)
{
    Node* ctx = pipe.context();
    if (commandTarget(pipe)) {
        okReply(rep, id, ctx ? ctx->find("output", false) : nullptr);
        return;
    }

    std::string error = pipe.lastResult().message;
    if (error.empty()) {
        error = "node.get failed";
    }
    errorReply(rep, id, "not_found", error);
}

static Result jsonRpcNodeGetRequestToInput(Node* ctx, Node* in, Node* out)
{
    auto* root = static_cast<Node*>(ctx->get("jsonrpc/root").toPointer());
    out->clear();
    out->set("current", Var::ptr(root));
    out->set("root", Var::ptr(root));
    if (auto* sub = ctx->get("jsonrpc/subscribe").toPointer()) out->set("subscribe", Var::ptr(sub));
    out->set("data", true);
    out->set("path", normalizeNodePath(in->get("path").toString()));
    if (in->find("depth")) out->at("depth")->set(in->get("depth"));
    if (in->get("meta").toBool(false)) out->set("meta", true);

    return Result::ok();
}

static void finishJsonRpcNodeGet(Pipeline& pipe, const Var& id, Node* rep)
{
    rep->clear();
    rep->set("jsonrpc", "2.0");

    Node* ctx = pipe.context();
    if (commandTarget(pipe)) {
        if (Node* data = ctx ? ctx->find("output", false) : nullptr) {
            rep->at("result")->copy(data, true, true, true);
        } else {
            rep->at("result")->set(Var());
        }
    } else {
        std::string error = pipe.lastResult().message;
        if (error.empty()) {
            error = "node.get failed";
        }
        rep->at("error")->set("code", static_cast<int64_t>(jsonRpcErrorCode("not_found")));
        rep->at("error")->set("message", error);
    }

    rep->at("id")->set(id.isNull() ? Var() : id);
}

static Result atGetRequestToInput(Node* ctx, Node* in, Node* out)
{
    Node* root = pointerValue(in, "root");
    const std::string query = in->get("query").toString();
    const bool autoIgnore = queryBool(query, "auto_ignore", true);
    const bool wantChildren = queryBool(query, "children", false);
    const bool wantMeta = queryBool(query, "meta", false);
    const bool wantStructure = queryBool(query, "structure", false);
    const int depth = queryInt(query, "depth", -1);

    ctx->set("http/at/root", Var::ptr(root));
    if (auto* subscribe = in->get("subscribe").as<SubscribeService*>()) {
        ctx->set("http/at/subscribe", Var::ptr(subscribe));
    }
    ctx->set("http/at/children", wantChildren);
    ctx->set("http/at/meta", wantMeta);
    ctx->set("http/at/structure", wantStructure);
    ctx->set("http/at/depth", depth);
    ctx->set("http/at/auto_ignore", autoIgnore);

    out->clear();
    out->set("current", Var::ptr(root));
    out->set("path", normalizeHttpNodePath(in->get("path").toString()));

    return Result::ok();
}

static void finishAtGet(Pipeline& pipe)
{
    Node* ctx = pipe.context();
    if (!ctx) return;

    auto* rep = static_cast<http::web_response*>(ctx->get("http/response").toPointer());
    if (!rep) return;

    const bool wantChildren = ctx->get("http/at/children").toBool(false);
    const bool wantMeta = ctx->get("http/at/meta").toBool(false);
    const bool wantStructure = ctx->get("http/at/structure").toBool(false);
    const int depth = ctx->get("http/at/depth").toInt(-1);
    const bool autoIgnore = ctx->get("http/at/auto_ignore").toBool(true);

    Node* target = commandTarget(pipe);
    if (!target) {
        rep->fill_json(wantChildren ? "[]" : "null", http::status::ok);
        return;
    }

    auto* root = ctx->get("http/at/root").as<Node*>();
    auto* subscribe = ctx->get("http/at/subscribe").as<SubscribeService*>();

    if (wantChildren) {
        Node reply("r");
        if (wantMeta) {
            writeNodeMeta(reply.at("meta"), root, subscribe, target);
            Node* children = reply.at("children");
            for (auto* child : target->children()) {
                children->append()->set(target->keyOf(child));
            }
            rep->fill_json(toJson(reply), http::status::ok);
            return;
        }

        for (auto* child : target->children()) {
            reply.append()->set(target->keyOf(child));
        }
        rep->fill_json(toJson(reply), http::status::ok);
        return;
    }

    if (wantMeta) {
        Node reply("r");
        writeNodeMeta(&reply, root, subscribe, target);
        rep->fill_json(toJson(reply), http::status::ok);
        return;
    }

    schema::ExportOptions options = compactJson;
    options.auto_ignore = autoIgnore;
    Var tree = exportNodeTreeVar(target, depth, options);
    Var body = wantStructure ? stripValues(tree) : tree;
    rep->fill_json(impl::json::stringify(body), http::status::ok);
}

// http service-native builtin (private factory "standard/http").
// Single Proc: resolves the target and (for data endpoints) shapes the
// transport-neutral data node. The raw node pointer is parked in ctx for the
// /at renderer; envelope/format is each endpoint's step3 concern.
//   in : current(ptr) + path + data(bool) + root(ptr) + subscribe(ptr) + depth/meta
//   out: { path, value, [tree], [meta] }   (when data=true)
//   ctx/http/target : resolved Node* (or null)
static Result httpNodeGet(Node* ctx, Node* in, Node* out)
{
    Node* current = in->get("current").as<Node*>();
    if (!current) current = node::root();
    std::string path = in->get("path").toString();
    Node* target = path.empty() ? current : current->find(path);

    ctx->set("http/target", Var::ptr(target));
    if (!target) return Result::fail("node not found: " + path);

    if (in->get("data").toBool(false)) {
        auto* root = in->get("root").as<Node*>();
        auto* subscribe = in->get("subscribe").as<SubscribeService*>();
        writeNodeGetData(out, root, subscribe, in, target);
    }
    return Result::ok();
}

static void registerHttpBuiltins()
{
    auto& httpCmds = factory::at("standard/http");
    if (!httpCmds.has("node.get")) {
        httpCmds.reg("node.get", Var::callable(httpNodeGet), "node.get (http)");
    }
}

struct NodeHttpServer::Private
{
    Node*    root = nullptr;
    uint16_t port = 12000;

    int batch_limit = 500;

    asio2::http_server server;
    std::chrono::steady_clock::time_point startTime;
    std::unique_ptr<SubscribeService> subscribeSvc;
    std::unique_ptr<NodeTaskService> taskSvc;

    void runNodeGet(Node* req, Node* rep) const
    {
        const Var id = req ? req->get("id") : Var();

        Pipeline pipe("http.ve.node.get");
        pipe.context()->set("ve/root", Var::ptr(root));
        if (subscribeSvc) {
            pipe.context()->set("ve/subscribe", Var::ptr(subscribeSvc.get()));
        }
        pipe.context()->at("ve/request")->copy(req, true, true, true);
        pipe.addProc(veNodeGetRequestToInput, "ve/request", {});
        Command cmd = command::create(factory::at("standard/http"), "node.get",
                                      pipe.context(), nullptr, nullptr);
        pipe.addCommand(cmd);
        pipeline::start(pipe, [id, rep](Pipeline& p) {
            finishVeNodeGet(p, id, rep);
        });
        pipe.wait();
    }

    void runJsonRpcNodeGet(Node* params, const Var& id, Node* rep) const
    {
        Pipeline pipe("http.jsonrpc.node.get");
        pipe.context()->set("jsonrpc/root", Var::ptr(root));
        if (subscribeSvc) {
            pipe.context()->set("jsonrpc/subscribe", Var::ptr(subscribeSvc.get()));
        }
        pipe.context()->at("jsonrpc/params")->copy(params, true, true, true);
        pipe.addProc(jsonRpcNodeGetRequestToInput, "jsonrpc/params", {});
        Command cmd = command::create(factory::at("standard/http"), "node.get",
                                      pipe.context(), nullptr, nullptr);
        pipe.addCommand(cmd);
        pipeline::start(pipe, [id, rep](Pipeline& p) {
            finishJsonRpcNodeGet(p, id, rep);
        });
        pipe.wait();
    }

    void handleBatch(Node* req, Node* rep) const
    {
        Node* itemsNode = req->find("items");
        Var id = req->get("id");
        if (!itemsNode) {
            errorReply(rep, id, "invalid_params", "items array required");
            return;
        }
        if (batch_limit > 0 && itemsNode->count() > batch_limit) {
            errorReply(rep, id, "invalid_params", "batch size exceeds limit");
            return;
        }

        Node items("items");
        for (auto* child : itemsNode->children()) {
            handleVe(child, items.append());
        }
        okReply(rep, id, &items);
    }

    void handleVe(Node* req, Node* rep) const
    {
        if (!root || !req || !rep) {
            return;
        }

        Var id = req->get("id");
        std::string op = req->get("op").toString();
        if (op.empty()) {
            errorReply(rep, id, "invalid_request", "op is required");
            return;
        }

        if (op == "batch") {
            handleBatch(req, rep);
            return;
        }

        if (op == "node.get") {
            runNodeGet(req, rep);
            return;
        }

        if (op == "node.list") {
            std::string path = req->get("path").toString();
            Node* target = findNode(root, req);
            if (!target) {
                errorReply(rep, id, "not_found", "node not found: " + path);
                return;
            }

            bool withMeta = req->get("meta").toBool(false);
            Node data("data");
            data.set("path", target->path(root));
            Node* children = data.at("children");
            for (auto* child : target->children()) {
                makeChildInfo(children->append(), root, subscribeSvc.get(), child, withMeta);
            }
            okReply(rep, id, &data);
            return;
        }

        if (op == "node.set") {
            Node* valueNode = req->find("value");
            if (!valueNode) {
                errorReply(rep, id, "invalid_params", "value is required");
                return;
            }

            Node* target = ensureNode(root, req);
            target->set(schema::exportAs<schema::VarS>(valueNode));

            Node data("data");
            data.set("path", target->path(root));
            okReply(rep, id, &data);
            return;
        }

        if (op == "node.put") {
            Node* treeNode = req->find("tree");
            if (!treeNode) {
                treeNode = req->find("value");
            }
            if (!treeNode) {
                errorReply(rep, id, "invalid_params", "tree is required");
                return;
            }

            Node* target = ensureNode(root, req);
            schema::ImportOptions importOptions;
            importOptions.auto_insert = true;
            importOptions.auto_remove = true;
            importOptions.auto_update = true;
            schema::importAs<schema::VarS>(
                target, schema::exportAs<schema::VarS>(treeNode), importOptions);

            Node data("data");
            data.set("path", target->path(root));
            okReply(rep, id, &data);
            return;
        }

        if (op == "node.remove") {
            std::string path = req->get("path").toString();
            if (path.empty()) {
                errorReply(rep, id, "invalid_params", "cannot remove root");
                return;
            }
            if (!root->erase(path)) {
                errorReply(rep, id, "not_found", "node not found: " + path);
                return;
            }

            Node data("data");
            data.set("path", path);
            okReply(rep, id, &data);
            return;
        }

        if (op == "node.trigger") {
            std::string path = req->get("path").toString();
            Node* target = findNode(root, req);
            if (!target) {
                errorReply(rep, id, "not_found", "node not found: " + path);
                return;
            }

            target->trigger<Node::NODE_CHANGED>();
            if (target->isWatching()) {
                target->activate(Node::NODE_CHANGED, target);
            }

            Node data("data");
            data.set("path", target->path(root));
            okReply(rep, id, &data);
            return;
        }

        if (op == "command.list") {
            auto cmds = command::factory().keys();
            std::sort(cmds.begin(), cmds.end());
            cmds.erase(std::unique(cmds.begin(), cmds.end()), cmds.end());

            Node data("data");
            Node* commands = data.at("commands");
            for (const auto& key : cmds) {
                Node* item = commands->append();
                item->set("name", key);
                item->set("help", command::factory().help(key));
            }
            okReply(rep, id, &data);
            return;
        }

        if (op == "command.run") {
            errorReply(rep, id, "unsupported",
                       "command.run is disabled on the command refactor branch; use HTTP /cmd");
            return;
        }

        if (op == "subscribe" || op == "unsubscribe") {
            errorReply(rep, id, "unsupported",
                       "subscriptions are not supported on this transport");
            return;
        }

        errorReply(rep, id, "unknown_op", "unknown op: " + op);
    }

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

        if (method == "node.get") {
            Node out("r");
            runJsonRpcNodeGet(&protocolReq, id, &out);
            return toJson(out);
        }

        Node protocolRep("rep");
        handleVe(&protocolReq, &protocolRep);

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

NodeHttpServer::NodeHttpServer(const Node* config_n) : _p(std::make_unique<Private>())
{
    _p->root = ve::n(config_n->get("root").toString("/"));
    _p->port = config_n->get("port").toInt(0); // default stop

    _p->batch_limit = config_n->get("batch_limit").toInt(500);
}

NodeHttpServer::~NodeHttpServer()
{
    stop();
}

bool NodeHttpServer::start()
{
    registerHttpBuiltins();
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
            _p->handleVe(&protocolReq, &protocolRep);
            rep.fill_json(toJson(protocolRep), statusFromReply(&protocolRep));
        });

    auto bindAtGet = [this](const std::string& nodePath, http::web_request& req, http::web_response& rep) {
        Pipeline pipe("http.at.node.get");
        pipe.context()->set("http/response", Var::ptr(&rep));
        Node* request = pipe.context()->at("http/at/request");
        request->set("path", nodePath);
        request->set("query", std::string(req.query()));
        request->set("root", Var::ptr(_p->root));
        if (_p->subscribeSvc) {
            request->set("subscribe", Var::ptr(_p->subscribeSvc.get()));
        }
        pipe.addProc(atGetRequestToInput, "http/at/request", {});
        Command cmd = command::create(factory::at("standard/http"), "node.get",
                                      pipe.context(), nullptr, nullptr);
        pipe.addCommand(cmd);
        pipeline::start(pipe, finishAtGet);
        pipe.wait();
    };

    auto bindAtPut = [this](const std::string& nodePath, http::web_request& req, http::web_response& rep) {
        std::string body(req.body());
        if (body.empty()) {
            Node protocolRep("rep");
            fillError(&protocolRep, "invalid_params", "request body is required");
            rep.fill_json(toJson(protocolRep), http::status::bad_request);
            return;
        }

        Node* target = _p->root->atPath(nodePath, true, '/', HTTP_KEY_SEP);
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
        Node* target = (trigger || body.empty())
            ? const_cast<const Node*>(_p->root)->atPath(nodePath, false, '/', HTTP_KEY_SEP)
            : _p->root->atPath(nodePath, true, '/', HTTP_KEY_SEP);

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
        Node* target = const_cast<const Node*>(_p->root)->atPath(nodePath, false, '/', HTTP_KEY_SEP);
        if (!target || !target->parent() || !target->parent()->remove(target)) {
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
        if (!command::factory().has(cmdKey)) {
            Node reply("rep");
            fillError(&reply, "not_found", "unknown command: " + cmdKey);
            rep.fill_json(toJson(reply), http::status::not_found);
            return;
        }

        Node ctx("_ctx");
        prepareHttpCommandContext(&ctx, cmdKey, req, rep, _p->root);

        const bool async = queryBool(req.query(), "async", false);
        if (async) {
            Pipeline parsePipe("http.cmd.parse", &ctx);
            parsePipe.addProc(httpRequestToInput, "http/request", "request");
            pipeline::start(parsePipe);
            parsePipe.wait();
            if (parsePipe.lastResult().isError()) {
                Node reply("rep");
                Node* error = parsePipe.context()->find("http/error");
                fillError(&reply,
                    error ? error->get("code").toString("invalid_request") : "invalid_request",
                    parsePipe.lastResult().message);
                rep.fill_json(toJson(reply), statusFromReply(&reply));
                return;
            }

            if (!_p->taskSvc) {
                Node reply("rep");
                fillError(&reply, "internal_error", "task service unavailable");
                rep.fill_json(toJson(reply), http::status::internal_server_error);
                return;
            }

            auto* detached = new Pipeline(cmdKey, parsePipe.context());
            detached->context()->erase("http/response");
            Command cmd = command::create(cmdKey, detached->context(),
                                          detached->context()->at("request"),
                                          detached->context()->at("reply"));
            detached->addCommand(cmd);
            std::string taskId = _p->taskSvc->attach(cmdKey, Var(), detached, {});
            if (taskId.empty()) {
                delete detached;
                Node reply("rep");
                fillError(&reply, "internal_error", "failed to start task");
                rep.fill_json(toJson(reply), http::status::internal_server_error);
                return;
            }

            pipeline::start(detached);
            Node out("r");
            out.set("ok", true);
            out.set("accepted", true);
            out.set("task_id", taskId);
            rep.fill_json(toJson(out), http::status::accepted);
        } else {
            Pipeline pipe("http.cmd", &ctx);
            pipe.addProc(httpRequestToInput, "http/request", {});
            Command cmd = command::create(cmdKey, pipe.context(), nullptr, nullptr);
            pipe.addCommand(cmd);
            pipe.addProc(outputToHttpResponse, "reply", "http/render");
            pipeline::start(pipe, finishHttpPipeline);
            pipe.wait();
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
