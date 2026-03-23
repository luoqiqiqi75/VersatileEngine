// terminal_service.h — TCP text REPL (remote shell) over the Node tree
#pragma once

#include "ve/global.h"
#include <cstdint>
#include <memory>
#include <string>

namespace ve {

class Node;

namespace service {

class VE_API TerminalReplServer
{
public:
    explicit TerminalReplServer(Node* root = nullptr, uint16_t port = 5061);
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

    int  run();
    void requestStop();

private:
    VE_DECLARE_UNIQUE_PRIVATE
};

} // namespace service
} // namespace ve
