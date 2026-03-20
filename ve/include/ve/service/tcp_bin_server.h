// tcp_bin_server.h — ve::TcpBinServer: binary TCP IPC transport
//
// Frame format:
//   [flag:1] [len:4 LE] [payload: bin-encoded Var]
//
// Flag bits [7:6] = message type:
//   00 = request, 01 = response, 10 = notify/event, 11 = error
//
// Payload = Var::Dict { "op": string, "path": string, "args": list/null, "id": int }
// Response = Var::Dict { "id": int, "code": int, "data": any }
//
// Pure C++ replacement for the old Qt CBS module. Peer client: tcp_bin_client.h (TcpBinClient).
#pragma once

#include "ve/global.h"
#include <cstdint>
#include <memory>

namespace ve {

class Node;

class VE_API TcpBinServer
{
public:
    explicit TcpBinServer(Node* root, uint16_t port = 5065);
    ~TcpBinServer();

    bool start();
    void stop();
    bool isRunning() const;
    int  connectionCount() const;
    uint16_t port() const;

private:
    struct Private;
    std::unique_ptr<Private> _p;
};

} // namespace ve
