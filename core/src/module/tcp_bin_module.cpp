// tcp_bin_module.cpp - ve::TcpBinModule (ve.service.tcp_bin)

#include "ve/core/module.h"
#include "ve/core/log.h"
#include "ve/service/tcp_bin_server.h"

namespace ve {

class TcpBinModule : public Module
{
    std::unique_ptr<TcpBinServer> server_;

public:
    explicit TcpBinModule(const std::string& name) : Module(name)
    {
        // node()->ensure("config/port")->set(Var(5065));
    }

protected:
    void ready() override
    {
        uint16_t port = static_cast<uint16_t>(
            node()->resolve("config/port")->get<int>(5065));

        server_ = std::make_unique<TcpBinServer>(node::root(), port);
        if (server_->start()) {
            veLogI << "[ve.service.tcp_bin] started on port " << port;
        } else {
            veLogE << "[ve.service.tcp_bin] failed to start on port " << port;
        }
    }

    void deinit() override
    {
        if (server_) { server_->stop(); server_.reset(); }
    }
};

} // namespace ve

VE_REGISTER_PRIORITY_MODULE(ve.service.tcp_bin, ve::TcpBinModule, 50, 1)
