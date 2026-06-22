#include "ve/entry.h"
#include "ve/core/loop.h"
#include "ve/core/module.h"
#include "ve/core/log.h"
#include "ve/service/terminal_service.h"

namespace ve {

// Blocking REPL clients own the process main loop: exec() runs the client
// until it exits, quit() asks it to stop.
class TerminalClientLoop : public Loop
{
    std::function<int()>  run_;
    std::function<void()> stop_;

public:
    TerminalClientLoop(std::function<int()> run, std::function<void()> stop)
        : Loop("terminal"), run_(std::move(run)), stop_(std::move(stop)) {}

    bool isRunning() const override { return true; }

    int exec() override
    {
        if (_quit.exchange(false)) return _exit_code.load();
        return run_ ? run_() : 0;
    }

    void quit(int exit_code) override
    {
        Loop::quit(exit_code);
        if (stop_) stop_();
    }
};

class ClientModule : public ve::Module
{
    std::unique_ptr<service::TerminalStdioClient> stdio_;
    std::unique_ptr<service::TerminalTcpClient> tcp_;
    std::unique_ptr<TerminalClientLoop> client_loop_;

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
        client_loop_ = std::make_unique<TerminalClientLoop>(
            [this]() -> int {
                while (stdio_) {
                    int rc = stdio_->run();
                    if (rc <= 0) {
                        return rc < 0 ? 1 : 0;
                    }
                }
                return 0;
            },
            [this] {
                if (stdio_) {
                    stdio_->requestStop();
                }
            }
        );
        loop::setMain(client_loop_.get());
        node()->at("terminal/stdio/runtime/stdio")->set(Var(true));
        node()->at("terminal/tcp/runtime/active")->set(Var(false));
        node()->at("terminal/tcp/runtime/last_error")->set(Var(""));
        if (entry::options().verbose) veLogI << "[ve/client/terminal/stdio] stdio REPL enabled";
    } else if (remote_enabled) {
        Node* tcp = node()->at("terminal/tcp");
        std::string host = tcp->at("config/host")->getString("127.0.0.1");
        int port = tcp->at("config/port")->getInt(10000);
        if (port <= 0 || port > 65535) {
            port = 10000;
        }

        tcp_ = std::make_unique<service::TerminalTcpClient>(host, static_cast<uint16_t>(port));

        tcp->at("runtime/host")->set(Var(host));
        tcp->at("runtime/port")->set(Var(port));
        tcp->at("runtime/active")->set(Var(true));
        tcp->at("runtime/last_error")->set(Var(""));

        client_loop_ = std::make_unique<TerminalClientLoop>(
            [this]() -> int {
                int rc = tcp_ ? tcp_->run() : 0;
                if (tcp_) {
                    node()->at("terminal/tcp/runtime/last_error")->set(Var(tcp_->lastError()));
                }
                node()->at("terminal/tcp/runtime/active")->set(Var(false));
                return rc;
            },
            [this] {
                if (tcp_) {
                    tcp_->requestStop();
                }
            }
        );
        loop::setMain(client_loop_.get());

        node()->at("terminal/stdio/runtime/stdio")->set(Var(false));
        veLogI << "[ve/client/terminal/tcp] enabled -> " << host << ":" << port;
    } else {
        node()->at("terminal/stdio/runtime/stdio")->set(Var(false));
        node()->at("terminal/tcp/runtime/active")->set(Var(false));
    }
}

void ClientModule::deinit()
{
    if (client_loop_) {
        if (loop::main() == client_loop_.get()) loop::setMain(nullptr);
        client_loop_.reset();
    }
    if (stdio_) {
        stdio_->requestStop();
        stdio_.reset();
    }
    if (tcp_) {
        node()->at("terminal/tcp/runtime/last_error")->set(Var(tcp_->lastError()));
        tcp_->requestStop();
        tcp_.reset();
    }
    node()->at("terminal/stdio/runtime/stdio")->set(Var(false));
    node()->at("terminal/tcp/runtime/active")->set(Var(false));
}

}

VE_REGISTER_PRIORITY_MODULE(ve.client, ve::ClientModule, 40)
