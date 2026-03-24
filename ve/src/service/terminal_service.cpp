// terminal_service.cpp — ve::service::TerminalServer
//
// REPL logic: TerminalSession (terminal_session.cpp). This file: TCP server, telnet, line editing.

#include "ve/service/terminal_service.h"
#include "terminal_builtins.h"
#include "terminal_line_editor.h"
#include "terminal_session.h"
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

#ifdef _WIN32
#  include <conio.h>
#  include <windows.h>
#else
#  include <termios.h>
#  include <unistd.h>
#endif

#include <cstdint>
#include <mutex>
#include <unordered_map>
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

static std::string renderEditorLine(const TerminalLineEditor& editor, bool new_line)
{
    std::string out = new_line ? "\r\n" : "\r";
    out += editor.renderedLine();
    out += "\x1b[K";

    size_t back = editor.line().size() - editor.cursor();
    if (back > 0) {
        out += "\x1b[" + std::to_string(back) + "D";
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
    bool restore_stdin_mode = false;
    bool restore_stdout_mode = false;
#else
    termios stdin_mode{};
    bool restore_stdin_mode = false;
#endif

    ~Private()
    {
#ifdef _WIN32
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
        if (Node* tcp_cfg = _p->root->find("ve/server/terminal/repl/config/tcp")) {
            cs->server_controls_input = tcp_cfg->get("server_line_echo").toBool(false);
        }
        cs->editor = std::make_unique<TerminalLineEditor>(
            cs->session.get(),
            cs->server_controls_input ? TerminalLineEditor::Mode::CONTROLLED : TerminalLineEditor::Mode::COOKED
        );

        std::string welcome =
            std::string(banner()) + std::string(title()) + cs->editor->renderedLine();

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
                session_ptr->async_send(renderEditorLine(*cs->editor, true));
            } else if (result.redraw && cs->server_controls_input) {
                session_ptr->async_send(renderEditorLine(*cs->editor, false));
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
                if (cs->server_controls_input) {
                    if (flushResult(cs->editor->onKey({TerminalKey::CHARACTER, static_cast<char>(ch)}))) {
                        return;
                    }
                } else {
                    cs->editor->appendCooked(static_cast<char>(ch));
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

template<typename State>
static bool prepareStdioConsole(State& state)
{
#ifdef _WIN32
    state.stdin_handle = GetStdHandle(STD_INPUT_HANDLE);
    state.stdout_handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (state.stdin_handle == INVALID_HANDLE_VALUE || state.stdout_handle == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD input_mode = 0;
    if (!GetConsoleMode(state.stdin_handle, &input_mode)) {
        return false;
    }
    state.stdin_mode = input_mode;
    state.restore_stdin_mode = true;

    DWORD raw_input = input_mode;
    raw_input &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
    raw_input |= ENABLE_EXTENDED_FLAGS;
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
    int ch = _getch();
    if (ch == 0 || ch == 0xE0) {
        int ext = _getch();
        switch (ext) {
            case 72: return {TerminalKey::UP};
            case 80: return {TerminalKey::DOWN};
            case 75: return {TerminalKey::LEFT};
            case 77: return {TerminalKey::RIGHT};
            case 71: return {TerminalKey::HOME};
            case 79: return {TerminalKey::END};
            case 83: return {TerminalKey::DELETE_KEY};
            default: return {};
        }
    }

    switch (ch) {
        case '\r': return {TerminalKey::ENTER};
        case '\t': return {TerminalKey::TAB};
        case 0x08: return {TerminalKey::BACKSPACE};
        case 0x03: return {TerminalKey::CTRL_C};
        case 0x15: return {TerminalKey::CTRL_U};
        case 0x1A: return {TerminalKey::EOF_KEY};
        default:
            if (ch >= 0x20 && ch < 0x7F) {
                return {TerminalKey::CHARACTER, static_cast<char>(ch)};
            }
            return {};
    }
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

    if (ch >= 0x20 && ch < 0x7F) {
        return {TerminalKey::CHARACTER, static_cast<char>(ch)};
    }
    return {};
#endif
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
        terminalBuiltinsEnsureRegistered();
        _p->session = std::make_unique<TerminalSession>(_p->root);
        _p->editor = std::make_unique<TerminalLineEditor>(_p->session.get(), TerminalLineEditor::Mode::CONTROLLED);
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

} // namespace service
} // namespace ve
