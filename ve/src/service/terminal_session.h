// terminal_session.h — per-connection REPL state
//
// TerminalSession inherits Session (root/current/send) and adds REPL-specific
// state (history, orphans, prompt). Used by TerminalServer and StdioClient.
#pragma once

#include "node_commands.h"

#include <memory>
#include <string>
#include <vector>

namespace ve {

class Node;

namespace service {

void registerReplCommands(Factory& f);

class TerminalSession : public Session
{
public:
    using AsyncOutputFn = std::function<void(const std::string&)>;

    struct Options {
        bool prompt_color;
        bool use_current;
        Options() : prompt_color(true), use_current(true) {}
    };

    explicit TerminalSession(Node* root, const Options& opts = Options());
    ~TerminalSession();

    std::string execute(const std::string& line);
    std::string prompt() const;
    std::vector<std::string> complete(const std::string& partial);
    const std::vector<std::string>& history() const;
    std::vector<Node*>& orphans();

    void setAsyncOutput(AsyncOutputFn fn);
    bool useColor() const;

private:
    struct Private;
    std::unique_ptr<Private> _p;
};

} // namespace service
} // namespace ve
