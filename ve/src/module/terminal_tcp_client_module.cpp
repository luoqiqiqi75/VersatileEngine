#include "ve/core/loop.h"
#include "ve/core/module.h"
#include "ve/core/log.h"
#include "ve/service/terminal_service.h"

namespace ve {

class TerminalTcpClientModule : public Module
{
    std::unique_ptr<service::TerminalTcpClient> client_;

public:
    explicit TerminalTcpClientModule(const std::string& name) : Module(name) {}

private:
    void ready() override
    {
        std::string host = node()->get("config/host").toString("127.0.0.1");
        int port = node()->get("config/port").toInt(5061);
        if (port <= 0 || port > 65535) {
            port = 5061;
        }

        client_ = std::make_unique<service::TerminalTcpClient>(host, static_cast<uint16_t>(port));

        node()->at("runtime/host")->set(Var(host));
        node()->at("runtime/port")->set(Var(port));
        node()->at("runtime/active")->set(Var(true));
        node()->at("runtime/last_error")->set(Var(""));

        loop::setMainRunner(
            [this]() -> int {
                int rc = client_ ? client_->run() : 0;
                if (client_) {
                    node()->at("runtime/last_error")->set(Var(client_->lastError()));
                }
                node()->at("runtime/active")->set(Var(false));
                return rc;
            },
            [this](int) {
                if (client_) {
                    client_->requestStop();
                }
            }
        );

        veLogI << "[ve/client/terminal/tcp] enabled -> " << host << ":" << port;
    }

    void deinit() override
    {
        if (client_) {
            node()->at("runtime/last_error")->set(Var(client_->lastError()));
            client_->requestStop();
            client_.reset();
        }
        node()->at("runtime/active")->set(Var(false));
    }
};

} // namespace ve

VE_REGISTER_PRIORITY_MODULE(ve.client.terminal.tcp, ve::TerminalTcpClientModule, 45)
