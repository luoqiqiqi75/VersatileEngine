// ----------------------------------------------------------------------------
// jsonrpc_service.h — JSON-RPC 2.0 over HTTP + WebSocket
// ----------------------------------------------------------------------------
// Copyright (c) 2023-present Thilo and VersatileEngine contributors.
// Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
// See LICENSE file in the project root for full license information.
// ----------------------------------------------------------------------------

#pragma once

#include "ve/global.h"
#include <memory>
#include <string>
#include <cstdint>

namespace ve {

class Var;

// ============================================================================
// JsonRpcServer — JSON-RPC 2.0 over HTTP POST + WebSocket
// ============================================================================
//
// Protocol: JSON-RPC 2.0 (https://www.jsonrpc.org/specification)
//
// Request:
//   {"jsonrpc":"2.0","method":"node.get","params":{"path":"/config"},"id":1}
//
// Response:
//   {"jsonrpc":"2.0","result":{"value":42},"id":1}
//
// Error:
//   {"jsonrpc":"2.0","error":{"code":-32600,"message":"Invalid Request"},"id":null}
//
// Transports:
//   - HTTP POST: single request/response
//   - WebSocket: persistent connection, bidirectional
//
// Methods:
//   - node.get    {"path": "/config"}
//   - node.set    {"path": "/config", "value": 42}
//   - node.list   {"path": "/"}
//   - command.run {"name": "test", "args": {...}}
//

class VE_API JsonRpcServer
{
public:
    JsonRpcServer();
    ~JsonRpcServer();

    // Start/stop
    bool start();
    void stop();
    bool isRunning() const;

    // Configuration
    void setHttpPort(uint16_t port) { m_httpPort = port; }
    void setWsPort(uint16_t port) { m_wsPort = port; }
    uint16_t httpPort() const { return m_httpPort; }
    uint16_t wsPort() const { return m_wsPort; }

private:
    // JSON-RPC 2.0 request handling
    std::string handleRequest(const std::string& requestJson);

    // Method handlers
    Var handleNodeGet(const Var& params);
    Var handleNodeSet(const Var& params);
    Var handleNodeList(const Var& params);
    Var handleCommandRun(const Var& params);

    // Response builders
    std::string buildResponse(const Var& id, const Var& result);
    std::string buildError(const Var& id, int code, const std::string& message);

    uint16_t m_httpPort = 5070;
    uint16_t m_wsPort = 5071;

    std::shared_ptr<void> m_httpServer;  // asio2::http_server
    std::shared_ptr<void> m_wsServer;    // asio2::ws_server
};

} // namespace ve
