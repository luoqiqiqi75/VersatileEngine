// bin_tcp_service.h — Binary TCP IPC (Var frames) for the Node tree: client + server
//
// Frame: [flag:1][len:4 LE][payload] with payload = bin::writeVar(Var).
// Flag bits [7:6]: request, response, notify, error (see bin_tcp namespace).
//
// Typical payload Dict: { "op", "path", "args", "id" } / response { "id", "code", "data" }.
#pragma once

#include "ve/global.h"
#include "ve/core/var.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace ve {

class Node;

namespace service {

namespace bin {

constexpr uint8_t FLAG_REQUEST   = 0x00;
constexpr uint8_t FLAG_RESPONSE  = 0x40;
constexpr uint8_t FLAG_NOTIFY    = 0x80;
constexpr uint8_t FLAG_ERROR     = 0xC0;
constexpr uint8_t FLAG_TYPE_MASK = 0xC0;

constexpr std::size_t FRAME_HEADER_SIZE = 5;

VE_API Bytes encodeFrame(uint8_t flag, const Var& payload);
VE_API bool  tryPopFrame(Bytes& buf, uint8_t& flag, Var& outVar);

} // namespace bin

class VE_API BinTcpServer
{
public:
    explicit BinTcpServer(Node* root, uint16_t port = 5065);
    ~BinTcpServer();

    bool start();
    void stop();
    bool isRunning() const;
    int  connectionCount() const;
    uint16_t port() const;

private:
    struct Private;
    std::unique_ptr<Private> _p;
};

class VE_API BinTcpClient
{
public:
    BinTcpClient();
    ~BinTcpClient();

    BinTcpClient(const BinTcpClient&) = delete;
    BinTcpClient& operator=(const BinTcpClient&) = delete;

    bool connect(const std::string& host, uint16_t port);
    void disconnect();
    bool isConnected() const;

    bool call(Var request, Var& outResponse, int timeoutMs = 30000);

private:
    VE_DECLARE_UNIQUE_PRIVATE
};

} // namespace service
} // namespace ve
