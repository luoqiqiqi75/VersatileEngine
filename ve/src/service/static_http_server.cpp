// static_http_server.cpp — ve::service::StaticServer
#include "ve/service/static_service.h"
#include "ve/core/log.h"
#include "server_util.h"

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <asio2/http/http_server.hpp>
#include <asio2/http/http_client.hpp>
#include <asio2/http/detail/http_url.hpp>
#if defined(ASIO2_ENABLE_SSL) || defined(ASIO2_USE_SSL)
#include <asio2/http/https_client.hpp>
#endif
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ve {
namespace service {

// ============================================================================
// Helpers
// ============================================================================

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
        if (it != mimeTypes().end()) return it->second;
    }
    return "application/octet-stream";
}

static bool isBrowserOptionalProbe(const std::string& rel)
{
    std::string s = rel;
    for (char& c : s) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
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
// Private
// ============================================================================

struct StaticServer::Private
{
    uint16_t port = 12400;
    asio2::http_server server;

    struct ProxyRule {
        std::string prefix;
        std::string targetHost;
        std::string targetPort;
        std::string targetPath;
        bool        isHttps = false;
    };

    struct Mount {
        std::string prefix;
        std::string root;
        std::string defaultFile = "index.html";
        bool spaFallback = false;
        std::vector<ProxyRule> proxyRules;
    };

    std::vector<Mount> mounts; // sorted by prefix length descending (longest first)

    Mount* findMount(const std::string& reqPath);
    bool tryProxy(const Mount& mount, const std::string& relPath,
                  http::web_request& req, http::web_response& rep);
    bool tryServeFile(const Mount& mount, const std::string& relPath,
                      http::web_response& rep);
};

StaticServer::Private::Mount* StaticServer::Private::findMount(const std::string& reqPath)
{
    for (auto& m : mounts) {
        if (m.prefix == "/" || m.prefix.empty()) continue; // root mount checked last
        if (reqPath.size() >= m.prefix.size() &&
            reqPath.substr(0, m.prefix.size()) == m.prefix &&
            (reqPath.size() == m.prefix.size() || reqPath[m.prefix.size()] == '/')) {
            return &m;
        }
    }
    // fall back to root mount
    for (auto& m : mounts) {
        if (m.prefix == "/" || m.prefix.empty()) return &m;
    }
    return nullptr;
}

bool StaticServer::Private::tryProxy(const Mount& mount, const std::string& relPath,
                                     http::web_request& req, http::web_response& rep)
{
    std::string reqTarget = std::string(req.target());

    for (const auto& rule : mount.proxyRules) {
        if (relPath.size() < rule.prefix.size()) continue;
        if (relPath.substr(0, rule.prefix.size()) != rule.prefix) continue;
        if (relPath.size() > rule.prefix.size() && relPath[rule.prefix.size()] != '/') continue;

        if (rule.targetHost.empty()) continue;

        // Build upstream target: strip mount prefix from original target, then apply proxy
        std::string afterMount = reqTarget.substr(mount.prefix == "/" ? 0 : mount.prefix.size());
        std::string remaining = afterMount.substr(rule.prefix.size());
        std::string upstreamTarget = rule.targetPath + remaining;
        if (upstreamTarget.empty() || upstreamTarget[0] != '/') upstreamTarget = "/" + upstreamTarget;

        http::web_request upstream;
        upstream.method(req.method());
        upstream.target(upstreamTarget);
        upstream.version(11);

        static const std::vector<std::string> hopByHop = {
            "host", "connection", "keep-alive", "transfer-encoding",
            "te", "trailer", "upgrade", "proxy-authorization"
        };
        for (auto const& field : req.base()) {
            std::string nameLower(field.name_string());
            for (auto& c : nameLower) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
            bool skip = false;
            for (const auto& h : hopByHop) { if (nameLower == h) { skip = true; break; } }
            if (!skip) upstream.set(field.name_string(), field.value());
        }
        upstream.set(http::field::host, rule.targetHost);
        upstream.body() = std::string(req.body());
        upstream.prepare_payload();

        auto timeout = std::chrono::seconds(30);
        http::web_response upstream_resp;

        if (rule.isHttps) {
#if defined(ASIO2_ENABLE_SSL) || defined(ASIO2_USE_SSL)
            upstream_resp = asio2::https_client::execute(
                rule.targetHost, rule.targetPort, upstream, timeout);
#else
            veLogWs("[static proxy] HTTPS proxy not compiled in:", rule.prefix);
            rep.fill_text("HTTPS proxy not supported in this build", http::status::not_implemented);
            return true;
#endif
        } else {
            upstream_resp = asio2::http_client::execute(
                rule.targetHost, rule.targetPort, upstream, timeout);
        }

        if (asio2::get_last_error()) {
            veLogWs("[static proxy] upstream error:", rule.prefix, "->",
                    rule.targetHost, ":", asio2::last_error_msg());
            rep.fill_text("Proxy upstream error: " + asio2::last_error_msg(),
                          http::status::bad_gateway);
            return true;
        }

        rep.result(upstream_resp.result());
        for (auto const& field : upstream_resp.base()) {
            std::string nameLower(field.name_string());
            for (auto& c : nameLower) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
            if (nameLower == "connection" || nameLower == "keep-alive" ||
                nameLower == "transfer-encoding" || nameLower == "te" ||
                nameLower == "trailer" || nameLower == "upgrade")
                continue;
            rep.set(field.name_string(), field.value());
        }
        rep.body() = upstream_resp.body();
        rep.prepare_payload();
        return true;
    }
    return false;
}

bool StaticServer::Private::tryServeFile(const Mount& mount, const std::string& relPath,
                                         http::web_response& rep)
{
    if (mount.root.empty()) return false;

    std::string rel = relPath;
    {
        const auto cut = rel.find_first_of("?#");
        if (cut != std::string::npos) rel.erase(cut);
    }
    while (!rel.empty() && rel[0] == '/') rel.erase(rel.begin());
    if (rel.empty()) rel = mount.defaultFile;

    if (!isPathSafe(rel)) return false;

    namespace fs = std::filesystem;
    const fs::path full = (fs::path(mount.root) / rel).lexically_normal();

    std::error_code ec;
    if (!fs::is_regular_file(full, ec)) {
        if (!isBrowserOptionalProbe(rel)) {
            veLogWs("[static] file missing:", full.string(), ec ? ec.message().c_str() : "");
        }
        return false;
    }

    std::string content = readFileBytes(full);
    if (content.empty()) {
        const auto sz = fs::file_size(full, ec);
        if (!ec && sz > 0) {
            veLogWs("[static] file read failed:", full.string());
            return false;
        }
    }

    rep.fill_text(std::move(content), http::status::ok, mimeForPath(rel));
    return true;
}

// ============================================================================
// StaticServer
// ============================================================================

StaticServer::StaticServer(uint16_t port)
    : _p(std::make_unique<Private>())
{
    _p->port = port;
}

StaticServer::~StaticServer()
{
    stop();
}

void StaticServer::addMount(const std::string& prefix, const std::string& root,
                            const std::string& defaultFile, bool spaFallback)
{
    Private::Mount m;
    m.prefix      = prefix;
    m.root        = root;
    m.defaultFile = defaultFile.empty() ? "index.html" : defaultFile;
    m.spaFallback = spaFallback;
    _p->mounts.push_back(std::move(m));
    // keep sorted: longest prefix first, root "/" last
    std::stable_sort(_p->mounts.begin(), _p->mounts.end(),
        [](const Private::Mount& a, const Private::Mount& b) {
            if (a.prefix == "/" || a.prefix.empty()) return false;
            if (b.prefix == "/" || b.prefix.empty()) return true;
            return a.prefix.size() > b.prefix.size();
        });
    veLogDs("[static] mount:", prefix, "->", root);
}

void StaticServer::addMountProxy(const std::string& mountPrefix,
                                 const std::string& proxyPrefix,
                                 const std::string& target)
{
    for (auto& m : _p->mounts) {
        if (m.prefix != mountPrefix) continue;
        http::url u(target);
        Private::ProxyRule rule;
        rule.prefix     = proxyPrefix;
        rule.targetHost = std::string(u.host());
        rule.targetPort = std::string(u.port());
        rule.targetPath = std::string(u.path());
        rule.isHttps    = asio2::iequals(u.schema(), "https");
        while (rule.targetPath.size() > 1 && rule.targetPath.back() == '/')
            rule.targetPath.pop_back();
        veLogDs("[static proxy] mount", mountPrefix, ":", proxyPrefix, "->", target);
        m.proxyRules.push_back(std::move(rule));
        return;
    }
    veLogWs("[static proxy] mount not found:", mountPrefix);
}

bool StaticServer::updateMountProxy(const std::string& mountPrefix,
                                    const std::string& proxyPrefix,
                                    const std::string& target)
{
    for (auto& m : _p->mounts) {
        if (m.prefix != mountPrefix) continue;
        for (auto& rule : m.proxyRules) {
            if (rule.prefix != proxyPrefix) continue;
            if (target.empty()) {
                rule.targetHost.clear();
                rule.targetPort.clear();
                rule.targetPath.clear();
                rule.isHttps = false;
            } else {
                http::url u(target);
                rule.targetHost = std::string(u.host());
                rule.targetPort = std::string(u.port());
                rule.targetPath = std::string(u.path());
                rule.isHttps    = asio2::iequals(u.schema(), "https");
                while (rule.targetPath.size() > 1 && rule.targetPath.back() == '/')
                    rule.targetPath.pop_back();
            }
            veLogDs("[static proxy] target updated:", proxyPrefix, "->", target);
            return true;
        }
        return false;
    }
    return false;
}

bool StaticServer::start()
{
    _p->server.bind_not_found(
        [this](http::web_request& req, http::web_response& rep) {
            std::string reqPath = std::string(req.path());

            Private::Mount* mount = _p->findMount(reqPath);
            if (!mount) {
                rep.fill_text("Not Found", http::status::not_found);
                return;
            }

            // Compute path relative to mount prefix
            std::string relPath = reqPath;
            if (mount->prefix != "/" && !mount->prefix.empty()) {
                relPath = reqPath.substr(mount->prefix.size());
            }

            // 1. Proxy
            if (_p->tryProxy(*mount, relPath, req, rep)) return;
            // 2. Static file
            if (_p->tryServeFile(*mount, relPath, rep)) return;
            // 3. SPA fallback
            if (mount->spaFallback) {
                namespace fs = std::filesystem;
                const fs::path full = (fs::path(mount->root) / mount->defaultFile).lexically_normal();
                std::error_code ec;
                if (fs::is_regular_file(full, ec)) {
                    std::string content = readFileBytes(full);
                    rep.fill_text(std::move(content), http::status::ok,
                                  mimeForPath(mount->defaultFile));
                    return;
                }
            }
            rep.fill_text("Not Found", http::status::not_found);
        });

    ve::service::disableWindowsPortReuse(_p->server);
    return _p->server.start("0.0.0.0", _p->port);
}

void StaticServer::stop()
{
    _p->server.stop();
}

bool StaticServer::isRunning() const
{
    return _p->server.is_started();
}

} // namespace service
} // namespace ve
