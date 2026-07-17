// server_module.h — internal declaration of ve.server ServerModule.
//
// Not exported (under src/, no VE_API). Only server_module.cpp needs the full
// class; asio2-specific ownership lives in service::ServerRuntime.
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

    // Owns the shared asio2 pool and every wrapper constructed on it. Declared
    // before module references so reverse member destruction releases those
    // references first; shutdown() is also called explicitly by deinit().
    service::ServerRuntime _runtime{4};

    std::shared_ptr<service::NodeHttpServer> _node_http_s;
    std::shared_ptr<service::NodeWsServer> _node_ws_s;
    std::shared_ptr<service::NodeTcpServer> _node_tcp_s;
    std::shared_ptr<service::NodeUdpServer> _node_udp_s;
    std::shared_ptr<service::BinTcpServer> _bin_tcp_s;
    std::shared_ptr<service::TerminalReplServer> _terminal_repl_s;
    std::shared_ptr<service::TerminalReplServer> _terminal_ai_s;
    std::shared_ptr<service::StaticServer> _static_s;

    std::string _data_root = "./data";
};

} // namespace ve
