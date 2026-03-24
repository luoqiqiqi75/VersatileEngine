#include "ve/core/module.h"
#include "ve/core/log.h"
#include "ve/service/terminal_service.h"

namespace ve {

class ClientModule : public ve::Module
{
    std::unique_ptr<service::TerminalStdioClient> stdio_;

public:
    explicit ClientModule(const std::string& name) : ve::Module(name) {}

private:
    void ready() override;
    void deinit() override;
};

void ClientModule::ready()
{
    bool stdio_enabled = node()->at("terminal/stdio/enabled")->getBool(false);
    bool remote_enabled = node()->at("terminal/tcp/enabled")->getBool(false);
    if (stdio_enabled && remote_enabled) {
        veLogE << "[ve/client] stdio terminal and remote terminal are both enabled; remote terminal wins";
        stdio_enabled = false;
    }

    if (stdio_enabled) {
        stdio_ = std::make_unique<service::TerminalStdioClient>(node::root());
        loop::setMainRunner(
            [this]() -> int {
                while (stdio_) {
                    int rc = stdio_->run();
                    if (rc <= 0) {
                        return rc < 0 ? 1 : 0;
                    }
                }
                return 0;
            },
            [this](int) {
                if (stdio_) {
                    stdio_->requestStop();
                }
            }
        );
        node()->at("terminal/stdio/runtime/stdio")->set(Var(true));
        veLogI << "[ve/client/terminal/stdio] stdio REPL enabled";
    } else {
        node()->at("terminal/stdio/runtime/stdio")->set(Var(false));
    }
}

void ClientModule::deinit()
{
    if (stdio_) {
        stdio_->requestStop();
        stdio_.reset();
    }
    node()->at("terminal/stdio/runtime/stdio")->set(Var(false));
}

}

VE_REGISTER_PRIORITY_MODULE(ve.client, ve::ClientModule, 40)