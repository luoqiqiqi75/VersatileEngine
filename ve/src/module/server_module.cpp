//
// Created by luoqi on 2026/3/24.
//

#include "src/service/node_commands.h"
#include "ve/core/module.h"
#include "ve/core/log.h"
#include "ve/core/command.h"
#include "ve/service/node_service.h"
#include "ve/service/static_service.h"
#include "ve/service/bin_service.h"
#include "ve/service/terminal_service.h"

namespace ve {

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
        n->set("config/port", p);
        server = std::make_unique<T>(n->at("config"));
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

    service::registerNodeCommands();
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
