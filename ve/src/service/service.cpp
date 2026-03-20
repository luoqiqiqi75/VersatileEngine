// service.cpp — ve::service unified start/stop
#include "ve/service/service.h"
#include "ve/service/http_server.h"
#include "ve/service/ws_server.h"
#include "ve/service/terminal.h"
#include "ve/service/tcp_bin_server.h"
#include "ve/core/command.h"
#include "ve/core/log.h"

namespace ve::service {

static std::unique_ptr<HttpServer>    g_http;
static std::unique_ptr<WsServer>     g_ws;
static std::unique_ptr<Terminal>      g_terminal;
static std::unique_ptr<TcpBinServer> g_tcpBin;

void startAll(const Config& cfg)
{
    veLogIs("ve::service::startAll  http:", cfg.http.port,
            "(", cfg.http.enabled ? "on" : "off", ")",
            "ws:", cfg.ws.port,
            "(", cfg.ws.enabled ? "on" : "off", ")",
            "tcp_text:", cfg.tcp_text.port,
            "(", cfg.tcp_text.enabled ? "on" : "off", ")",
            "tcp_bin:", cfg.tcp_bin.port,
            "(", cfg.tcp_bin.enabled ? "on" : "off", ")");

    command::initBuiltins();

    if (cfg.tcp_text.enabled) {
        g_terminal = std::make_unique<Terminal>(cfg.root, cfg.tcp_text.port);
        g_terminal->start();
    }

    if (cfg.http.enabled) {
        g_http = std::make_unique<HttpServer>(cfg.root, cfg.http.port);
        g_http->start();
    }

    if (cfg.ws.enabled) {
        g_ws = std::make_unique<WsServer>(cfg.root, cfg.ws.port);
        g_ws->start();
    }

    if (cfg.tcp_bin.enabled) {
        g_tcpBin = std::make_unique<TcpBinServer>(cfg.root, cfg.tcp_bin.port);
        g_tcpBin->start();
    }
}

void stopAll()
{
    veLogI("ve::service::stopAll");
    if (g_tcpBin)   { g_tcpBin->stop();   g_tcpBin.reset(); }
    if (g_ws)       { g_ws->stop();       g_ws.reset(); }
    if (g_http)     { g_http->stop();     g_http.reset(); }
    if (g_terminal) { g_terminal->stop(); g_terminal.reset(); }
}

void startAll(Node* root, uint16_t http_port, uint16_t ws_port, uint16_t tcp_port)
{
    Config cfg;
    cfg.root = root;
    cfg.http.port     = http_port;
    cfg.ws.port       = ws_port;
    cfg.tcp_text.port = tcp_port;
    cfg.tcp_bin.enabled = false;
    startAll(cfg);
}

} // namespace ve::service
