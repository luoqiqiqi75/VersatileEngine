// ----------------------------------------------------------------------------
// jsonrpc_module.cpp — JSON-RPC 2.0 Service Module
// ----------------------------------------------------------------------------
#include "ve/core/module.h"
#include "ve/service/jsonrpc_service.h"
#include "ve/core/log.h"

namespace ve {

class JsonRpcModule : public Module
{
    std::unique_ptr<JsonRpcServer> _server;

public:
    using Module::Module;

private:
    void ready() override
    {
        bool enabled = node()->get("enable").toBool(false);
        if (!enabled) {
            return;
        }

        _server = std::make_unique<JsonRpcServer>();

        int httpPort = node()->get("config/http_port").toInt(5070);
        int wsPort   = node()->get("config/ws_port").toInt(5071);
        _server->setHttpPort(static_cast<uint16_t>(httpPort));
        _server->setWsPort(static_cast<uint16_t>(wsPort));

        if (_server->start()) {
            node()->set("runtime/http_port", httpPort);
            node()->set("runtime/ws_port", wsPort);
            node()->set("runtime/listening", true);
        } else {
            node()->set("runtime/listening", false);
        }
    }

    void deinit() override
    {
        if (_server) {
            _server->stop();
            _server.reset();
        }
        node()->set("runtime/listening", false);
    }
};

}

VE_REGISTER_PRIORITY_MODULE(ve.server.jsonrpc, ve::JsonRpcModule, 50)
