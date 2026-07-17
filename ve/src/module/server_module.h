// server_module.h — internal declaration of ve.server ServerModule.
//
// Not exported (under src/, no VE_API). Only server_module.cpp needs to see
// the full class. Other TUs get to the shared iopool through the free
// function sharedIopool() declared in server_util.h.
#pragma once

#include "ve/core/module.h"
#include "ve/service/node_service.h"
#include "ve/service/static_service.h"
#include "ve/service/bin_service.h"
#include "ve/service/terminal_service.h"
#include "src/service/server_util.h"

#include <memory>
#include <string>

namespace ve {

class ServerModule : public Module
{
public:
    ServerModule();
    ~ServerModule() override;

    void bindStaticProxyTargets();

private:
    void init() override;
    void prepare() override;
    void ready() override;
    void deinit() override;

    // The one iopool every ve asio2 server uses. Started in the ctor and
    // stopped by deinit() while all registered server objects are still
    // alive; the dtor repeats stop() as an idempotent fallback.
    //
    // Declared before the server unique_ptrs so member destruction (reverse
    // of declaration order) tears servers down first, iopool last — a
    // safety net for exit paths that skip deinit(). deinit() explicitly
    // enforces the same order.
    asio2::iopool _iopool{4};

    std::unique_ptr<service::NodeHttpServer> _node_http_s;
    std::unique_ptr<service::NodeWsServer> _node_ws_s;
    std::unique_ptr<service::NodeTcpServer> _node_tcp_s;
    std::unique_ptr<service::NodeUdpServer> _node_udp_s;
    std::unique_ptr<service::BinTcpServer> _bin_tcp_s;
    std::unique_ptr<service::TerminalReplServer> _terminal_repl_s;
    std::unique_ptr<service::TerminalReplServer> _terminal_ai_s;
    std::unique_ptr<service::StaticServer> _static_s;

    std::string _data_root = "./data";
};

} // namespace ve
