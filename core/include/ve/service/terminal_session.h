// terminal_session.h — Stateful REPL session: per-connection navigation + command dispatch
//
// Pure logic, no I/O. Any Transport (TCP text, WS, etc.) can hold
// per-connection TerminalSession instances.
#pragma once

#include "ve/global.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ve {

class Node;

class VE_API TerminalSession
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

} // namespace ve
