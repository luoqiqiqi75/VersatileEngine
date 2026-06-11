// node_http_service.cpp — ve::service::NodeHttpServer
#include "ve/service/node_service.h"
#include "ve/core/command.h"
#include "ve/core/node.h"
#include "ve/core/pipeline.h"
#include "ve/core/schema.h"
#include "ve/core/impl/json.h"
#include "server_util.h"
#include "node_commands.h"

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

struct HttpRep : std::pair<http::status, std::string>
{
    using std::pair<http::status, std::string>::pair;
    explicit HttpRep() { first = http::status::ok; }
};
struct HttpResultRep : std::pair<Result, Node*>
{
    using std::pair<Result, Node*>::pair;
    explicit HttpResultRep(Result&& r) { first = std::move(r); second = nullptr; }
    explicit HttpResultRep(Node* n) { first = Result::ok(); second = n; }
};

}

namespace convert {

static bool parse(const service::HttpRep& r, http::web_response& rep)
{
    rep.fill_json(r.second, r.first);
    return true;
}

static bool parse(const service::HttpResultRep& r, http::web_response& rep)
{
    Node proto_n;
    proto_n.set("code", r.first.code());
    proto_n.set("message", r.first.message());
    proto_n.at("data")->copy(r.second);
    http::status status = r.first.isAccepted() ? http::status::accepted : http::status::ok; // always ok
    return parse(service::HttpRep { status, schema::exportAs<schema::JsonS>(&proto_n, service::compactJson) }, rep);
}

}

namespace service {

static std::string toJson(Node& n)
{
    return schema::exportAs<schema::JsonS>(&n, compactJson);
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
                     pipe.lastResult().message());
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

static Node* commandTarget(Pipeline& pipe)
{
    Node* ctx = pipe.context();
    return ctx ? ctx->get("http/target").as<Node*>() : nullptr;
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

    if (wantChildren) {
        Node reply("r");
        if (wantMeta) {
            writeNodeMeta(reply.at("meta"), root, target);
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
        writeNodeMeta(&reply, root, target);
        rep->fill_json(toJson(reply), http::status::ok);
        return;
    }

    schema::ExportOptions options = compactJson;
    options.auto_ignore = autoIgnore;
    Var tree = exportNodeTree(target, depth, options);
    Var body = wantStructure ? stripValues(tree) : tree;
    rep->fill_json(impl::json::stringify(body), http::status::ok);
}

// /at service-native builtin (private factory "standard/http").
// Resolves the target into ctx; finishAtGet renders it per the query flags.
//   in  : current(ptr) + path
//   ctx/http/target : resolved Node* (or null)
static Result httpNodeGet(Node* ctx, Node* in, Node*)
{
    Node* current = in->get("current").as<Node*>();
    if (!current) current = node::root();
    std::string path = in->get("path").toString();
    Node* target = path.empty() ? current : current->find(path);

    ctx->set("http/target", Var::ptr(target));
    if (!target) return Result::fail("node not found: " + path);
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
    Object   obj;
    Node*    root = nullptr;
    uint16_t port = 12000;

    int batch_limit = 500;

    asio2::http_server server;
    std::chrono::steady_clock::time_point startTime;

    // /ve and /jsonrpc both run through the one shared node dispatch. HTTP is
    // sessionless, so subscribe/unsubscribe report unsupported (no Session).
    void handleVe(Node* req, Node* rep) const
    {
        dispatchNode(root, req, rep, nullptr, batch_limit);
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
    registerNodeCommands();

    auto& http_f = factory::at("standard/http");

    { // health protocol
        _p->startTime = std::chrono::steady_clock::now();

        auto* http_timestamp_fn = ve::command::reg(http_f, "timestamp", [=] {
            auto elapsed = std::chrono::steady_clock::now() - _p->startTime;
            auto seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
            return seconds;
        });

        _p->server.bind<http::verb::get>("/health", [=] (http::web_request&, http::web_response& rep) {
            int s = Command(http_timestamp_fn).run().output()->getInt64();
            convert::parse(HttpRep(http::status::ok, "\"status\":\"ok\",\"uptime_s\":" + std::to_string(s) + "}"), rep);
        });
    }

    { // at protocol
        auto tar_n_f = [root_n = _p->root] (http::web_request& req, http::web_response& rep) {
            auto sv = req.path();
            sv.remove_prefix(4); // /at/
            Node* tar_n = root_n->atPath(sv, false, VE_NODE_PATH_SEP, HTTP_KEY_SEP);
            if (!tar_n) convert::parse(HttpRep{http::status::not_found, "node not found"}, rep);
            return tar_n;
        };

        // export tree
        _p->server.bind<http::verb::get>("/at", [root_n = _p->root] (http::web_request&, http::web_response& rep) {
            convert::parse(HttpResultRep(root_n), rep);
        });
        _p->server.bind<http::verb::get>("/at/*", [=] (http::web_request& req, http::web_response& rep) {
            if (const auto tar_n = tar_n_f(req, rep)) convert::parse(HttpResultRep(tar_n), rep);
        });

        // import tree
        // _p->server.bind<http::verb::put>("/at", [] (http::web_request& req, http::web_response& rep) {
        //     convert::parse(HttpResult(http::status::forbidden, "forbid to import whole node tree"), rep);
        // });
        _p->server.bind<http::verb::put>("/at/*", [tar_n_f] (http::web_request& req, http::web_response& rep) {
            if (const auto tar_n = tar_n_f(req, rep)) {
                if (schema::importAs<schema::JsonS>(tar_n, req.body())) { // without deletion
                    convert::parse(HttpRep(), rep);
                } else {
                    convert::parse(HttpResultRep(Result::fail(JRpcParseError, "invalid json")), rep); // todo error code control
                }
            }
        });

        // _p->server.bind<http::verb::post>("/at", [] (http::web_request&, http::web_response& rep) {
        //     convert::parse(HttpResult(http::status::forbidden, "forbid to import whole node tree"), rep);
        // });
        _p->server.bind<http::verb::post>("/at/*", [tar_n_f] (http::web_request& req, http::web_response& rep) {
            if (auto const tar_n = tar_n_f(req, rep)) {
                if (schema::importAs<schema::JsonS>(tar_n, req.body(), schema::ImportOptions { true, true, false})) { // with deletion
                    convert::parse(HttpRep(), rep);
                } else {
                    convert::parse(HttpResultRep(Result::fail(JRpcParseError, "invalid json")), rep); // todo error code control
                }
            }
        });

        _p->server.bind<http::verb::delete_>("/at/*", [tar_n_f] (http::web_request& req, http::web_response& rep) {
            if (auto const tar_n = tar_n_f(req, rep)) {
                if (tar_n->parent()->remove(tar_n)) {
                    convert::parse(HttpRep(), rep);
                } else {
                    convert::parse(HttpResultRep{Result::fail(JRpcInternalError, "failed")}, rep);
                }
            }
        });
    }

    { // cmd protocol
        _p->server.bind<http::verb::post>("/cmd/*", [o = &_p->obj] (http::web_request& req, http::web_response& rep) {
            auto cmd_sv = req.path();
            cmd_sv.remove_prefix(5); // /cmd/
            std::string cmd_key(cmd_sv);
            Command cmd = command::create(cmd_key);
            if (!cmd.valid()) {
                convert::parse(HttpResultRep(Result::fail(JRpcMethodNotFound, "unknown command")), rep);
                return;
            }

            Pipeline pipe;
            if (!schema::importAs<schema::JsonS>(pipe.inputNode(), req.body())) {
                convert::parse(HttpResultRep(Result::fail(JRpcInvalidRequest, "bad request")), rep);
                return;
            }
            pipe.add(cmd);

            if (queryBool(req.query(), "async", false)) {
               pipe.onFinished(o, [guard = rep.defer(), rep_ptr = &rep] (Pipeline& p) {
                   convert::parse(HttpResultRep(p.result(), p.outputNode()), *rep_ptr);
               });
               pipe.async();
           } else {
               pipe.sync();
               convert::parse(HttpResultRep(pipe.result(), pipe.outputNode()), rep);
           }
        });
    }

    { // ve standard protocol
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
    }

    { // jsonrpc protocol
        _p->server.bind<http::verb::post>("/jsonrpc", [this](http::web_request& req, http::web_response& rep) {
           rep.fill_json(_p->handleJsonRpc(std::string(req.body())), http::status::ok);
       });
    }

    _p->server.bind_not_found([] (http::web_request&, http::web_response& rep) {
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
}

bool NodeHttpServer::isRunning() const
{
    return _p->server.is_started();
}

} // namespace service
} // namespace ve
