// terminal_service.cpp — ve::service::TerminalServer
//
// REPL logic: TerminalSession (terminal_session.cpp). This file: TCP server, telnet, line editing.

#include "ve/service/terminal_service.h"
#include "terminal_line_editor.h"
#include "terminal_session.h"
#include "ve/core/node.h"
#include "ve/core/impl/json.h"
#include "ve/core/log.h"
#include "server_util.h"

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <asio2/tcp/tcp_client.hpp>
#include <asio2/tcp/tcp_server.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#ifdef _WIN32
#  include <conio.h>
#  include <windows.h>
#else
#  include <sys/select.h>
#  include <termios.h>
#  include <unistd.h>
#endif

#include <chrono>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <atomic>
#include <iostream>
#include <thread>

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

static std::string toTcpText(const std::string& text)
{
    std::string out;
    out.reserve(text.size() + 16);
    for (char ch : text) {
        if (ch == '\n') {
            out += "\r\n";
        } else {
            out.push_back(ch);
        }
    }
    return out;
}

static std::string renderEditorLine(const TerminalLineEditor& editor, bool new_line, bool use_ansi = true)
{
    std::string out = new_line ? "\r\n" : "\r";
    out += editor.renderedLine();

    if (use_ansi) {
        out += "\x1b[K";
        size_t back_cols = editor.cursorColsToEnd();
        if (back_cols > 0) {
            out += "\x1b[" + std::to_string(back_cols) + "D";
        }
    }
    return out;
}

static void writeEditorResult(std::ostream& os, const TerminalLineEditor& editor,
                              const TerminalEditResult& result, bool use_crlf)
{
    if (!result.output.empty()) {
        if (use_crlf) {
            os << toTcpText(result.output);
        } else {
            os << result.output;
        }
    }

    if (result.prompt_on_new_line) {
        if (use_crlf) {
            os << renderEditorLine(editor, true);
        } else {
            os << '\n' << editor.renderedLine() << "\x1b[K";
            size_t back = editor.line().size() - editor.cursor();
            if (back > 0) {
                os << "\x1b[" << back << 'D';
            }
        }
    } else if (result.redraw) {
        if (use_crlf) {
            os << renderEditorLine(editor, false);
        } else {
            os << '\r' << editor.renderedLine() << "\x1b[K";
            size_t back = editor.line().size() - editor.cursor();
            if (back > 0) {
                os << "\x1b[" << back << 'D';
            }
        }
    }
    os.flush();
}

struct TcpDecodeState
{
    int iac_state = 0;
    std::string esc_seq;
    bool prev_cr = false;
};

static bool consumeTelnetByte(TcpDecodeState& state, uint8_t ch)
{
    if (state.iac_state == 1) {
        if (ch >= 0xFB && ch <= 0xFE) {
            state.iac_state = 2;
            return true;
        }
        state.iac_state = 0;
        return true;
    }
    if (state.iac_state == 2) {
        state.iac_state = 0;
        return true;
    }
    if (ch == 0xFF) {
        state.iac_state = 1;
        return true;
    }
    return false;
}

static bool decodeEscapedTcpKey(TcpDecodeState& state, uint8_t ch, TerminalKeyEvent& ev)
{
    if (state.esc_seq.empty()) {
        if (ch == 0x1B) {
            state.esc_seq.push_back(static_cast<char>(ch));
            return true;
        }
        return false;
    }

    state.esc_seq.push_back(static_cast<char>(ch));
    const std::string& seq = state.esc_seq;

    if (seq == "\x1b[" || seq == "\x1bO") {
        return true;
    }

    if (seq == "\x1b[A") {
        ev.key = TerminalKey::UP;
    } else if (seq == "\x1b[B") {
        ev.key = TerminalKey::DOWN;
    } else if (seq == "\x1b[C") {
        ev.key = TerminalKey::RIGHT;
    } else if (seq == "\x1b[D") {
        ev.key = TerminalKey::LEFT;
    } else if (seq == "\x1b[H" || seq == "\x1bOH") {
        ev.key = TerminalKey::HOME;
    } else if (seq == "\x1b[F" || seq == "\x1bOF") {
        ev.key = TerminalKey::END;
    } else if (seq == "\x1b[3~") {
        ev.key = TerminalKey::DELETE_KEY;
    }

    state.esc_seq.clear();
    return true;
}

// ============================================================================
// Per-connection state: TerminalSession + line-editing buffer
// ============================================================================

struct ConnectionState {
    std::unique_ptr<TerminalSession> session;
    std::unique_ptr<TerminalLineEditor> editor;
    TcpDecodeState decode;
    bool server_controls_input = false;
    bool use_ansi = true;  // false for AI mode

    // UTF-8 multi-byte accumulator for TCP input
    std::string utf8_buf;
    size_t utf8_expected = 0;  // total bytes expected for current character
};

// ============================================================================
// TerminalStdioClient::Private
// ============================================================================

struct TerminalStdioClient::Private
{
    Node* root = nullptr;
    std::unique_ptr<TerminalSession> session;
    std::unique_ptr<TerminalLineEditor> editor;
    bool banner_printed = false;
    bool console_prepared = false;
    bool raw_console = false;
    std::atomic<bool> stop_requested{false};

#ifdef _WIN32
    HANDLE stdin_handle = INVALID_HANDLE_VALUE;
    HANDLE stdout_handle = INVALID_HANDLE_VALUE;
    DWORD stdin_mode = 0;
    DWORD stdout_mode = 0;
    UINT stdin_cp = 0;
    UINT stdout_cp = 0;
    bool restore_stdin_mode = false;
    bool restore_stdout_mode = false;
    bool restore_codepage = false;
#else
    termios stdin_mode{};
    bool restore_stdin_mode = false;
#endif

    ~Private()
    {
#ifdef _WIN32
        if (restore_codepage) {
            SetConsoleCP(stdin_cp);
            SetConsoleOutputCP(stdout_cp);
        }
        if (restore_stdin_mode && stdin_handle != INVALID_HANDLE_VALUE) {
            SetConsoleMode(stdin_handle, stdin_mode);
        }
        if (restore_stdout_mode && stdout_handle != INVALID_HANDLE_VALUE) {
            SetConsoleMode(stdout_handle, stdout_mode);
        }
#else
        if (restore_stdin_mode) {
            tcsetattr(STDIN_FILENO, TCSANOW, &stdin_mode);
        }
#endif
    }
};

struct TerminalTcpClient::Private
{
    std::string host = "127.0.0.1";
    uint16_t port = 10000;
    asio2::tcp_client client;
    std::atomic<bool> connected{false};
    std::atomic<bool> stop_requested{false};
    std::atomic<bool> console_prepared{false};
    std::atomic<bool> raw_console{false};
    std::string last_error;
    std::mutex io_mtx;
    mutable std::mutex state_mtx;

#ifdef _WIN32
    HANDLE stdin_handle = INVALID_HANDLE_VALUE;
    HANDLE stdout_handle = INVALID_HANDLE_VALUE;
    DWORD stdin_mode = 0;
    DWORD stdout_mode = 0;
    UINT stdin_cp = 0;
    UINT stdout_cp = 0;
    bool restore_stdin_mode = false;
    bool restore_stdout_mode = false;
    bool restore_codepage = false;
#else
    termios stdin_mode{};
    bool restore_stdin_mode = false;
#endif

    ~Private()
    {
#ifdef _WIN32
        if (restore_codepage) {
            SetConsoleCP(stdin_cp);
            SetConsoleOutputCP(stdout_cp);
        }
        if (restore_stdin_mode && stdin_handle != INVALID_HANDLE_VALUE) {
            SetConsoleMode(stdin_handle, stdin_mode);
        }
        if (restore_stdout_mode && stdout_handle != INVALID_HANDLE_VALUE) {
            SetConsoleMode(stdout_handle, stdout_mode);
        }
#else
        if (restore_stdin_mode) {
            tcsetattr(STDIN_FILENO, TCSANOW, &stdin_mode);
        }
#endif
    }
};

// ============================================================================
// TerminalServer::Private
// ============================================================================

struct TerminalReplServer::Private
{
    Node*    root = nullptr;
    bool     ownsRoot = false;
    uint16_t port = 10000;
    TerminalReplServer::Options opts;

    asio2::tcp_server server;
    std::mutex mtx;
    std::unordered_map<std::size_t, std::unique_ptr<ConnectionState>> connections;
    std::atomic<int> connCount{0};
};

// ============================================================================
// TerminalServer
// ============================================================================

TerminalReplServer::TerminalReplServer(Node* root, uint16_t port, const Options& opts) : _p(std::make_unique<Private>())
{
    _p->root = root ? root : ve::node::root();
    _p->port = port;
    _p->opts = opts;
}

TerminalReplServer::~TerminalReplServer()
{
    stop();
    if (_p->ownsRoot) delete _p->root;
}

bool TerminalReplServer::start()
{
    _p->server.bind_connect([this](auto& session_ptr) {
        auto key = session_ptr->hash_key();
        auto cs = std::make_unique<ConnectionState>();

        TerminalSession::Options session_opts;
        session_opts.prompt_color = _p->opts.prompt_color;
        session_opts.use_current = _p->opts.use_current;
        cs->session = std::make_unique<TerminalSession>(_p->root, session_opts);
        cs->use_ansi = _p->opts.prompt_color;  // AI mode: no ANSI escape codes

        if (Node* tcp_cfg = _p->root->find("ve/server/terminal/repl/config/tcp")) {
            cs->server_controls_input = tcp_cfg->get("server_line_echo").toBool(false);
        }
        cs->editor = std::make_unique<TerminalLineEditor>(
            cs->session.get(),
            cs->server_controls_input ? TerminalLineEditor::Mode::CONTROLLED : TerminalLineEditor::Mode::COOKED
        );

        std::string welcome;
        if (_p->opts.banner) welcome += banner();
        if (_p->opts.title) welcome += title();
        welcome += cs->editor->renderedLine();

        // Wire async command output for this connection
        cs->session->setAsyncOutput([this, key](const std::string& text) {
            _p->server.post([this, key, text]() {
                std::lock_guard<std::mutex> lock(_p->mtx);
                auto it = _p->connections.find(key);
                if (it == _p->connections.end()) return;
                auto* cs = it->second.get();
                std::string out;
                if (cs->use_ansi) out += "\r\x1b[K";
                out += toTcpText(text);
                out += renderEditorLine(*cs->editor, false, cs->use_ansi);
                _p->server.foreach_session([&](auto& sp) {
                    if (sp->hash_key() == key)
                        sp->async_send(out);
                });
            });
        });

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

        auto flushResult = [&session_ptr, cs](const TerminalEditResult& result) -> bool {
            if (!result.output.empty()) {
                session_ptr->async_send(toTcpText(result.output));
            }
            if (result.disconnect) {
                session_ptr->stop();
                return true;
            }
            if (result.prompt_on_new_line) {
                session_ptr->async_send(renderEditorLine(*cs->editor, true, cs->use_ansi));
            } else if (result.redraw && cs->server_controls_input) {
                session_ptr->async_send(renderEditorLine(*cs->editor, false, cs->use_ansi));
            }
            return false;
        };

        for (uint8_t ch : data) {
            if (consumeTelnetByte(cs->decode, ch)) {
                if (!cs->server_controls_input) {
                    cs->server_controls_input = true;
                    cs->editor->setMode(TerminalLineEditor::Mode::CONTROLLED);
                }
                continue;
            }
            if (ch == '\n' && cs->decode.prev_cr) {
                cs->decode.prev_cr = false;
                continue;
            }
            cs->decode.prev_cr = (ch == '\r');

            TerminalKeyEvent ev;
            if (decodeEscapedTcpKey(cs->decode, ch, ev)) {
                if (cs->server_controls_input && ev.key != TerminalKey::NONE) {
                    if (flushResult(cs->editor->onKey(ev))) {
                        return;
                    }
                }
                continue;
            }
            if (!cs->decode.esc_seq.empty()) {
                continue;
            }

            if (ch == '\r' || ch == '\n') {
                if (flushResult(cs->editor->onKey({TerminalKey::ENTER}))) {
                    return;
                }
            } else if (ch == 0x7F || ch == 0x08) {
                if (flushResult(cs->editor->onKey({TerminalKey::BACKSPACE}))) {
                    return;
                }
            } else if (ch == 0x03) {
                if (flushResult(cs->editor->onKey({TerminalKey::CTRL_C}))) {
                    return;
                }
            } else if (ch == 0x04) {
                if (flushResult(cs->editor->onKey({TerminalKey::EOF_KEY}))) {
                    return;
                }
            } else if (ch == 0x09) {
                if (flushResult(cs->editor->onKey({TerminalKey::TAB}))) {
                    return;
                }
            } else if (ch == 0x15) {
                if (flushResult(cs->editor->onKey({TerminalKey::CTRL_U}))) {
                    return;
                }
            } else if (ch >= 0x20 && ch < 0x7F) {
                // ASCII single-byte character
                if (cs->server_controls_input) {
                    TerminalKeyEvent ev;
                    ev.key = TerminalKey::CHARACTER;
                    ev.ch = static_cast<char>(ch);
                    ev.chars = std::string(1, ev.ch);
                    if (flushResult(cs->editor->onKey(ev))) {
                        return;
                    }
                } else {
                    cs->editor->appendCooked(static_cast<char>(ch));
                }
            } else if (ch >= 0x80) {
                // UTF-8 multi-byte character
                if (cs->utf8_buf.empty()) {
                    // Start of new UTF-8 sequence
                    cs->utf8_buf.push_back(static_cast<char>(ch));
                    if ((ch & 0xE0) == 0xC0) cs->utf8_expected = 2;
                    else if ((ch & 0xF0) == 0xE0) cs->utf8_expected = 3;
                    else if ((ch & 0xF8) == 0xF0) cs->utf8_expected = 4;
                    else {
                        // Invalid lead byte
                        cs->utf8_buf.clear();
                        cs->utf8_expected = 0;
                    }
                } else {
                    // Continuation byte
                    if ((ch & 0xC0) == 0x80) {
                        cs->utf8_buf.push_back(static_cast<char>(ch));
                        if (cs->utf8_buf.size() >= cs->utf8_expected) {
                            // Complete UTF-8 character
                            if (cs->server_controls_input) {
                                TerminalKeyEvent ev;
                                ev.key = TerminalKey::CHARACTER;
                                ev.chars = std::move(cs->utf8_buf);
                                cs->utf8_buf.clear();
                                cs->utf8_expected = 0;
                                if (flushResult(cs->editor->onKey(ev))) {
                                    return;
                                }
                            } else {
                                for (char c : cs->utf8_buf) {
                                    cs->editor->appendCooked(c);
                                }
                                cs->utf8_buf.clear();
                                cs->utf8_expected = 0;
                            }
                        }
                    } else {
                        // Invalid continuation byte
                        cs->utf8_buf.clear();
                        cs->utf8_expected = 0;
                    }
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

    ve::service::disableWindowsPortReuse(_p->server);
    return _p->server.start("0.0.0.0", _p->port);
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

template<typename State>
static bool prepareStdioConsole(State& state)
{
#ifdef _WIN32
    state.stdin_handle = GetStdHandle(STD_INPUT_HANDLE);
    state.stdout_handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (state.stdin_handle == INVALID_HANDLE_VALUE || state.stdout_handle == INVALID_HANDLE_VALUE) {
        return false;
    }

    // Save original code pages
    state.stdin_cp = GetConsoleCP();
    state.stdout_cp = GetConsoleOutputCP();
    state.restore_codepage = true;

    // Set console input/output code page to UTF-8
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);

    DWORD input_mode = 0;
    if (!GetConsoleMode(state.stdin_handle, &input_mode)) {
        return false;
    }
    state.stdin_mode = input_mode;
    state.restore_stdin_mode = true;

    DWORD raw_input = input_mode;
    raw_input &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
    // Keep ENABLE_WINDOW_INPUT to allow IME events
    raw_input |= ENABLE_EXTENDED_FLAGS | ENABLE_WINDOW_INPUT;
    if (!SetConsoleMode(state.stdin_handle, raw_input)) {
        state.restore_stdin_mode = false;
        return false;
    }

    DWORD output_mode = 0;
    if (GetConsoleMode(state.stdout_handle, &output_mode)) {
        state.stdout_mode = output_mode;
        DWORD vt_output = output_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        if (SetConsoleMode(state.stdout_handle, vt_output)) {
            state.restore_stdout_mode = true;
        }
    }
    return true;
#else
    if (!isatty(STDIN_FILENO)) {
        return false;
    }

    if (tcgetattr(STDIN_FILENO, &state.stdin_mode) != 0) {
        return false;
    }
    state.restore_stdin_mode = true;

    termios raw = state.stdin_mode;
    raw.c_iflag &= ~(ICRNL | IXON);
    raw.c_lflag &= ~(ECHO | ICANON | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) {
        state.restore_stdin_mode = false;
        return false;
    }
    return true;
#endif
}

static TerminalKeyEvent readStdioKey()
{
#ifdef _WIN32
    // Use ReadConsoleInput to support IME (Chinese input)
    HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
    INPUT_RECORD irInBuf;
    DWORD cNumRead;

    if (!ReadConsoleInputW(hStdin, &irInBuf, 1, &cNumRead) || cNumRead == 0) {
        return {TerminalKey::EOF_KEY};
    }

    if (irInBuf.EventType != KEY_EVENT || !irInBuf.Event.KeyEvent.bKeyDown) {
        return {};  // Ignore key-up events and non-key events
    }

    const KEY_EVENT_RECORD& keyEvent = irInBuf.Event.KeyEvent;
    WCHAR wch = keyEvent.uChar.UnicodeChar;
    WORD vk = keyEvent.wVirtualKeyCode;

    // Handle special keys (no Unicode character)
    if (wch == 0) {
        switch (vk) {
            case VK_UP: return {TerminalKey::UP};
            case VK_DOWN: return {TerminalKey::DOWN};
            case VK_LEFT: return {TerminalKey::LEFT};
            case VK_RIGHT: return {TerminalKey::RIGHT};
            case VK_HOME: return {TerminalKey::HOME};
            case VK_END: return {TerminalKey::END};
            case VK_DELETE: return {TerminalKey::DELETE_KEY};
            default: return {};
        }
    }

    // Handle control characters
    if (wch == L'\r') return {TerminalKey::ENTER};
    if (wch == L'\t') return {TerminalKey::TAB};
    if (wch == 0x08) return {TerminalKey::BACKSPACE};
    if (wch == 0x03) return {TerminalKey::CTRL_C};
    if (wch == 0x15) return {TerminalKey::CTRL_U};
    if (wch == 0x1A) return {TerminalKey::EOF_KEY};

    // Convert Unicode (UTF-16) to UTF-8
    if (wch >= 0x20) {
        char utf8_buf[8];
        int len = WideCharToMultiByte(CP_UTF8, 0, &wch, 1, utf8_buf, sizeof(utf8_buf), nullptr, nullptr);
        if (len > 0) {
            TerminalKeyEvent ev;
            ev.key = TerminalKey::CHARACTER;
            ev.chars = std::string(utf8_buf, len);
            if (len == 1) {
                ev.ch = utf8_buf[0];
            }
            return ev;
        }
    }

    return {};
#else
    unsigned char ch = 0;
    ssize_t n = ::read(STDIN_FILENO, &ch, 1);
    if (n <= 0) {
        return {TerminalKey::EOF_KEY};
    }

    if (ch == '\n' || ch == '\r') return {TerminalKey::ENTER};
    if (ch == '\t') return {TerminalKey::TAB};
    if (ch == 0x7F || ch == 0x08) return {TerminalKey::BACKSPACE};
    if (ch == 0x03) return {TerminalKey::CTRL_C};
    if (ch == 0x15) return {TerminalKey::CTRL_U};
    if (ch == 0x04) return {TerminalKey::EOF_KEY};

    if (ch == 0x1B) {
        unsigned char seq1 = 0;
        unsigned char seq2 = 0;
        if (::read(STDIN_FILENO, &seq1, 1) <= 0) {
            return {};
        }
        if (seq1 == '[') {
            if (::read(STDIN_FILENO, &seq2, 1) <= 0) {
                return {};
            }
            switch (seq2) {
                case 'A': return {TerminalKey::UP};
                case 'B': return {TerminalKey::DOWN};
                case 'C': return {TerminalKey::RIGHT};
                case 'D': return {TerminalKey::LEFT};
                case 'H': return {TerminalKey::HOME};
                case 'F': return {TerminalKey::END};
                case '3': {
                    unsigned char tilde = 0;
                    if (::read(STDIN_FILENO, &tilde, 1) > 0 && tilde == '~') {
                        return {TerminalKey::DELETE_KEY};
                    }
                    return {};
                }
                default: return {};
            }
        }
        return {};
    }

    // ASCII single-byte
    if (ch >= 0x20 && ch < 0x7F) {
        TerminalKeyEvent ev;
        ev.key = TerminalKey::CHARACTER;
        ev.ch = static_cast<char>(ch);
        ev.chars = std::string(1, ev.ch);
        return ev;
    }

    // UTF-8 multi-byte: read continuation bytes
    if (ch >= 0x80) {
        size_t len = 0;
        if ((ch & 0xE0) == 0xC0) len = 2;
        else if ((ch & 0xF0) == 0xE0) len = 3;
        else if ((ch & 0xF8) == 0xF0) len = 4;
        else return {};  // Invalid lead byte

        std::string utf8_char(1, static_cast<char>(ch));
        for (size_t i = 1; i < len; ++i) {
            unsigned char cont = 0;
            if (::read(STDIN_FILENO, &cont, 1) <= 0 || (cont & 0xC0) != 0x80) {
                return {};  // Invalid continuation byte
            }
            utf8_char.push_back(static_cast<char>(cont));
        }

        TerminalKeyEvent ev;
        ev.key = TerminalKey::CHARACTER;
        ev.chars = std::move(utf8_char);
        return ev;
    }

    return {};
#endif
}

static bool pollStdioKey(TerminalKeyEvent& out_event, int timeout_ms)
{
#ifdef _WIN32
    HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
    DWORD waitResult = WaitForSingleObject(hStdin, timeout_ms);
    if (waitResult == WAIT_OBJECT_0) {
        out_event = readStdioKey();
        return out_event.key != TerminalKey::NONE;
    }
    return false;
#else
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(STDIN_FILENO, &rfds);

    timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int rc = ::select(STDIN_FILENO + 1, &rfds, nullptr, nullptr, &tv);
    if (rc <= 0 || !FD_ISSET(STDIN_FILENO, &rfds)) {
        return false;
    }

    out_event = readStdioKey();
    return out_event.key != TerminalKey::NONE;
#endif
}

static std::string keyEventToBytes(const TerminalKeyEvent& ev)
{
    switch (ev.key) {
        case TerminalKey::CHARACTER:
            if (!ev.chars.empty()) {
                return ev.chars;
            }
            if (ev.ch >= 0x20 && ev.ch < 0x7f) {
                return std::string(1, ev.ch);
            }
            return {};
        case TerminalKey::ENTER:
            return "\r";
        case TerminalKey::BACKSPACE:
            return "\b";
        case TerminalKey::TAB:
            return "\t";
        case TerminalKey::CTRL_C:
            return std::string(1, '\x03');
        case TerminalKey::CTRL_U:
            return std::string(1, '\x15');
        case TerminalKey::EOF_KEY:
            return std::string(1, '\x04');
        case TerminalKey::LEFT:
            return "\x1b[D";
        case TerminalKey::RIGHT:
            return "\x1b[C";
        case TerminalKey::UP:
            return "\x1b[A";
        case TerminalKey::DOWN:
            return "\x1b[B";
        case TerminalKey::DELETE_KEY:
            return "\x1b[3~";
        case TerminalKey::HOME:
            return "\x1b[H";
        case TerminalKey::END:
            return "\x1b[F";
        case TerminalKey::NONE:
            return {};
    }
    return {};
}

template<typename State>
static int runCookedStdioRoundtrip(State& state)
{
    std::string line;
    if (!std::getline(std::cin, line)) {
        if (std::cin.eof()) {
            std::cout << '\n' << std::flush;
            return 0;
        }
        return -1;
    }

    state.editor->clearLine();
    for (char ch : line) {
        state.editor->appendCooked(ch);
    }

    TerminalEditResult result = state.editor->onKey({TerminalKey::ENTER});
    writeEditorResult(std::cout, *state.editor, result, false);
    if (result.disconnect || result.eof) {
        return 0;
    }
    return 1;
}

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
        _p->session = std::make_unique<TerminalSession>(_p->root);
        _p->editor = std::make_unique<TerminalLineEditor>(_p->session.get(), TerminalLineEditor::Mode::CONTROLLED);
        // Wire async command output for stdio
        _p->session->setAsyncOutput([this](const std::string& text) {
            std::cout << "\r\x1b[K" << text;
            if (!text.empty() && text.back() != '\n')
                std::cout << '\n';
            std::cout << _p->editor->renderedLine() << "\x1b[K";
            size_t back_cols = _p->editor->cursorColsToEnd();
            if (back_cols > 0)
                std::cout << "\x1b[" << back_cols << 'D';
            std::cout.flush();
        });
    }

    if (!_p->console_prepared) {
        std::cin.tie(nullptr);
        _p->raw_console = prepareStdioConsole(*_p);
        _p->console_prepared = true;
    }
    if (!_p->banner_printed) {
        std::cout << banner();
        std::cout << title();
        std::cout << '\n' << _p->editor->renderedLine() << std::flush;
        _p->banner_printed = true;
    }

    if (!_p->raw_console) {
        _p->editor->setMode(TerminalLineEditor::Mode::COOKED);
        return runCookedStdioRoundtrip(*_p);
    }

    _p->editor->setMode(TerminalLineEditor::Mode::CONTROLLED);
    while (!_p->stop_requested.load(std::memory_order_relaxed)) {
        TerminalKeyEvent ev = readStdioKey();
        if (ev.key == TerminalKey::NONE) {
            continue;
        }

        TerminalEditResult result = _p->editor->onKey(ev);
        if (result.eof && result.output.empty()) {
            std::cout << '\n';
        }
        writeEditorResult(std::cout, *_p->editor, result, false);
        if (result.disconnect || result.eof) {
            return 0;
        }
    }
    return 0;
}

void TerminalStdioClient::requestStop()
{
    _p->stop_requested.store(true, std::memory_order_relaxed);
}

// ============================================================================
// TerminalTcpClient
// ============================================================================

TerminalTcpClient::TerminalTcpClient(const std::string& host, uint16_t port)
    : _p(std::make_unique<Private>())
{
    _p->host = host;
    _p->port = port;
}

TerminalTcpClient::~TerminalTcpClient()
{
    requestStop();
}

int TerminalTcpClient::run()
{
    _p->stop_requested.store(false, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(_p->state_mtx);
        _p->last_error.clear();
    }
    _p->connected.store(false, std::memory_order_relaxed);

    if (!_p->console_prepared.load(std::memory_order_relaxed)) {
        std::cin.tie(nullptr);
        _p->raw_console.store(prepareStdioConsole(*_p), std::memory_order_relaxed);
        _p->console_prepared.store(true, std::memory_order_relaxed);
    }

    _p->client.bind_recv([this](std::string_view data) {
        std::lock_guard<std::mutex> lock(_p->io_mtx);
        std::cout.write(data.data(), static_cast<std::streamsize>(data.size()));
        std::cout.flush();
    });
    _p->client.bind_disconnect([this]() {
        _p->connected.store(false, std::memory_order_relaxed);
        if (!_p->stop_requested.load(std::memory_order_relaxed)) {
            std::lock_guard<std::mutex> lock(_p->state_mtx);
            _p->last_error = asio2::get_last_error_msg();
        }
    });

    if (!_p->client.start(_p->host, std::to_string(static_cast<int>(_p->port)))) {
        {
            std::lock_guard<std::mutex> lock(_p->state_mtx);
            _p->last_error = asio2::get_last_error_msg();
            if (_p->last_error.empty()) {
                _p->last_error = "connect failed";
            }
        }
        veLogE << "[ve/client/terminal/tcp] connect failed: " << _p->host
               << ":" << _p->port << " (" << lastError() << ")";
        return 1;
    }

    _p->connected.store(true, std::memory_order_relaxed);
    _p->client.send(std::string("\xFF\xFD\x01", 3));

    if (_p->raw_console.load(std::memory_order_relaxed)) {
        while (!_p->stop_requested.load(std::memory_order_relaxed)
            && _p->connected.load(std::memory_order_relaxed)) {
            TerminalKeyEvent ev;
            if (!pollStdioKey(ev, 50)) {
                continue;
            }
            std::string bytes = keyEventToBytes(ev);
            if (!bytes.empty() && _p->connected.load(std::memory_order_relaxed)) {
                _p->client.send(bytes);
            }
        }
    } else {
        std::string line;
        while (!_p->stop_requested.load(std::memory_order_relaxed)
            && _p->connected.load(std::memory_order_relaxed)
            && std::getline(std::cin, line)) {
            _p->client.send(line + "\r");
        }
    }

    if (_p->client.is_started()) {
        _p->client.stop();
    }
    _p->connected.store(false, std::memory_order_relaxed);
    return 0;
}

void TerminalTcpClient::requestStop()
{
    _p->stop_requested.store(true, std::memory_order_relaxed);
    if (_p->client.is_started()) {
        _p->client.stop();
    }
}

bool TerminalTcpClient::isConnected() const
{
    return _p->connected.load(std::memory_order_relaxed) && _p->client.is_started();
}

const std::string& TerminalTcpClient::host() const
{
    return _p->host;
}

uint16_t TerminalTcpClient::port() const
{
    return _p->port;
}

std::string TerminalTcpClient::lastError() const
{
    std::lock_guard<std::mutex> lock(_p->state_mtx);
    return _p->last_error;
}

} // namespace service
} // namespace ve
