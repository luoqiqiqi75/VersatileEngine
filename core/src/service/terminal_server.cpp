// terminal_server.cpp — ve::TerminalServer: TCP-based remote Terminal
#include "ve/service/terminal_server.h"
#include "ve/service/terminal.h"
#include "ve/core/log.h"

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <asio2/tcp/tcp_server.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <mutex>
#include <unordered_map>

namespace ve {

struct TerminalServer::Private
{
    Node*    root = nullptr;
    uint16_t port = 5061;

    asio2::tcp_server server;
    std::mutex mtx;
    std::unordered_map<std::size_t, std::unique_ptr<Terminal>> sessions;
    std::atomic<int> connCount{0};
};

TerminalServer::TerminalServer(Node* root, uint16_t port)
    : _p(std::make_unique<Private>())
{
    _p->root = root;
    _p->port = port;
}

TerminalServer::~TerminalServer()
{
    stop();
}

bool TerminalServer::start()
{
    _p->server.bind_connect([this](auto& session_ptr) {
        auto key = session_ptr->hash_key();
        auto term = std::make_unique<Terminal>(_p->root);
        auto* tp = term.get();

        tp->setOutput([session_ptr](const std::string& text) {
            session_ptr->async_send(text);
        });

        {
            std::lock_guard<std::mutex> lock(_p->mtx);
            _p->sessions[key] = std::move(term);
        }
        _p->connCount.fetch_add(1, std::memory_order_relaxed);

        session_ptr->async_send("ve_terminal (remote)\ntype 'help' for commands\n\n" + tp->prompt());
    });

    _p->server.bind_recv([this](auto& session_ptr, std::string_view data) {
        auto key = session_ptr->hash_key();
        Terminal* tp = nullptr;
        {
            std::lock_guard<std::mutex> lock(_p->mtx);
            auto it = _p->sessions.find(key);
            if (it != _p->sessions.end()) tp = it->second.get();
        }
        if (!tp) return;

        std::string input(data);
        while (!input.empty() && (input.back() == '\n' || input.back() == '\r'))
            input.pop_back();

        if (input.empty()) {
            session_ptr->async_send(tp->prompt());
            return;
        }

        bool cont = tp->execute(input);
        if (!cont) {
            session_ptr->async_send("bye\n");
            session_ptr->stop();
            return;
        }
        session_ptr->async_send(tp->prompt());
    });

    _p->server.bind_disconnect([this](auto& session_ptr) {
        auto key = session_ptr->hash_key();
        {
            std::lock_guard<std::mutex> lock(_p->mtx);
            _p->sessions.erase(key);
        }
        _p->connCount.fetch_sub(1, std::memory_order_relaxed);
    });

    bool ok = _p->server.start("0.0.0.0", _p->port);
    if (ok) {
        veLogIs("TerminalServer started on port", _p->port);
    } else {
        veLogEs("TerminalServer failed to start on port", _p->port);
    }
    return ok;
}

void TerminalServer::stop()
{
    _p->server.stop();
    std::lock_guard<std::mutex> lock(_p->mtx);
    _p->sessions.clear();
}

bool TerminalServer::isRunning() const
{
    return _p->server.is_started();
}

int TerminalServer::connectionCount() const
{
    return _p->connCount.load(std::memory_order_relaxed);
}

} // namespace ve
