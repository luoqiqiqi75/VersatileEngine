// tcp_bin_client.h — blocking TcpBin client (pairs with ve::TcpBinServer)
#pragma once

#include "ve/global.h"
#include "ve/core/var.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace ve {

class VE_API TcpBinClient
{
public:
    TcpBinClient();
    ~TcpBinClient();

    TcpBinClient(const TcpBinClient&) = delete;
    TcpBinClient& operator=(const TcpBinClient&) = delete;

    bool connect(const std::string& host, uint16_t port);
    void disconnect();
    bool isConnected() const;

    /// request must be a Dict (op/path/args/id). If id is missing, assigns a monotonic id.
    /// On success, outResponse is the Dict { id, code, data } (or error frame payload).
    bool call(Var request, Var& outResponse, int timeoutMs = 30000);

private:
    struct Private;
    std::unique_ptr<Private> _p;
};

} // namespace ve
