// terminal_service.h — TCP text REPL (remote shell) over the Node tree
#pragma once

#include "ve/global.h"
#include <cstdint>

namespace ve {

class Node;

namespace service {

class VE_API TerminalReplServer
{
public:
    explicit TerminalReplServer(Node* root, uint16_t port);
    ~TerminalReplServer();

    bool     start();
    void     stop();
    bool     isRunning() const;
    int      connectionCount() const;
    uint16_t port() const;

    static std::string nodeToJson(const Node* node, int indent = 2);

private:
    VE_DECLARE_UNIQUE_PRIVATE
};

struct VE_API TerminalStdioClient
{
    explicit TerminalStdioClient(Node* root = nullptr);
    ~TerminalStdioClient();

    // Run the stdio REPL:
    //   1 => continue pumping (fallback cooked stdin mode)
    //   0 => stop (quit/exit/EOF/requestStop)
    //  -1 => I/O error
    int  run();
    void requestStop();

private:
    VE_DECLARE_UNIQUE_PRIVATE
};

class VE_API TerminalTcpClient
{
public:
    explicit TerminalTcpClient(const std::string& host, uint16_t port);
    ~TerminalTcpClient();

    // Connect to the remote terminal, forward local console input, and block
    // until disconnect / EOF / requestStop. Returns a process-style exit code.
    int  run();
    void requestStop();

    bool isConnected() const;
    const std::string& host() const;
    uint16_t port() const;
    std::string lastError() const;

private:
    VE_DECLARE_UNIQUE_PRIVATE
};

} // namespace service
} // namespace ve
