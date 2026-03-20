// terminal_module.cpp - ve::TerminalModule (ve.service.terminal)

#include "ve/core/module.h"
#include "ve/core/log.h"
#include "ve/service/terminal.h"

namespace ve {

class TerminalModule : public Module
{
    std::unique_ptr<Terminal> server_;

public:
    explicit TerminalModule(const std::string& name) : Module(name)
    {
        // node()->at("config/port")->set(Var(5061));
    }

protected:
    void ready() override
    {
        uint16_t port = static_cast<uint16_t>(
            node()->at("config/port")->getInt(5061));

        server_ = std::make_unique<Terminal>(node::root(), port);
        if (server_->start()) {
            veLogI << "[ve.service.terminal] started on port " << server_->port();
            node()->at("runtime/port")->set(Var(static_cast<int64_t>(server_->port())));
            node()->at("runtime/listening")->set(Var(true));
        } else {
            veLogE << "[ve.service.terminal] failed to start on port " << port;
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

VE_REGISTER_PRIORITY_MODULE(ve.service.terminal, ve::TerminalModule, 50, 1)
