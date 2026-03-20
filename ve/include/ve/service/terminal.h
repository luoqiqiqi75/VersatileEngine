// terminal.h — ve::Terminal: TCP-based remote Node REPL service
//
// Combines the REPL engine and TCP server into a single class.
// Commands are registered globally via command::reg(); Terminal is a frontend.
// Per-session state (current node, history, orphans) is managed internally.
//
#pragma once

#include "ve/global.h"
#include <cstdint>
#include <memory>
#include <string>

namespace ve {

class Node;

class VE_API Terminal
{
public:
    explicit Terminal(Node* root = nullptr, uint16_t port = 5061);
    ~Terminal();

    bool     start();
    void     stop();
    bool     isRunning() const;
    int      connectionCount() const;
    uint16_t port() const;

    static std::string nodeToJson(const Node* node, int indent = 2);

private:
    struct Private;
    std::unique_ptr<Private> _p;
};

} // namespace ve
