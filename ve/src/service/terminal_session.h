// terminal_session.h — internal: per-connection REPL state (used only by TerminalServer)
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ve {

class Node;

namespace service {

class TerminalSession
{
public:
    using AsyncOutputFn = std::function<void(const std::string&)>;

    struct Options {
        bool prompt_color = true;  // Use colored prompt
        bool use_current = true;   // Maintain current path (cd command)
    };

    explicit TerminalSession(Node* root, const Options& opts = Options());
    ~TerminalSession();

    std::string execute(const std::string& line);
    std::string prompt() const;
    std::vector<std::string> complete(const std::string& partial);
    const std::vector<std::string>& history() const;

    void setAsyncOutput(AsyncOutputFn fn);

    Node* root() const;
    Node* current() const;

private:
    struct Private;
    std::unique_ptr<Private> _p;
};

} // namespace service
} // namespace ve
