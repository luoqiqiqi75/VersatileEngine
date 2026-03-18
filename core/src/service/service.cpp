// service.cpp — ve::service unified start/stop
#include "ve/service/service.h"
#include "ve/service/http_server.h"
#include "ve/service/ws_server.h"
#include "ve/service/terminal_server.h"
#include "ve/core/log.h"

namespace ve::service {

static std::unique_ptr<HttpServer>      g_http;
static std::unique_ptr<WsServer>        g_ws;
static std::unique_ptr<TerminalServer>  g_terminal;

void startAll(Node* root, uint16_t http_port, uint16_t ws_port, uint16_t tcp_port)
{
    veLogIs("ve::service::startAll  http:", http_port, "ws:", ws_port, "tcp:", tcp_port);

    g_terminal = std::make_unique<TerminalServer>(root, tcp_port);
    g_terminal->start();

    g_http = std::make_unique<HttpServer>(root, http_port);
    g_http->start();

    g_ws = std::make_unique<WsServer>(root, ws_port);
    g_ws->start();
}

void stopAll()
{
    veLogI("ve::service::stopAll");
    if (g_ws)       { g_ws->stop();       g_ws.reset(); }
    if (g_http)     { g_http->stop();     g_http.reset(); }
    if (g_terminal) { g_terminal->stop(); g_terminal.reset(); }
}

} // namespace ve::service
