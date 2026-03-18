// ws_server.h — ve::WsServer: WebSocket-based real-time Node updates
#pragma once

#include "ve/global.h"
#include <cstdint>
#include <memory>

namespace ve {

class Node;

class VE_API WsServer
{
public:
    explicit WsServer(Node* root, uint16_t port = 8081);
    ~WsServer();

    bool start();
    void stop();
    bool isRunning() const;
    int  connectionCount() const;

private:
    struct Private;
    std::unique_ptr<Private> _p;
};

} // namespace ve
