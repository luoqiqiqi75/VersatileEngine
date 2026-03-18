// http_server.cpp — ve::HttpServer implementation
#include "ve/service/http_server.h"
#include "ve/core/node.h"
#include "ve/core/var.h"
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
#include <string>

namespace ve {

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

// JSON parse/stringify now provided by ve/core/impl/json.h

// ============================================================================
// Private
// ============================================================================

struct HttpServer::Private
{
    Node*    root = nullptr;
    uint16_t port = 8080;
    asio2::http_server server;
    std::chrono::steady_clock::time_point startTime;
};

// ============================================================================
// HttpServer
// ============================================================================

HttpServer::HttpServer(Node* root, uint16_t port)
    : _p(std::make_unique<Private>())
{
    _p->root = root;
    _p->port = port;
}

HttpServer::~HttpServer()
{
    stop();
}

bool HttpServer::start()
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

    // GET /api/node/*
    _p->server.bind<http::verb::get>("/api/node/*",
        [this](http::web_request& req, http::web_response& rep) {
            auto nodePath = strip_prefix(req.path(), "/api/node");
            Node* target = nodePath.empty() ? _p->root : _p->root->resolve(nodePath);
            if (!target) {
                rep.fill_json("{\"error\":\"not found\"}", http::status::not_found);
                return;
            }
            std::string json;
            if (target->hasValue())
                json = "{\"path\":\"" + target->path(_p->root) + "\",\"value\":" + json::stringify(target->value()) + "}";
            else
                json = "{\"path\":\"" + target->path(_p->root) + "\",\"value\":null}";
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
            Node* target = _p->root->ensure(nodePath);
            if (!target) {
                rep.fill_json("{\"error\":\"cannot create node\"}", http::status::internal_server_error);
                return;
            }
            std::string body(req.body());
            if (!body.empty()) {
                target->set(json::parse(body));
            }
            rep.fill_json("{\"ok\":true,\"path\":\"" + target->path(_p->root) + "\"}", http::status::ok);
        });

    // GET /api/tree/*
    _p->server.bind<http::verb::get>("/api/tree/*",
        [this](http::web_request& req, http::web_response& rep) {
            auto nodePath = strip_prefix(req.path(), "/api/tree");
            Node* target = nodePath.empty() ? _p->root : _p->root->resolve(nodePath);
            if (!target) {
                rep.fill_json("{\"error\":\"not found\"}", http::status::not_found);
                return;
            }
            rep.fill_json(json::exportTree(target), http::status::ok);
        });

    // GET /api/children/*
    _p->server.bind<http::verb::get>("/api/children/*",
        [this](http::web_request& req, http::web_response& rep) {
            auto nodePath = strip_prefix(req.path(), "/api/children");
            Node* target = nodePath.empty() ? _p->root : _p->root->resolve(nodePath);
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

    // 404
    _p->server.bind_not_found(
        [](http::web_request& req, http::web_response& rep) {
            rep.fill_json("{\"error\":\"not found\"}", http::status::not_found);
        });

    bool ok = _p->server.start("0.0.0.0", _p->port);
    if (ok)
        veLogIs("HttpServer started on port", _p->port);
    else
        veLogEs("HttpServer failed to start on port", _p->port);
    return ok;
}

void HttpServer::stop()
{
    _p->server.stop();
}

bool HttpServer::isRunning() const
{
    return _p->server.is_started();
}

} // namespace ve
