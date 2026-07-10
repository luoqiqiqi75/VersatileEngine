// server_module.h — internal declaration of ServerModule.
//
// This header is under src/, not exported (no VE_API). It exists so the
// individual service servers (NodeHttpServer, ...) can downcast the
// module::instance("ve.server") pointer and pull the shared asio::io_context
// out of it at construction — see makeServerPool() at the bottom.
//
// Shutdown correctness note: server.stop() posts _do_stop to the io thread
// and returns without waiting (asio2 does not block on external asio
// io_contexts it doesn't own). So a server object could be destroyed before
// the io thread runs _do_stop, and the handler would then dereference freed
// memory. Fix in the module path: ServerModule::deinit() first calls stop()
// on every server (posts _do_stop), then drains its own io pool (release the
// work_guard and join the workers) so every posted handler completes against
// a still-live server, and only then destroys the server objects. In the
// standalone path (no ServerModule) the same discipline is applied by each
// server's Private dtor via AsioServerPool::drain().
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

    // Shared io_context for every asio2 server ve owns. Only valid while the
    // module is alive; ServerModule guarantees, in deinit(), that io threads
    // are drained before any server object is destroyed.
    asio::io_context& sharedIoContext() { return _pool.io(); }

    void bindStaticProxyTargets();

private:
    void init() override;
    void prepare() override;
    void ready() override;
    void deinit() override;

    // Own-mode pool: private io_context + worker threads. Declared before
    // the server unique_ptrs so member destruction (reverse of declaration
    // order) has the servers destroyed first, then the pool — a safety net
    // for exit paths that skip deinit(). deinit() itself enforces the
    // correct order explicitly.
    service::AsioServerPool _pool{4};

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

// Called by each individual Server's Private ctor. If ServerModule is
// registered, returns an AsioServerPool that shares the module's io_context
// (drain() will be a no-op — the module drains). Otherwise returns an
// own-mode pool with its own io_context + workers.
inline service::AsioServerPool makeServerPool()
{
    if (auto* m = module::instance<ServerModule>("ve.server"))
        return service::AsioServerPool(m->sharedIoContext());
    return service::AsioServerPool{};
}

} // namespace ve
