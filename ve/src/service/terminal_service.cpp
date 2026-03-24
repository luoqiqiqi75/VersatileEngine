// terminal_service.cpp — ve::service::TerminalServer
//
// REPL logic: TerminalSession (terminal_session.cpp). This file: TCP server, telnet, line editing.

#include "ve/service/terminal_service.h"
#include "terminal_session.h"
#include "terminal_builtins.h"
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
#include <atomic>
#include <iostream>

namespace ve {
namespace service {

// ============================================================================
// Banner
// ============================================================================

static const char* banner()
{
    return
        "\r\n"
        R"(   .                 .                      )" "\r\n"
        R"(  / \               / \                     )""\r\n"
        R"(  \  \             /  /---------.           )""\r\n"
        R"(   \  \           /  / ,---------'          )""\r\n"
        R"(    \  \         /  A  \                    )""\r\n"
        R"(     \  \       /  / \  \                   )""\r\n"
        R"(      .  o     o  .   .  \_______.-.        )""\r\n"
        R"(       \  \   /  /     \  o-------`-'       )""\r\n"
        R"(        \  \ /  /       \  \                )""\r\n"
        R"(         \  v  /         \  \               )""\r\n"
        R"(          \   /           \  `----------.   )""\r\n"
        R"(           `-'             `-------------'  )""\r\n"
        "\r\n";
}

static const char* title() {
    return
        "  VersatileEngine Terminal\r\n"
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
    /// false: nc-style local echo; server redraws input with CR + EL (ANSI).
    /// true: raw telnet; server echoes keys (ve/service/terminal/config/tcp/server_line_echo).
    bool server_line_echo = false;
};

// ============================================================================
// TerminalStdioClient::Private
// ============================================================================

struct TerminalStdioClient::Private
{
    Node* root = nullptr;
    std::unique_ptr<TerminalSession> session;
    bool banner_printed = false;
    bool io_prepared = false;
    std::atomic<bool> stop_requested{false};
};

// ============================================================================
// TerminalServer::Private
// ============================================================================

struct TerminalReplServer::Private
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
// TerminalServer
// ============================================================================

TerminalReplServer::TerminalReplServer(Node* root, uint16_t port) : _p(std::make_unique<Private>())
{
    _p->root = root ? root : ve::node::root();
    _p->port = port;
}

TerminalReplServer::~TerminalReplServer()
{
    stop();
    if (_p->ownsRoot) delete _p->root;
}

bool TerminalReplServer::start()
{
    terminalBuiltinsEnsureRegistered();

    _p->server.bind_connect([this](auto& session_ptr) {
        auto key = session_ptr->hash_key();
        auto cs = std::make_unique<ConnectionState>();
        cs->session = std::make_unique<TerminalSession>(_p->root);
        if (Node* tcp_cfg = _p->root->find("ve/service/terminal/config/tcp")) {
            cs->server_line_echo = tcp_cfg->get("server_line_echo").toBool(false);
        }

        std::string welcome =
            std::string(banner()) + std::string(title()) + "\r\n" + cs->session->prompt();

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
        auto sendPromptNewLine = [&session_ptr, cs] {
            session_ptr->async_send(std::string("\r\n") + cs->session->prompt());
        };
        auto redrawInputLine = [&session_ptr, cs] {
            // nc local-echo: redraw one logical input line with CR + EL. ANSI EL is widely supported.
            constexpr const char* kClearToEol = "\x1b[K";
            session_ptr->async_send(std::string("\r") + cs->session->prompt() + cs->lineBuf + kClearToEol);
        };
        auto applyTabsToLine = [cs](std::string& line, std::string& listOut) -> bool {
            bool hadTab = false;
            std::string cur;
            cur.reserve(line.size() + 16);

            for (char ch : line) {
                if (ch != '\t') {
                    cur.push_back(ch);
                    continue;
                }

                hadTab = true;
                auto matches = cs->session->complete(cur);
                if (matches.empty()) {
                    continue;
                }

                size_t wordStart = cur.find_last_of(' ');
                std::string prefix = (wordStart == std::string::npos) ? cur : cur.substr(wordStart + 1);
                std::string common = commonPrefix(matches);

                if (common.size() > prefix.size()) {
                    cur += common.substr(prefix.size());
                    if (matches.size() == 1) {
                        cur.push_back(' ');
                    }
                }

                if (matches.size() > 1) {
                    listOut += "\r\n";
                    for (auto& m : matches) {
                        listOut += "  " + m;
                    }
                    listOut += "\r\n";
                }
            }

            if (!hadTab) {
                return false;
            }
            line = std::move(cur);
            return true;
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

            // --- ESC / CSI: drop well-known sequences; lone ESC does not eat the next key ---
            if (cs->escState == 2) {
                cs->escState = 0;
                continue;
            }
            if (cs->escState == 1) {
                if (ch == '[') {
                    cs->escState = 2;
                    continue;
                }
                cs->escState = 0;
            }
            if (ch == 0x1B) {
                cs->escState = 1;
                continue;
            }

            // --- Line editing ---
            if (ch == '\n' && cs->prevCR) { cs->prevCR = false; continue; }
            cs->prevCR = (ch == '\r');

            if (ch == '\r' || ch == '\n') {
                if (cs->lineBuf.empty()) {
                    sendPromptNewLine();
                    continue;
                }
                std::string line = std::move(cs->lineBuf);
                cs->lineBuf.clear();

                std::string tabList;
                if (applyTabsToLine(line, tabList)) {
                    cs->lineBuf = std::move(line);
                    if (!tabList.empty()) {
                        session_ptr->async_send(tabList);
                    }
                    redrawInputLine();
                    continue;
                }

                std::string result = cs->session->execute(line);

                if (!result.empty() && result[0] == '\x04') {
                    session_ptr->async_send("bye\r\n");
                    session_ptr->stop();
                    return;
                }
                if (!result.empty()) {
                    sendText(result);
                    if (result.back() != '\n') {
                        session_ptr->async_send("\r\n");
                    }
                }
                sendPromptNewLine();
            } else if (ch == 0x7F || ch == 0x08) {
                if (!cs->lineBuf.empty()) {
                    cs->lineBuf.pop_back();
                    if (cs->server_line_echo) {
                        session_ptr->async_send("\b \b");
                    } else {
                        redrawInputLine();
                    }
                }
            } else if (ch == 0x03) {
                cs->lineBuf.clear();
                session_ptr->async_send("^C\r\n");
                sendPromptNewLine();
            } else if (ch == 0x15) {
                if (cs->server_line_echo) {
                    std::string erase;
                    for (size_t i = 0; i < cs->lineBuf.size(); ++i) {
                        erase += "\b \b";
                    }
                    session_ptr->async_send(erase);
                }
                cs->lineBuf.clear();
                if (!cs->server_line_echo) {
                    redrawInputLine();
                }
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
                    if (cs->server_line_echo) {
                        session_ptr->async_send(suffix);
                    }
                    if (matches.size() == 1) {
                        cs->lineBuf += ' ';
                        if (cs->server_line_echo) {
                            session_ptr->async_send(" ");
                        }
                    }
                }
                if (matches.size() > 1) {
                    std::string list = "\r\n";
                    for (auto& m : matches) {
                        list += "  " + m;
                    }
                    list += "\r\n";
                    session_ptr->async_send(list);
                }
                if (cs->server_line_echo) {
                    session_ptr->async_send(cs->session->prompt() + cs->lineBuf);
                } else {
                    redrawInputLine();
                }
            } else if (ch >= 0x20 && ch < 0x7F) {
                cs->lineBuf += static_cast<char>(ch);
                if (cs->server_line_echo) {
                    session_ptr->async_send(std::string(1, static_cast<char>(ch)));
                } else {
                    redrawInputLine();
                }
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

void TerminalReplServer::stop()
{
    _p->server.stop();
    std::lock_guard<std::mutex> lock(_p->mtx);
    _p->connections.clear();
}

bool TerminalReplServer::isRunning() const
{
    return _p->server.is_started();
}

int TerminalReplServer::connectionCount() const
{
    return _p->connCount.load(std::memory_order_relaxed);
}

uint16_t TerminalReplServer::port() const
{
    return _p->port;
}

std::string TerminalReplServer::nodeToJson(const Node* node, int indent)
{
    return impl::json::exportTree(node, indent);
}

// ============================================================================
// TerminalStdioClient
// ============================================================================

TerminalStdioClient::TerminalStdioClient(Node* root) : _p(std::make_unique<Private>())
{
    _p->root = root ? root : ve::node::root();
}

TerminalStdioClient::~TerminalStdioClient() = default;

int TerminalStdioClient::run()
{
    if (_p->stop_requested.load(std::memory_order_relaxed)) {
        return 0;
    }

    if (!_p->session) {
        terminalBuiltinsEnsureRegistered();
        _p->session = std::make_unique<TerminalSession>(_p->root);
    }
    if (!_p->io_prepared) {
        std::cin.tie(nullptr);
        _p->io_prepared = true;
    }
    if (!_p->banner_printed) {
        std::cout << banner();
        std::cout << title();
        std::cout << '\n' << _p->session->prompt() << std::flush;
        _p->banner_printed = true;
    }

    std::string line;
    if (!std::getline(std::cin, line)) {
        if (std::cin.eof()) {
            std::cout << '\n';
            return 0;
        }
        return -1;
    }

    std::string result = _p->session->execute(line);
    if (!result.empty() && result[0] == '\x04') {
        std::cout << "bye\n";
        return 0;
    }

    if (!result.empty()) {
        std::cout << result;
        if (result.back() != '\n') {
            std::cout << '\n';
        }
    }
    std::cout << '\n' << _p->session->prompt() << std::flush;
    return 1;
}

void TerminalStdioClient::requestStop()
{
    _p->stop_requested.store(true, std::memory_order_relaxed);
}

} // namespace service
} // namespace ve
