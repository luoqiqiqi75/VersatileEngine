// terminal.cpp — ve::Terminal: TCP server frontend using TerminalSession
//
// The REPL logic lives in TerminalSession (terminal_session.cpp).
// This file manages the TCP server, telnet protocol, and line editing.

#include "ve/service/terminal.h"
#include "ve/service/terminal_session.h"
#include "ve/core/node.h"
#include "ve/core/impl/json.h"
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
#include <algorithm>

namespace ve {

// ============================================================================
// Banner
// ============================================================================

static const char* banner()
{
    return
        "\r\n"
        "   .                 .                        \r\n"
        "  / \\               / \\                       \r\n"
        "  \\  \\             /  /-----------.           \r\n"
        "   \\  \\           /  /  o----------'          \r\n"
        "    \\  \\         /  / \\  \\                    \r\n"
        "     \\  \\       /  /   \\  \\                   \r\n"
        "      .  o     o  .     .  \\_______.-.        \r\n"
        "       \\  \\   /  /       \\  ,-------`-'       \r\n"
        "        \\  \\ /  /         \\  \\                \r\n"
        "         \\  v  /           \\  \\               \r\n"
        "          \\   /             \\  `----------.   \r\n"
        "           `-'               `-------------'  \r\n"
        "\r\n"
        "  VersatileEngine Terminal (remote)\r\n"
        "  Type 'help' for commands\r\n"
        "\r\n";
}

static std::string commonPrefix(const std::vector<std::string>& v) {
    if (v.empty()) return {};
    std::string p = v[0];
    for (size_t i = 1; i < v.size(); ++i) {
        size_t j = 0;
        while (j < p.size() && j < v[i].size() && p[j] == v[i][j]) ++j;
        p.resize(j);
    }
    return p;
}

// ============================================================================
// Per-connection state: TerminalSession + line-editing buffer
// ============================================================================

struct ConnectionState {
    std::unique_ptr<TerminalSession> session;
    std::string lineBuf;
    int escState = 0;
    int iacState = 0;
    bool prevCR  = false;
};

// ============================================================================
// Terminal::Private
// ============================================================================

struct Terminal::Private
{
    Node*    root = nullptr;
    bool     ownsRoot = false;
    uint16_t port = 5061;
    uint16_t maxRetry = 10;

    asio2::tcp_server server;
    std::mutex mtx;
    std::unordered_map<std::size_t, std::unique_ptr<ConnectionState>> connections;
    std::atomic<int> connCount{0};
};

// ============================================================================
// Terminal
// ============================================================================

Terminal::Terminal(Node* root, uint16_t port)
    : _p(std::make_unique<Private>())
{
    if (root) {
        _p->root = root;
    } else {
        _p->root = ve::node::root();
    }
    _p->port = port;
}

Terminal::~Terminal()
{
    stop();
    if (_p->ownsRoot) delete _p->root;
}

bool Terminal::start()
{
    _p->server.bind_connect([this](auto& session_ptr) {
        auto key = session_ptr->hash_key();
        auto cs = std::make_unique<ConnectionState>();
        cs->session = std::make_unique<TerminalSession>(_p->root);

        std::string welcome = std::string(banner()) + cs->session->prompt();

        {
            std::lock_guard<std::mutex> lock(_p->mtx);
            _p->connections[key] = std::move(cs);
        }
        _p->connCount.fetch_add(1, std::memory_order_relaxed);

        session_ptr->async_send(welcome);
    });

    _p->server.bind_recv([this](auto& session_ptr, std::string_view data) {
        auto key = session_ptr->hash_key();
        ConnectionState* cs = nullptr;
        {
            std::lock_guard<std::mutex> lock(_p->mtx);
            auto it = _p->connections.find(key);
            if (it != _p->connections.end()) cs = it->second.get();
        }
        if (!cs) return;

        auto sendText = [&session_ptr](const std::string& text) {
            std::string out;
            out.reserve(text.size() + 16);
            for (char ch : text) {
                if (ch == '\n') out += "\r\n";
                else            out += ch;
            }
            session_ptr->async_send(out);
        };

        for (uint8_t ch : data) {
            // --- Telnet IAC filter ---
            if (cs->iacState == 1) {
                if (ch >= 0xFB && ch <= 0xFE) { cs->iacState = 2; continue; }
                cs->iacState = 0;
                continue;
            }
            if (cs->iacState == 2) { cs->iacState = 0; continue; }
            if (ch == 0xFF) { cs->iacState = 1; continue; }

            // --- ESC sequence filter ---
            if (cs->escState == 1) {
                cs->escState = (ch == '[') ? 2 : 0;
                continue;
            }
            if (cs->escState == 2) {
                cs->escState = 0;
                continue;
            }
            if (ch == 0x1B) { cs->escState = 1; continue; }

            // --- Line editing ---
            if (ch == '\n' && cs->prevCR) { cs->prevCR = false; continue; }
            cs->prevCR = (ch == '\r');

            if (ch == '\r' || ch == '\n') {
                session_ptr->async_send("\r\n");
                if (cs->lineBuf.empty()) {
                    session_ptr->async_send(cs->session->prompt());
                    continue;
                }
                std::string line = std::move(cs->lineBuf);
                cs->lineBuf.clear();
                std::string result = cs->session->execute(line);

                if (!result.empty() && result[0] == '\x04') {
                    session_ptr->async_send("bye\r\n");
                    session_ptr->stop();
                    return;
                }
                if (!result.empty()) sendText(result);
                session_ptr->async_send(cs->session->prompt());
            } else if (ch == 0x7F || ch == 0x08) {
                if (!cs->lineBuf.empty()) {
                    cs->lineBuf.pop_back();
                    session_ptr->async_send("\b \b");
                }
            } else if (ch == 0x03) {
                cs->lineBuf.clear();
                session_ptr->async_send("^C\r\n" + cs->session->prompt());
            } else if (ch == 0x15) {
                std::string erase;
                for (size_t i = 0; i < cs->lineBuf.size(); ++i)
                    erase += "\b \b";
                cs->lineBuf.clear();
                session_ptr->async_send(erase);
            } else if (ch == 0x09) {
                // --- Tab completion via TerminalSession ---
                auto matches = cs->session->complete(cs->lineBuf);
                if (matches.empty()) continue;

                size_t wordStart = cs->lineBuf.find_last_of(' ');
                std::string prefix = (wordStart == std::string::npos) ? cs->lineBuf : cs->lineBuf.substr(wordStart + 1);

                std::string common = commonPrefix(matches);
                if (common.size() > prefix.size()) {
                    std::string suffix = common.substr(prefix.size());
                    cs->lineBuf += suffix;
                    session_ptr->async_send(suffix);
                    if (matches.size() == 1) {
                        cs->lineBuf += ' ';
                        session_ptr->async_send(" ");
                    }
                } else if (matches.size() > 1) {
                    std::string list = "\r\n";
                    for (auto& m : matches) list += "  " + m;
                    list += "\r\n" + cs->session->prompt() + cs->lineBuf;
                    session_ptr->async_send(list);
                }
            } else if (ch >= 0x20 && ch < 0x7F) {
                cs->lineBuf += static_cast<char>(ch);
                session_ptr->async_send(std::string(1, static_cast<char>(ch)));
            }
        }
    });

    _p->server.bind_disconnect([this](auto& session_ptr) {
        auto key = session_ptr->hash_key();
        {
            std::lock_guard<std::mutex> lock(_p->mtx);
            _p->connections.erase(key);
        }
        _p->connCount.fetch_sub(1, std::memory_order_relaxed);
    });

    uint16_t p = _p->port;
    for (uint16_t i = 0; i <= _p->maxRetry; ++i) {
        if (_p->server.start("0.0.0.0", p)) {
            _p->port = p;
            veLogIs("Terminal started on port", p);
            return true;
        }
        veLogEs("Terminal port", p, "unavailable, trying next");
        ++p;
    }
    veLogEs("Terminal failed after", _p->maxRetry + 1, "attempts");
    return false;
}

void Terminal::stop()
{
    _p->server.stop();
    std::lock_guard<std::mutex> lock(_p->mtx);
    _p->connections.clear();
}

bool Terminal::isRunning() const
{
    return _p->server.is_started();
}

int Terminal::connectionCount() const
{
    return _p->connCount.load(std::memory_order_relaxed);
}

uint16_t Terminal::port() const
{
    return _p->port;
}

std::string Terminal::nodeToJson(const Node* node, int indent)
{
    return json::exportTree(node, indent);
}

} // namespace ve
