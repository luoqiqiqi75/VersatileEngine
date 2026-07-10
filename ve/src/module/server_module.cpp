//
// Created by luoqi on 2026/3/24.
//

#include "src/module/server_module.h"

#include "ve/core/log.h"
#include "ve/core/command.h"
#include "ve/core/res.h"
#include "ve/entry.h"

#include "src/service/node_commands.h"
#include "src/service/cmd_commands.h"
#include "src/service/terminal_session.h"

namespace ve {

template<typename T> void openServer(std::unique_ptr<T>& server, Node* n, int default_port,
                                     const std::string& name)
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
        n->set("config/port", p);
        server = std::make_unique<T>(n->at("config"));
        if (server->start()) {
            n->set("runtime/port", p);
            n->set("runtime/listening", true);
            if (p != port) {
                veLogIs(name, "started on fallback port", p, "(default", port, "failed)");
            } else if (entry::verbose()) {
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

// Ask the server to stop but don't destroy it yet — io threads must drain any
// _do_stop handler first, before the server object goes away.
template<typename T> void stopServer(std::unique_ptr<T>& server, Node* n)
{
    if (server) server->stop();
    n->set("runtime/listening", false);
}

ServerModule::ServerModule()
{
    { // register op commands
        auto& f = factory::at("service/op");
        schema::JsonS::toNode(f.node(), std::string(res::read("ve/service/op.json")));
        service::registerNodeCommands(f);
    }

    { // register repl commands
        auto& f = factory::at("service/repl");
        schema::JsonS::toNode(f.node(), std::string(res::read("ve/service/repl.json")));
        service::registerReplCommands(f);
    }

    { // register cmd commands (save/load)
        auto& f = factory::at("cmd");
        schema::JsonS::toNode(f.node(), std::string(res::read("ve/service/cmd.json")));
        service::registerCmdCommands(f);
    }
}

ServerModule::~ServerModule() = default;

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
                veLogIs(name, "started on fallback port", p, "(default", port, "failed)");
            } else if (entry::verbose()) {
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
    _data_root = node()->get("file_io/data_root").toString("./data");
}

void ServerModule::prepare() {
    // Resolve cross-module config: if a terminal client is enabled, force the
    // matching server-side REPL off (mutually exclusive, client wins).
    const bool terminal_client_stdio = n("ve/client/terminal/stdio/enabled")->getBool(false);
    const bool terminal_client_tcp = n("ve/client/terminal/tcp/enabled")->getBool(false);
    if (terminal_client_stdio || terminal_client_tcp) {
        n("ve/server/terminal/repl/enable")->set(false);
    }
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

            Node* targetNode = rule->find("target");
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
    // Human REPL: banner, title, color (if enabled, can be disabled for token saving)
    if (node()->get("terminal/repl/enable").toBool(true)) {
        auto repl_config_n = node()->at("terminal/repl/config");
        repl_config_n->set("banner", true);
        repl_config_n->set("title", true);
        repl_config_n->set("prompt_color", true);
        openServer(_terminal_repl_s, node()->at("terminal/repl"), 10000, "TerminalReplServer");
    }

    // AI REPL: no banner, no title, no color (save tokens), but keep cd/current (AI can handle state)
    if (node()->get("terminal/ai/enable").toBool(true)) {
        auto ai_config_n = node()->at("terminal/ai/config");
        ai_config_n->set("banner", false);
        ai_config_n->set("title", false);
        ai_config_n->set("prompt_color", false);
        openServer(_terminal_ai_s, node()->at("terminal/ai"), 10100, "TerminalAiServer");
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
    // Step 1: stop every server. Each stop() posts _do_stop onto the shared
    // io thread and returns — the actual shutdown chain runs on the io thread
    // and still references the live server object.
    stopServer(_node_http_s, node()->at("node/http"));
    stopServer(_node_ws_s, node()->at("node/ws"));
    stopServer(_node_tcp_s, node()->at("node/tcp"));
    stopServer(_node_udp_s, node()->at("node/udp"));
    stopServer(_bin_tcp_s, node()->at("bin/tcp"));
    stopServer(_terminal_repl_s, node()->at("terminal/repl"));
    stopServer(_terminal_ai_s, node()->at("terminal/ai"));
    stopServer(_static_s, node()->at("static"));

    // Step 2: drain the shared io pool. Releasing the work_guard drops the
    // idle-hold, and joining the workers waits for every posted handler
    // (including _do_stop) to complete — all against still-live server
    // objects. This is the shutdown barrier.
    _pool.drain();

    // Step 3: destroy the server objects. No handler can reference them now.
    _node_http_s.reset();
    _node_ws_s.reset();
    _node_tcp_s.reset();
    _node_udp_s.reset();
    _bin_tcp_s.reset();
    _terminal_repl_s.reset();
    _terminal_ai_s.reset();
    _static_s.reset();
}

}

VE_REGISTER_PRIORITY_MODULE(ve.server, ve::ServerModule, 50)
