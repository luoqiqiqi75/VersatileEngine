// ws_module.cpp - ve::WsModule (ve.service.ws)

#include "ve/core/module.h"
#include "ve/core/log.h"
#include "ve/service/ws_server.h"

namespace ve {

class WsModule : public Module
{
    std::unique_ptr<WsServer> server_;

public:
    explicit WsModule(const std::string& name) : Module(name)
    {
        // node()->at("config/port")->set(Var(8081));
    }

protected:
    void ready() override
    {
        uint16_t port = static_cast<uint16_t>(
            node()->at("config/port")->getInt(8081));

        server_ = std::make_unique<WsServer>(node::root(), port);
        if (server_->start()) {
            veLogI << "[ve.service.ws] started on port " << port;
            node()->at("runtime/port")->set(Var(static_cast<int64_t>(port)));
            node()->at("runtime/listening")->set(Var(true));
        } else {
            veLogE << "[ve.service.ws] failed to start on port " << port;
        }
    }

    void deinit() override
    {
        if (server_) {
            server_->stop();
            server_.reset();
        }
        if (Node* ln = node()->find("runtime/listening")) {
            ln->set(Var(false));
        }
    }
};

} // namespace ve

VE_REGISTER_PRIORITY_MODULE(ve.service.ws, ve::WsModule, 50, 1)
