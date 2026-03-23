// terminal_session.h — internal: per-connection REPL state (used only by TerminalServer)
#pragma once

#include <memory>
#include <string>
#include <vector>

namespace ve {

class Node;

namespace service {

class TerminalSession
{
public:
    explicit TerminalSession(Node* root);
    ~TerminalSession();

    std::string execute(const std::string& line);
    std::string prompt() const;
    std::vector<std::string> complete(const std::string& partial);

    Node* root() const;
    Node* current() const;

private:
    struct Private;
    std::unique_ptr<Private> _p;
};

} // namespace service
} // namespace ve
