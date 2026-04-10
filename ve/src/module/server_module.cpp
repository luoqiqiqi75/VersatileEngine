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
#include "ve/service/node_service.h"
#include "ve/service/bin_service.h"
#include "ve/service/terminal_service.h"
#include <fstream>
#include <filesystem>
#include <sstream>
#include <iomanip>

namespace ve {

namespace fs = std::filesystem;

template<typename T> void openServer(std::unique_ptr<T>& server, Node* n, int default_port)
{
    int port = n->get("config/port").toInt(default_port);
    int maxRetry = n->get("config/max_retry").toInt(100);

    for (int i = 0; i <= maxRetry; ++i) {
        server = std::make_unique<T>(node::root(), static_cast<uint16_t>(port + i));
        if (server->start()) {
            n->set("runtime/port", port + i);
            n->set("runtime/listening", true);
            return;
        }
    }
    n->set("runtime/listening", false);
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

    std::string _data_root = "./data";

public:
    using Module::Module;

    void registerFileCommands();

private:
    void init() override;
    void ready() override;
    void deinit() override;
};

template<> void openServer(std::unique_ptr<ve::service::NodeHttpServer>& server, Node* n, int default_port)
{
    int port = n->get("config/port").toInt(default_port);
    int maxRetry = n->get("config/max_retry").toInt(100);

    std::string static_root = n->get("config/static_root").toString();
    std::string default_file = n->get("config/default_file").toString();

    // Proxy rules: config/proxy = [{ "prefix": "/foo", "target": "https://..." }, ...]
    Node* proxy_node = n->find("config/proxy");

    for (int i = 0; i <= maxRetry; ++i) {
        server = std::make_unique<service::NodeHttpServer>(node::root(), static_cast<uint16_t>(port + i));
        if (!static_root.empty()) {
            veLogD << "[ve/service/node/http] static root: " << static_root;
            server->setStaticRoot(static_root);
        }
        if (!default_file.empty()) server->setDefaultFile(default_file);
        if (proxy_node) {
            for (Node* rule : proxy_node->children()) {
                std::string pfx = rule->get("prefix").toString();
                std::string tgt = rule->get("target").toString();
                if (!pfx.empty() && !tgt.empty())
                    server->addProxyRule(pfx, tgt);
            }
        }
        if (server->start()) {
            n->set("runtime/port", port + i);
            n->set("runtime/listening", true);
            return;
        }
    }
    n->set("runtime/listening", false);
}

void ServerModule::init() {
    const bool terminal_client_stdio = n("ve/client/terminal/stdio/enabled")->getBool(false);
    const bool terminal_client_tcp = n("ve/client/terminal/tcp/enabled")->getBool(false);
    if (terminal_client_stdio || terminal_client_tcp) {
        n("ve/server/terminal/repl/enable")->set(false);
    }

    _data_root = node()->get("file_io/data_root").toString("./data");
    registerFileCommands();
}

void ServerModule::registerFileCommands()
{
    auto data_root = _data_root;

    // Declare parameter metadata for save/load
    // Positional params: ordered children without _short
    // Named params: children with _short
    auto* saveDecl = node::root()->at("ve/command/declare/save");
    saveDecl->at("format");
    saveDecl->at("path");
    saveDecl->at("file")->set("_short", "f");

    auto* loadDecl = node::root()->at("ve/command/declare/load");
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

void ServerModule::ready() {
    if (node()->get("node/http/enable").toBool(true)) openServer(_node_http_s, node()->at("node/http"), 12000);
    if (node()->get("node/ws/enable").toBool(true)) openServer(_node_ws_s, node()->at("node/ws"), 12100);
    if (node()->get("node/tcp/enable").toBool(true)) openServer(_node_tcp_s, node()->at("node/tcp"), 12200);
    if (node()->get("node/udp/enable").toBool(true)) openServer(_node_udp_s, node()->at("node/udp"), 12300);
    if (node()->get("bin/tcp/enable").toBool(true)) openServer(_bin_tcp_s, node()->at("bin/tcp"), 11000);
    if (node()->get("terminal/repl/enable").toBool(true)) openServer(_terminal_repl_s, node()->at("terminal/repl"), 10000);
}

void ServerModule::deinit() {
    closeServer(_node_http_s, node()->at("node/http"));
    closeServer(_node_ws_s, node()->at("node/ws"));
    closeServer(_node_tcp_s, node()->at("node/tcp"));
    closeServer(_node_udp_s, node()->at("node/udp"));
    closeServer(_bin_tcp_s, node()->at("bin/tcp"));
    closeServer(_terminal_repl_s, node()->at("terminal/repl"));
}

}

VE_REGISTER_PRIORITY_MODULE(ve/server, ve::ServerModule, 50)
