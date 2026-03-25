//
// Created by luoqi on 2026/3/24.
//

#include "ve/core/module.h"
#include "ve/core/log.h"
#include "ve/service/node_service.h"
#include "ve/service/bin_service.h"
#include "ve/service/terminal_service.h"

namespace ve {

template<typename T> void openServer(std::unique_ptr<T>& server, Node* n, int default_port)
{
    int port = n->get("config/port").toInt(default_port);

    server = std::make_unique<T>(node::root(), port);

    if (server->start()) { // self logging
        n->set("runtime/port", port);
        n->set("runtime/listening", true);
    } else {
        n->set("runtime/listening", false);
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

    std::unique_ptr<service::BinTcpServer> _bin_tcp_s;

    std::unique_ptr<service::TerminalReplServer> _terminal_repl_s;

public:
    using Module::Module;

private:
    void init() override;
    void ready() override;
    void deinit() override;
};

template<> void openServer(std::unique_ptr<ve::service::NodeHttpServer>& server, Node* n, int default_port)
{
    int port = n->get("config/port").toInt(default_port);

    server = std::make_unique<service::NodeHttpServer>(node::root(), port);

    std::string static_root = n->get("config/static_root").toString();
    if (!static_root.empty()) {
        veLogD << "[ve/service/node/http] static root: " << static_root;
        server->setStaticRoot(static_root);
    }

    std::string default_file = n->get("config/default_file").toString();
    if (!default_file.empty()) server->setDefaultFile(default_file);

    if (server->start()) {
        n->set("runtime/port", port);
        n->set("runtime/listening", true);
    } else {
        n->set("runtime/listening", false);
    }
}

void ServerModule::init() {
    const bool terminal_client_stdio = n("ve/client/terminal/stdio/enabled")->getBool(false);
    const bool terminal_client_tcp = n("ve/client/terminal/tcp/enabled")->getBool(false);
    if (terminal_client_stdio || terminal_client_tcp) {
        n("ve/server/terminal/repl/enable")->set(false);
    }
}

void ServerModule::ready() {
    if (node()->get("node/http/enable").toBool(true)) openServer(_node_http_s, node()->at("node/http"), 8080);
    if (node()->get("node/ws/enable").toBool(true)) openServer(_node_ws_s, node()->at("node/ws"), 8081);
    if (node()->get("bin/tcp/enable").toBool(true)) openServer(_bin_tcp_s, node()->at("bin/tcp"), 5065);
    if (node()->get("terminal/repl/enable").toBool(true)) openServer(_terminal_repl_s, node()->at("terminal/repl"), 5061);
}

void ServerModule::deinit() {
    closeServer(_node_http_s, node()->at("node/http"));
    closeServer(_node_ws_s, node()->at("node/ws"));
    closeServer(_bin_tcp_s, node()->at("bin/tcp"));
    closeServer(_terminal_repl_s, node()->at("terminal/repl"));
}

}

VE_REGISTER_PRIORITY_MODULE(ve/server, ve::ServerModule, 50)
