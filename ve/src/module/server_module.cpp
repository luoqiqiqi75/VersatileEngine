//
// Created by luoqi on 2026/3/24.
//

#include "ve/core/module.h"
#include "ve/core/log.h"
#include "ve/core/command.h"
#include "ve/core/schema.h"
#include "ve/core/impl/json.h"
#include "ve/core/impl/bin.h"
#include "ve/core/impl/xml.h"
#include "ve/core/impl/md.h"
#include "ve/service/node_service.h"
#include "ve/service/static_service.h"
#include "ve/service/bin_service.h"
#include "ve/service/terminal_service.h"
#include <fstream>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>

namespace ve {

namespace fs = std::filesystem;

template<typename T> void openServer(std::unique_ptr<T>& server, Node* n, int default_port, const std::string& name)
{
    int port = n->get("config/port").toInt(default_port);
    int maxRetry = n->get("config/max_retry").toInt(-1);
    
    int endPort = port;
    if (maxRetry >= 0) {
        endPort = port + maxRetry;
    } else if ((port % 100) == 0) {
        endPort = port + 99;
    }

    for (int p = port; p <= endPort; ++p) {
        server = std::make_unique<T>(node::root(), static_cast<uint16_t>(p));
        if (server->start()) {
            n->set("runtime/port", p);
            n->set("runtime/listening", true);
            if (p != port) {
                veLogWs(name, "started on fallback port", p, "(default", port, "failed)");
            } else {
                veLogIs(name, "started on port", p);
            }
            return;
        }
    }
    n->set("runtime/listening", false);
    if (port == endPort) {
        veLogEs(name, "failed to start on port", port);
    } else {
        veLogEs(name, "failed to start on any port between", port, "and", endPort);
    }
}
template<typename T> void closeServer(std::unique_ptr<T>& server, Node* n)
{
    if (server) {
        server->stop();
        server.reset();
    }
    n->set("runtime/listening", false);
}

class ServerModule : public Module
{
    std::unique_ptr<service::NodeHttpServer> _node_http_s;
    std::unique_ptr<service::NodeWsServer> _node_ws_s;
    std::unique_ptr<service::NodeTcpServer> _node_tcp_s;
    std::unique_ptr<service::NodeUdpServer> _node_udp_s;
    std::unique_ptr<service::BinTcpServer> _bin_tcp_s;
    std::unique_ptr<service::TerminalReplServer> _terminal_repl_s;
    std::unique_ptr<service::TerminalReplServer> _terminal_ai_s;  // AI REPL (no banner, no current)
    std::unique_ptr<service::StaticServer> _static_s;

    std::string _data_root = "./data";

public:
    using Module::Module;

    void registerFileCommands();
    void registerSearchCommand();
    void bindStaticProxyTargets();

private:
    void init() override;
    void ready() override;
    void deinit() override;
};

template<> void openServer(std::unique_ptr<ve::service::StaticServer>& server,
                           Node* n, int default_port, const std::string& name)
{
    int port = n->get("config/port").toInt(default_port);
    int maxRetry = n->get("config/max_retry").toInt(-1);

    int endPort = port;
    if (maxRetry >= 0) {
        endPort = port + maxRetry;
    } else if ((port % 10) == 0) {
        endPort = port + 9;
    }

    Node* mounts_node = n->find("config/mounts");

    for (int p = port; p <= endPort; ++p) {
        server = std::make_unique<service::StaticServer>(static_cast<uint16_t>(p));
        if (mounts_node) {
            for (Node* mount : mounts_node->children()) {
                std::string prefix      = mount->get("prefix").toString("/");
                std::string root        = mount->get("root").toString();
                std::string default_file = mount->get("default_file").toString("index.html");
                bool spa_fallback       = mount->get("spa_fallback").toBool(false);
                if (!root.empty()) {
                    server->addMount(prefix, root, default_file, spa_fallback);
                }
                Node* proxy_node = mount->find("proxy");
                if (proxy_node) {
                    for (Node* rule : proxy_node->children()) {
                        std::string pfx = rule->get("prefix").toString();
                        std::string tgt = rule->get("target").toString();
                        if (!pfx.empty() && !tgt.empty())
                            server->addMountProxy(prefix, pfx, tgt);
                    }
                }
            }
        }
        if (server->start()) {
            n->set("runtime/port", p);
            n->set("runtime/listening", true);
            if (p != port) {
                veLogWs(name, "started on fallback port", p, "(default", port, "failed)");
            } else {
                veLogIs(name, "started on port", p);
            }
            return;
        }
    }
    n->set("runtime/listening", false);
    if (port == endPort) {
        veLogEs(name, "failed to start on port", port);
    } else {
        veLogEs(name, "failed to start on any port between", port, "and", endPort);
    }
}

void ServerModule::init() {
    const bool terminal_client_stdio = n("ve/client/terminal/stdio/enabled")->getBool(false);
    const bool terminal_client_tcp = n("ve/client/terminal/tcp/enabled")->getBool(false);
    if (terminal_client_stdio || terminal_client_tcp) {
        n("ve/server/terminal/repl/enable")->set(false);
    }

    _data_root = node()->get("file_io/data_root").toString("./data");
    registerFileCommands();
    registerSearchCommand();
}

void ServerModule::registerFileCommands()
{
    auto data_root = _data_root;

    // Declare parameter metadata for save/load
    auto* saveDecl = command::declareNode("save");
    saveDecl->at("format");
    saveDecl->at("path");
    saveDecl->at("file")->set("_short", "f");

    auto* loadDecl = command::declareNode("load");
    loadDecl->at("format");
    loadDecl->at("path");
    loadDecl->at("file")->set("_short", "f");
    loadDecl->at("inline")->set("_short", "i");

    // save <format> [path] [-f file]
    command::reg("save", [data_root](Node* ctx) -> Result {
        auto a = command::args(ctx);

        std::string format = a.string("format");
        if (format.empty()) {
            auto fmts = schema::schemaFormatNames();
            std::string out = "available formats: json, xml, bin, var";
            for (auto& fn : fmts) out += ", " + fn;
            return Result::fail(Var(out));
        }

        // Resolve target from the command context when available.
        Node* current = command::current(ctx);
        Node* base = current ? current : node::root();

        std::string pathStr = a.string("path");
        Node* target = pathStr.empty() ? base : base->find(pathStr);
        if (!target) {
            return Result::fail(Var("Node not found: " + pathStr));
        }

        std::string file = a.string("file");

        // Export
        std::string result;
        std::vector<uint8_t> binResult;
        bool isBin = false;

        if (format == "json") {
            result = impl::json::exportTree(target);
        } else if (format == "xml") {
            result = impl::xml::exportTree(target);
        } else if (format == "md") {
            result = impl::md::exportTree(target);
        } else if (format == "var") {
            result = impl::json::stringify(schema::exportAs<schema::VarS>(target)) + "\n";
        } else if (format == "bin") {
            binResult = impl::bin::exportTree(target);
            isBin = true;
        } else if (schema::hasSchemaFormat(format)) {
            result = schema::exportSchemaFormat(format, target);
        } else {
            return Result::fail(Var("Unknown format: " + format));
        }

        // Save to file or return content
        if (file.empty()) {
            if (isBin) {
                // Hex dump for bin format
                std::ostringstream oss;
                for (size_t i = 0; i < binResult.size(); ++i) {
                    if (i > 0 && i % 16 == 0) oss << "\n";
                    else if (i > 0) oss << " ";
                    oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(binResult[i]);
                }
                oss << "\n(" << std::dec << binResult.size() << " bytes)";
                return Result::ok(Var(oss.str()));
            }
            return Result::ok(Var(result));
        }

        // Save to file (relative to data_root)
        fs::path filepath = fs::path(data_root) / file;
        fs::path parent = filepath.parent_path();

        if (!parent.empty() && !fs::exists(parent)) {
            std::error_code ec;
            fs::create_directories(parent, ec);
            if (ec) {
                return Result::fail(Var("Failed to create directory: " + ec.message()));
            }
        }

        if (isBin) {
            std::ofstream ofs(filepath, std::ios::binary);
            if (!ofs.is_open()) {
                return Result::fail(Var("Cannot write: " + filepath.string()));
            }
            ofs.write(reinterpret_cast<const char*>(binResult.data()), binResult.size());
            ofs.close();
            return Result::ok(Var("Saved to " + file + " (" + std::to_string(binResult.size()) + " bytes)"));
        } else {
            std::ofstream ofs(filepath);
            if (!ofs.is_open()) {
                return Result::fail(Var("Cannot write: " + filepath.string()));
            }
            ofs << result;
            ofs.close();
            return Result::ok(Var("Saved to " + file));
        }
    }, "save <format> [path] [-f file]");

    // load <format> [path] [-f file] [-i data]
    command::reg("load", [data_root](Node* ctx) -> Result {
        auto a = command::args(ctx);

        std::string format = a.string("format");
        if (format.empty()) {
            return Result::fail(Var("Usage: load <format> [path] [-f file] [-i data]"));
        }

        // Resolve target from the command context when available.
        Node* current = command::current(ctx);
        Node* base = current ? current : node::root();

        std::string pathStr = a.string("path");
        Node* target = pathStr.empty() ? base : base->at(pathStr);

        std::string file = a.string("file");
        std::string importContent = a.string("inline");

        // Read content
        std::string content;
        if (!file.empty()) {
            fs::path filepath = fs::path(data_root) / file;
            std::ifstream ifs(filepath, format == "bin" ? std::ios::binary : std::ios::in);
            if (!ifs.is_open()) {
                return Result::fail(Var("Cannot read: " + filepath.string()));
            }
            content.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
        } else if (!importContent.empty()) {
            content = importContent;
        } else {
            return Result::fail(Var("Usage: load <format> [path] -f <file> | -i <data>"));
        }

        // Import
        bool ok = false;
        if (format == "json") {
            ok = impl::json::importTree(target, content);
        } else if (format == "xml") {
            ok = impl::xml::importTree(target, content);
        } else if (format == "md") {
            ok = impl::md::importTree(target, content);
        } else if (format == "bin") {
            ok = impl::bin::importTree(target, reinterpret_cast<const uint8_t*>(content.data()), content.size());
        } else if (schema::hasSchemaFormat(format)) {
            ok = schema::importSchemaFormat(format, target, content);
        } else {
            return Result::fail(Var("Unknown format: " + format));
        }

        if (ok) {
            return Result::ok(Var(file.empty() ? "Imported" : "Imported from " + file));
        } else {
            return Result::fail(Var("Import failed (invalid " + format + ")"));
        }
    }, "load <format> [path] [-f file] [-i data]");
}

// ============================================================================
// search command - fuzzy node tree search
// ============================================================================

static bool globMatch(const std::string& pattern, const std::string& str)
{
    size_t pi = 0, si = 0;
    size_t starP = std::string::npos, starS = 0;
    while (si < str.size()) {
        if (pi < pattern.size() && (pattern[pi] == str[si] || pattern[pi] == '?')) {
            ++pi; ++si;
        } else if (pi < pattern.size() && pattern[pi] == '*') {
            starP = pi++; starS = si;
        } else if (starP != std::string::npos) {
            pi = starP + 1; si = ++starS;
        } else {
            return false;
        }
    }
    while (pi < pattern.size() && pattern[pi] == '*') ++pi;
    return pi == pattern.size();
}

static std::string toLower(const std::string& str)
{
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

void ServerModule::registerSearchCommand()
{
    auto* decl = command::declareNode("search");
    decl->at("pattern");
    decl->at("root");
    decl->at("key")->set("_short", "k");
    decl->at("value")->set("_short", "v");
    decl->at("path")->set("_short", "p");
    decl->at("top")->set("_short", "n");
    decl->at("ignore-case")->set("_short", "i");
    decl->at("with-value")->set("_short", "w");
    decl->at("leaf-only")->set("_short", "l");

    command::reg("search", [](Node* ctx) -> Result {
        auto a = command::args(ctx);

        std::string pattern = a.string("pattern");
        if (pattern.empty()) {
            return Result::fail(Var("Usage: search <pattern> [root] [--key|--value|--path] [--ignore-case] [--top N] [--with-value] [--leaf-only]"));
        }

        std::string rootPath = a.string("root");
        bool matchKey   = a.flag("key");
        bool matchValue = a.flag("value");
        bool matchPath  = a.flag("path");
        bool ignoreCase = a.flag("ignore-case");
        bool withValue  = a.flag("with-value");
        bool leafOnly   = a.flag("leaf-only");
        int  topN       = static_cast<int>(a.integer("top", 10));

        if (!matchKey && !matchValue && !matchPath) matchKey = true;

        if (ignoreCase) pattern = toLower(pattern);

        Node* root = (rootPath.empty() || rootPath == "/")
            ? node::root()
            : node::root()->find(rootPath);
        if (!root) return Result::fail(Var("root not found: " + rootPath));

        std::string rootPrefix = (root == node::root()) ? "" : root->path();
        Var::ListV results;

        std::function<void(Node*, const std::string&)> walk =
            [&](Node* n, const std::string& currentPath) {
                if (static_cast<int>(results.size()) >= topN) return;

                for (int i = 0; i < n->count(); ++i) {
                    if (static_cast<int>(results.size()) >= topN) return;
                    Node* c = n->child(i);
                    if (c->get().isCallable()) continue;

                    std::string childPath = currentPath.empty()
                        ? c->name() : currentPath + "/" + c->name();

                    bool hit = false;
                    if (matchKey) {
                        std::string keyToMatch = ignoreCase ? toLower(c->name()) : c->name();
                        if (keyToMatch.find(pattern) != std::string::npos) hit = true;
                    }
                    if (!hit && matchValue && !c->get().isNull()) {
                        std::string valueToMatch = ignoreCase ? toLower(c->get().toString()) : c->get().toString();
                        if (valueToMatch.find(pattern) != std::string::npos) hit = true;
                    }
                    if (!hit && matchPath) {
                        std::string pathToMatch = ignoreCase ? toLower(childPath) : childPath;
                        if (globMatch(pattern, pathToMatch)) hit = true;
                    }

                    if (hit) {
                        if (leafOnly && c->count() > 0) {
                            // skip non-leaf
                        } else if (withValue) {
                            Var::DictV item;
                            item["path"] = Var(childPath);
                            item["value"] = c->get();
                            results.push_back(Var(std::move(item)));
                        } else {
                            results.push_back(Var(childPath));
                        }
                    }
                    walk(c, childPath);
                }
            };

        walk(root, rootPrefix);
        return Result::ok(Var(std::move(results)));
    }, "search <pattern> [root] [--key|--value|--path] [--ignore-case] [--top N] [--with-value] [--leaf-only]");
}

void ServerModule::bindStaticProxyTargets()
{
    if (!_static_s) return;
    Node* mounts_node = node()->find("static/config/mounts");
    if (!mounts_node) return;

    for (Node* mount : mounts_node->children()) {
        std::string prefix = mount->get("prefix").toString("/");
        Node* proxy_node = mount->find("proxy");
        if (!proxy_node) continue;

        for (Node* rule : proxy_node->children()) {
            std::string pfx = rule->get("prefix").toString();
            if (pfx.empty()) continue;

            Node* targetNode = rule->find("target", false);
            if (!targetNode) continue;

            targetNode->onChanged(this, [this, prefix, pfx](const Var& newVal, const Var&) {
                if (_static_s) {
                    _static_s->updateMountProxy(prefix, pfx, newVal.toString());
                }
            });
        }
    }
}

void ServerModule::ready() {
    if (node()->get("terminal/repl/enable").toBool(true)) openServer(_terminal_repl_s, node()->at("terminal/repl"), 10000, "TerminalReplServer");

    // AI REPL: no banner, no title, no color (save tokens), but keep cd/current (AI can handle state)
    if (node()->get("terminal/ai/enable").toBool(true)) {
        int port = node()->get("terminal/ai/config/port").toInt(10100);
        service::TerminalReplServer::Options ai_opts;
        ai_opts.banner = false;
        ai_opts.title = false;
        ai_opts.prompt_color = false;
        ai_opts.use_current = true;  // Keep cd - AI can understand navigation
        _terminal_ai_s = std::make_unique<service::TerminalReplServer>(node::root(), static_cast<uint16_t>(port), ai_opts);
        if (_terminal_ai_s->start()) {
            node()->at("terminal/ai")->set("runtime/port", port);
            node()->at("terminal/ai")->set("runtime/listening", true);
            veLogI << "TerminalReplServer(AI) started on port " << port;
        } else {
            veLogW << "TerminalReplServer(AI) failed to start on port " << port;
            _terminal_ai_s.reset();
        }
    }

    if (node()->get("bin/tcp/enable").toBool(true)) openServer(_bin_tcp_s, node()->at("bin/tcp"), 11000, "BinTcpServer");
    if (node()->get("node/http/enable").toBool(true)) openServer(_node_http_s, node()->at("node/http"), 12000, "NodeHttpServer");
    if (node()->get("node/ws/enable").toBool(true)) openServer(_node_ws_s, node()->at("node/ws"), 12100, "NodeWsServer");
    if (node()->get("node/tcp/enable").toBool(true)) openServer(_node_tcp_s, node()->at("node/tcp"), 12200, "NodeTcpServer");
    if (node()->get("node/udp/enable").toBool(true)) openServer(_node_udp_s, node()->at("node/udp"), 12300, "NodeUdpServer");
    if (node()->get("static/enable").toBool(false)) {
        openServer(_static_s, node()->at("static"), 12400, "StaticServer");
        bindStaticProxyTargets();
    }
}

void ServerModule::deinit() {
    closeServer(_node_http_s, node()->at("node/http"));
    closeServer(_node_ws_s, node()->at("node/ws"));
    closeServer(_node_tcp_s, node()->at("node/tcp"));
    closeServer(_node_udp_s, node()->at("node/udp"));
    closeServer(_bin_tcp_s, node()->at("bin/tcp"));
    closeServer(_terminal_repl_s, node()->at("terminal/repl"));
    closeServer(_terminal_ai_s, node()->at("terminal/ai"));
    closeServer(_static_s, node()->at("static"));
}

}

VE_REGISTER_PRIORITY_MODULE(ve/server, ve::ServerModule, 50)
