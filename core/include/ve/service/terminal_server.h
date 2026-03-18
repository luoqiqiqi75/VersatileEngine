// terminal_server.h — ve::TerminalServer: TCP-based remote Terminal
#pragma once

#include "ve/global.h"
#include <cstdint>
#include <memory>

namespace ve {

class Node;

class VE_API TerminalServer
{
public:
    explicit TerminalServer(Node* root, uint16_t port = 5061);
    ~TerminalServer();

    bool start();
    void stop();
    bool isRunning() const;
    int  connectionCount() const;

private:
    struct Private;
    std::unique_ptr<Private> _p;
};

} // namespace ve
