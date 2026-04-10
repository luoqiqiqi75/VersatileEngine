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

#include <chrono>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_map>

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

static const std::unordered_map<std::string, std::string>& mimeTypes()
{
    static const std::unordered_map<std::string, std::string> m = {
        {".html",  "text/html"},
        {".htm",   "text/html"},
        {".css",   "text/css"},
        {".js",    "application/javascript"},
        {".json",  "application/json"},
        {".xml",   "application/xml"},
        {".txt",   "text/plain"},
        {".csv",   "text/csv"},
        {".png",   "image/png"},
        {".jpg",   "image/jpeg"},
        {".jpeg",  "image/jpeg"},
        {".gif",   "image/gif"},
        {".svg",   "image/svg+xml"},
        {".ico",   "image/x-icon"},
        {".webp",  "image/webp"},
        {".woff",  "font/woff"},
        {".woff2", "font/woff2"},
        {".ttf",   "font/ttf"},
        {".otf",   "font/otf"},
        {".eot",   "application/vnd.ms-fontobject"},
        {".mp3",   "audio/mpeg"},
        {".mp4",   "video/mp4"},
        {".webm",  "video/webm"},
        {".wasm",  "application/wasm"},
        {".pdf",   "application/pdf"},
        {".zip",   "application/zip"},
        {".map",   "application/json"},
    };
    return m;
}

static std::string mimeForPath(const std::string& path)
{
    auto dot = path.rfind('.');
    if (dot != std::string::npos) {
        std::string ext = path.substr(dot);
        for (auto& c : ext) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
        auto it = mimeTypes().find(ext);
        if (it != mimeTypes().end()) {
            return it->second;
        }
    }
    return "application/octet-stream";
}

static bool isBrowserOptionalProbe(const std::string& rel)
{
    std::string s = rel;
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (s == "favicon.ico" || s == "robots.txt") return true;
    if (s.rfind("apple-touch-icon", 0) == 0) return true;
    if (s.rfind("android-chrome", 0) == 0) return true;
    return false;
}

static bool isPathSafe(const std::string& relPath)
{
    if (relPath.find("..") != std::string::npos) return false;
    if (relPath.find('\\') != std::string::npos) return false;
    if (!relPath.empty() && relPath[0] == '/') return false;
    return true;
}

static std::string readFileBytes(const std::filesystem::path& filepath)
{
    std::ifstream f(filepath, std::ios::binary);
    if (!f.is_open()) return {};
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
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

    std::string staticRoot;
    std::string defaultFile = "index.html";

    bool tryServeFile(const std::string& reqPath, http::web_response& rep);

    // JSON-RPC 2.0
    std::string handleJsonRpc(const std::string& requestJson);
    std::string jrpcResponse(const Var& id, const Var& result);
    std::string jrpcError(const Var& id, int code, const std::string& message);
};

bool NodeHttpServer::Private::tryServeFile(const std::string& reqPathIn, http::web_response& rep)
{
    if (staticRoot.empty()) return false;

    std::string reqPath = reqPathIn;
    {
        const auto cut = reqPath.find_first_of("?#");
        if (cut != std::string::npos) reqPath.erase(cut);
    }

    std::string rel = reqPath;
    while (!rel.empty() && rel[0] == '/') rel.erase(rel.begin());
    if (rel.empty()) rel = defaultFile;

    if (!isPathSafe(rel)) return false;

    namespace fs = std::filesystem;
    const fs::path full = (fs::path(staticRoot) / rel).lexically_normal();

    std::error_code ec;
    if (!fs::is_regular_file(full, ec)) {
        if (!isBrowserOptionalProbe(rel)) {
            veLogWs("[http] static file missing or not a file:", full.string(),
                    ec ? ec.message().c_str() : "");
        }
        return false;
    }

    std::string content = readFileBytes(full);
    if (content.empty()) {
        const auto sz = fs::file_size(full, ec);
        if (!ec && sz > 0) {
            veLogWs("[http] static file open/read failed:", full.string());
            return false;
        }
    }

    rep.fill_text(std::move(content), http::status::ok, mimeForPath(rel));
    return true;
}

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

void NodeHttpServer::setStaticRoot(const std::string& dirPath)
{
    std::string s = dirPath;
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    _p->staticRoot = std::move(s);
}

void NodeHttpServer::setDefaultFile(const std::string& filename)
{
    _p->defaultFile = filename;
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

    // GET /
    _p->server.bind<http::verb::get>("/",
        [this](http::web_request& req, http::web_response& rep) {
            if (_p->tryServeFile(std::string(req.path()), rep)) return;
            rep.fill_json(jsonError("not found"), http::status::not_found);
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
            if (!body.empty()) {
                Node tmp("tmp");
                if (schema::importAs<schema::JsonS>(&tmp, body)) {
                    if (tmp.count() > 0) {
                        target->copy(&tmp);
                    } else {
                        target->set(tmp.get());
                    }
                }
            } else {
                // Empty body = trigger only, set same value to emit signal
                target->set(target->get());
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
            // Parse request body
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

    // Static file fallback
    _p->server.bind_not_found(
        [this](http::web_request& req, http::web_response& rep) {
            if (_p->tryServeFile(std::string(req.path()), rep)) return;
            rep.fill_json(jsonError("not found"), http::status::not_found);
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
        // Parse request
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

        // Route to handler
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
