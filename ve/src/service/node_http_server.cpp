// node_http_service.cpp — ve::service::NodeHttpServer
#include "ve/service/node_service.h"
#include "ve/core/node.h"
#include "ve/core/var.h"
#include "ve/core/command.h"
#include "ve/core/impl/json.h"
#include "ve/core/log.h"

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
    if (s == "favicon.ico" || s == "robots.txt") {
        return true;
    }
    if (s.rfind("apple-touch-icon", 0) == 0) {
        return true;
    }
    if (s.rfind("android-chrome", 0) == 0) {
        return true;
    }
    return false;
}

static bool isPathSafe(const std::string& relPath)
{
    if (relPath.find("..") != std::string::npos) {
        return false;
    }
    if (relPath.find('\\') != std::string::npos) {
        return false;
    }
    if (!relPath.empty() && relPath[0] == '/') {
        return false;
    }
    return true;
}

static std::string readFileBytes(const std::filesystem::path& filepath)
{
    std::ifstream f(filepath, std::ios::binary);
    if (!f.is_open()) {
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

// ============================================================================
// Private
// ============================================================================

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

struct NodeHttpServer::Private
{
    Node*    root = nullptr;
    uint16_t port = 8080;
    asio2::http_server server;
    std::chrono::steady_clock::time_point startTime;

    std::string staticRoot;
    std::string defaultFile = "index.html";

    bool tryServeFile(const std::string& reqPath, http::web_response& rep);

    // JSON-RPC 2.0
    std::string handleJsonRpc(const std::string& requestJson);
    Var jrpcNodeGet(const Var& params);
    Var jrpcNodeSet(const Var& params);
    Var jrpcNodeList(const Var& params);
    Var jrpcCommandRun(const Var& params);
    std::string jrpcResponse(const Var& id, const Var& result);
    std::string jrpcError(const Var& id, int code, const std::string& message);
};

// ============================================================================
// NodeHttpServer
// ============================================================================

bool NodeHttpServer::Private::tryServeFile(const std::string& reqPathIn, http::web_response& rep)
{
    if (staticRoot.empty()) {
        return false;
    }

    std::string reqPath = reqPathIn;
    {
        const auto cut = reqPath.find_first_of("?#");
        if (cut != std::string::npos) {
            reqPath.erase(cut);
        }
    }

    std::string rel = reqPath;
    while (!rel.empty() && rel[0] == '/') {
        rel.erase(rel.begin());
    }
    if (rel.empty()) {
        rel = defaultFile;
    }

    if (!isPathSafe(rel)) {
        return false;
    }

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

    std::string mime = mimeForPath(rel);
    rep.fill_text(std::move(content), http::status::ok, mime);
    return true;
}

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
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
    }
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
            std::string json = "{\"status\":\"ok\",\"uptime_s\":" + std::to_string(sec) + "}";
            rep.fill_json(std::move(json), http::status::ok);
        });

    // POST /jsonrpc — JSON-RPC 2.0 endpoint
    _p->server.bind<http::verb::post>("/jsonrpc",
        [this](http::web_request& req, http::web_response& rep) {
            std::string response = _p->handleJsonRpc(std::string(req.body()));
            rep.fill_json(std::move(response), http::status::ok);
        });

    // GET / — explicit route so static root index.html is served before generic not_found
    _p->server.bind<http::verb::get>("/",
        [this](http::web_request& req, http::web_response& rep) {
            if (_p->tryServeFile(std::string(req.path()), rep)) {
                return;
            }
            rep.fill_json("{\"error\":\"not found\"}", http::status::not_found);
        });

    // GET /api/node/*
    _p->server.bind<http::verb::get>("/api/node/*",
        [this](http::web_request& req, http::web_response& rep) {
            auto nodePath = strip_prefix(req.path(), "/api/node");
            Node* target = nodePath.empty() ? _p->root : _p->root->find(nodePath);
            if (!target) {
                rep.fill_json("{\"error\":\"not found\"}", http::status::not_found);
                return;
            }
            const Var& tv = target->get();
            std::string json = "{\"path\":\"" + target->path(_p->root) + "\",\"value\":"
                + (tv.isNull() ? std::string("null") : impl::json::stringify(tv)) + "}";
            rep.fill_json(std::move(json), http::status::ok);
        });

    // PUT /api/node/*
    _p->server.bind<http::verb::put>("/api/node/*",
        [this](http::web_request& req, http::web_response& rep) {
            auto nodePath = strip_prefix(req.path(), "/api/node");
            if (nodePath.empty()) {
                rep.fill_json("{\"error\":\"path required\"}", http::status::bad_request);
                return;
            }
            Node* target = _p->root->at(nodePath);
            if (!target) {
                rep.fill_json("{\"error\":\"cannot create node\"}", http::status::internal_server_error);
                return;
            }
            std::string body(req.body());
            if (!body.empty()) {
                target->set(impl::json::parse(body));
            }
            rep.fill_json("{\"ok\":true,\"path\":\"" + target->path(_p->root) + "\"}", http::status::ok);
        });

    // POST /api/node/* — trigger node signal (optional body sets value first)
    _p->server.bind<http::verb::post>("/api/node/*",
        [this](http::web_request& req, http::web_response& rep) {
            auto nodePath = strip_prefix(req.path(), "/api/node");
            if (nodePath.empty()) {
                rep.fill_json("{\"error\":\"path required\"}", http::status::bad_request);
                return;
            }
            Node* target = _p->root->at(nodePath);
            if (!target) {
                rep.fill_json("{\"error\":\"not found\"}", http::status::not_found);
                return;
            }
            std::string body(req.body());
            if (!body.empty()) {
                target->set(impl::json::parse(body));
            } else {
                target->trigger<Node::NODE_CHANGED>();
                target->activate(Node::NODE_CHANGED, target);
            }
            rep.fill_json("{\"ok\":true,\"path\":\"" + target->path(_p->root) + "\"}", http::status::ok);
        });

    // GET /api/tree/*
    _p->server.bind<http::verb::get>("/api/tree/*",
        [this](http::web_request& req, http::web_response& rep) {
            auto nodePath = strip_prefix(req.path(), "/api/tree");
            Node* target = nodePath.empty() ? _p->root : _p->root->at(nodePath);
            if (!target) {
                rep.fill_json("{\"error\":\"not found\"}", http::status::not_found);
                return;
            }
            rep.fill_json(impl::json::exportTree(target), http::status::ok);
        });

    // POST /api/tree/* — import JSON subtree
    _p->server.bind<http::verb::post>("/api/tree/*",
        [this](http::web_request& req, http::web_response& rep) {
            auto nodePath = strip_prefix(req.path(), "/api/tree");
            Node* target = nodePath.empty() ? _p->root : _p->root->at(nodePath);
            if (!target) {
                rep.fill_json("{\"error\":\"cannot create node\"}", http::status::internal_server_error);
                return;
            }
            std::string body(req.body());
            if (body.empty()) {
                rep.fill_json("{\"error\":\"empty body\"}", http::status::bad_request);
                return;
            }
            if (impl::json::importTree(target, body))
                rep.fill_json("{\"ok\":true,\"path\":\"" + target->path(_p->root) + "\"}", http::status::ok);
            else
                rep.fill_json("{\"error\":\"invalid JSON\"}", http::status::bad_request);
        });

    // POST /api/cmd/* — execute a named command via command::call()
    _p->server.bind<http::verb::get>("/api/cmd",
        [this](http::web_request& req, http::web_response& rep) {
            auto cmds = command::keys();
            std::sort(cmds.begin(), cmds.end());
            cmds.erase(std::unique(cmds.begin(), cmds.end()), cmds.end());

            std::string json = "{\"commands\":[";
            for (size_t i = 0; i < cmds.size(); ++i) {
                const auto& key = cmds[i];
                if (i > 0) {
                    json += ",";
                }
                std::string help = command::help(key);
                json += "{\"name\":" + impl::json::stringify(Var(key))
                      + ",\"help\":" + impl::json::stringify(Var(help)) + "}";
            }
            json += "]}";
            rep.fill_json(std::move(json), http::status::ok);
        });

    // POST /api/cmd/* — execute a named command via command::call()
    _p->server.bind<http::verb::post>("/api/cmd/*",
        [this](http::web_request& req, http::web_response& rep) {
            std::string bodyStr(req.body());
            Var bodyParsed = bodyStr.empty() ? Var() : impl::json::parse(bodyStr);

            std::string cmdKey = strip_prefix(req.path(), "/api/cmd");
            // Some transports may pass a wildcard path unexpectedly; allow body.cmd fallback.
            if (bodyParsed.isDict()) {
                auto& dict = bodyParsed.toDict();
                if ((cmdKey.empty() || cmdKey.front() == '{') && dict.has("cmd")) {
                    cmdKey = dict["cmd"].toString();
                }
            }
            if (cmdKey.empty()) {
                rep.fill_json("{\"error\":\"command key required\"}", http::status::bad_request);
                return;
            }
            if (!command::has(cmdKey)) {
                rep.fill_json("{\"error\":\"unknown command: " + cmdKey + "\"}", http::status::not_found);
                return;
            }

            int64_t reqId = 0;
            Var cmdBody;
            if (bodyParsed.isDict()) {
                auto& dict = bodyParsed.toDict();
                if (dict.has("id"))   reqId   = dict["id"].toInt64();
                if (dict.has("body")) cmdBody = dict["body"];
                else if (dict.has("args")) cmdBody = dict["args"];
                else                  cmdBody = bodyParsed;
            } else {
                cmdBody = bodyParsed;
            }

            auto result = command::call(cmdKey, cmdBody);
            std::string idField = reqId ? ",\"id\":" + std::to_string(reqId) : "";

            if (result.isSuccess() || result.isAccepted()) {
                rep.fill_json("{\"ok\":true,\"result\":"
                    + impl::json::stringify(result.content()) + idField + "}", http::status::ok);
            } else {
                rep.fill_json("{\"error\":"
                    + impl::json::stringify(Var(result).toString()) + idField + "}",
                    http::status::internal_server_error);
            }
        });

    // GET /api/children/*
    _p->server.bind<http::verb::get>("/api/children/*",
        [this](http::web_request& req, http::web_response& rep) {
            auto nodePath = strip_prefix(req.path(), "/api/children");
            Node* target = nodePath.empty() ? _p->root : _p->root->at(nodePath);
            if (!target) {
                rep.fill_json("{\"error\":\"not found\"}", http::status::not_found);
                return;
            }
            std::string json = "[";
            int total = target->count();
            for (int i = 0; i < total; ++i) {
                auto* c = target->child(i);
                if (i > 0) json += ",";
                json += "\"" + (c->name().empty() ? std::string("") : c->name()) + "\"";
            }
            json += "]";
            rep.fill_json(std::move(json), http::status::ok);
        });

    // Static file fallback, then 404
    _p->server.bind_not_found(
        [this](http::web_request& req, http::web_response& rep) {
            if (_p->tryServeFile(std::string(req.path()), rep)) {
                return;
            }
            rep.fill_json("{\"error\":\"not found\"}", http::status::not_found);
        });

    bool ok = _p->server.start("0.0.0.0", _p->port);
    if (ok)
        veLogIs("NodeHttpServer started on port", _p->port);
    else
        veLogEs("NodeHttpServer failed to start on port", _p->port);
    return ok;
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
        Var request = impl::json::parse(requestJson);
        if (!request.isDict()) {
            return jrpcError(Var(), JRpcInvalidRequest, "Request must be an object");
        }

        auto& dict = request.toDict();

        if (!dict.has("jsonrpc") || dict["jsonrpc"].toString() != "2.0") {
            return jrpcError(Var(), JRpcInvalidRequest, "Invalid jsonrpc version");
        }

        Var id = dict.has("id") ? dict["id"] : Var();

        if (!dict.has("method") || !dict["method"].isString()) {
            return jrpcError(id, JRpcInvalidRequest, "Missing or invalid method");
        }
        std::string method = dict["method"].toString();

        Var params = dict.has("params") ? dict["params"] : Var(Var::DictV{});

        Var result;
        if (method == "node.get") {
            result = jrpcNodeGet(params);
        } else if (method == "node.set") {
            result = jrpcNodeSet(params);
        } else if (method == "node.list") {
            result = jrpcNodeList(params);
        } else if (method == "command.run") {
            result = jrpcCommandRun(params);
        } else {
            return jrpcError(id, JRpcMethodNotFound, "Method not found: " + method);
        }

        return jrpcResponse(id, result);
    }
    catch (const std::exception& e) {
        return jrpcError(Var(), JRpcInternalError, e.what());
    }
}

Var NodeHttpServer::Private::jrpcNodeGet(const Var& params)
{
    if (!params.isDict()) {
        throw std::runtime_error("params must be an object");
    }

    auto& dict = params.toDict();
    if (!dict.has("path")) {
        throw std::runtime_error("Missing required parameter: path");
    }

    std::string path = dict["path"].toString();
    Node* node = root->find(path);

    Var::DictV result;
    if (!node) {
        result["found"] = false;
    } else {
        result["found"] = true;
        result["value"] = node->get();
        result["path"] = node->path();
    }
    return Var(result);
}

Var NodeHttpServer::Private::jrpcNodeSet(const Var& params)
{
    if (!params.isDict()) {
        throw std::runtime_error("params must be an object");
    }

    auto& dict = params.toDict();
    if (!dict.has("path") || !dict.has("value")) {
        throw std::runtime_error("Missing required parameters: path, value");
    }

    std::string path = dict["path"].toString();
    Var value = dict["value"];

    Node* node = root->find(path);
    if (!node) {
        node = root->at(path);
    }

    if (node) {
        node->set(value);
    }

    Var::DictV result;
    result["success"] = (node != nullptr);
    if (node) {
        result["path"] = node->path();
    }
    return Var(result);
}

Var NodeHttpServer::Private::jrpcNodeList(const Var& params)
{
    if (!params.isDict()) {
        throw std::runtime_error("params must be an object");
    }

    auto& dict = params.toDict();
    if (!dict.has("path")) {
        throw std::runtime_error("Missing required parameter: path");
    }

    std::string path = dict["path"].toString();
    Node* node = root->find(path);

    Var::DictV result;
    if (!node) {
        result["found"] = false;
    } else {
        Var::ListV children;
        for (auto* child : node->children()) {
            Var::DictV childInfo;
            childInfo["name"] = child->name();
            childInfo["path"] = child->path();
            childInfo["hasValue"] = !child->get().isNull();
            childInfo["childCount"] = static_cast<int64_t>(child->children().size());
            children.push_back(Var(childInfo));
        }

        result["found"] = true;
        result["path"] = node->path();
        result["children"] = Var(children);
    }
    return Var(result);
}

Var NodeHttpServer::Private::jrpcCommandRun(const Var& params)
{
    if (!params.isDict()) {
        throw std::runtime_error("params must be an object");
    }

    auto& dict = params.toDict();
    if (!dict.has("name")) {
        throw std::runtime_error("Missing required parameter: name");
    }

    std::string name = dict["name"].toString();
    Var args = dict.has("args") ? dict["args"] : Var(Var::DictV{});

    Var cmdResult = command::call(name, args);

    Var::DictV response;
    response["result"] = cmdResult;
    return Var(response);
}

std::string NodeHttpServer::Private::jrpcResponse(const Var& id, const Var& result)
{
    Var::DictV response;
    response["jsonrpc"] = "2.0";
    response["result"] = result;
    if (!id.isNull()) {
        response["id"] = id;
    }
    return impl::json::stringify(Var(response));
}

std::string NodeHttpServer::Private::jrpcError(const Var& id, int code, const std::string& message)
{
    Var::DictV error;
    error["code"] = static_cast<int64_t>(code);
    error["message"] = message;

    Var::DictV response;
    response["jsonrpc"] = "2.0";
    response["error"] = Var(error);
    if (!id.isNull()) {
        response["id"] = id;
    } else {
        response["id"] = Var();
    }

    return impl::json::stringify(Var(response));
}

} // namespace service
} // namespace ve
