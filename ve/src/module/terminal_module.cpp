// terminal_module.cpp - ve::TerminalModule (ve.service.terminal)

#include "ve/core/module.h"
#include "ve/core/log.h"
#include "ve/core/loop.h"
#include "ve/service/terminal_service.h"

namespace ve {

class TerminalModule : public Module
{
    std::unique_ptr<service::TerminalReplServer> server_;
    std::unique_ptr<service::TerminalStdioClient> stdio_;

public:
    explicit TerminalModule(const std::string& name) : Module(name)
    {
    }

protected:
    void ready() override
    {
        bool stdio_enabled = node()->at("config/stdio/enabled")->getBool(false);
        if (stdio_enabled) {
            stdio_ = std::make_unique<service::TerminalStdioClient>(node::root());
            loop::setMainRunner(
                [this]() -> int { return stdio_ ? stdio_->run() : 0; },
                [this](int) {
                    if (stdio_) {
                        stdio_->requestStop();
                    }
                }
            );
            node()->at("runtime/stdio")->set(Var(true));
            veLogI << "[ve.service.terminal] stdio REPL enabled";
        } else {
            node()->at("runtime/stdio")->set(Var(false));
        }

        uint16_t port = static_cast<uint16_t>(node()->at("config/port")->getInt(5061));

        server_ = std::make_unique<service::TerminalReplServer>(node::root(), port);
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
        if (stdio_) {
            stdio_->requestStop();
            stdio_.reset();
        }
        node()->at("runtime/stdio")->set(Var(false));

        if (server_) {
            server_->stop();
            server_.reset();
        }
        node()->at("runtime/listening")->set(Var(false));
    }
};

} // namespace ve

VE_REGISTER_PRIORITY_MODULE(ve.service.terminal, ve::TerminalModule, 50, 1)
