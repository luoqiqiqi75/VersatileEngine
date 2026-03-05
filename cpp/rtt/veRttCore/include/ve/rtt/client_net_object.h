#pragma once

#include <ve/rtt/net_object.h>

namespace imol {

class ClientNetObject : public NetObject {
public:
    enum Signal : int { CONNECTED = 0x0310, DISCONNECTED = 0x0311 };

    explicit ClientNetObject(const std::string& name, NetType type = NET_RAW)
        : NetObject(name, type) {}

    virtual ~ClientNetObject() = default;

    virtual bool connectToHost(const std::string& ip, int port) = 0;
    virtual bool disconnectFromHost() = 0;
    virtual bool send(const std::string& content) const = 0;
    virtual bool isConnected() const = 0;
};

#ifdef IMOL_HAS_EVPP

#include <ve/rtt/evpp_loop.h>
#include <evpp/tcp_client.h>
#include <evpp/tcp_conn.h>
#include <evpp/buffer.h>

class EvppClientNetObject : public ClientNetObject {
public:
    explicit EvppClientNetObject(const std::string& name, NetType type = NET_RAW);
    ~EvppClientNetObject() override;

    bool connectToHost(const std::string& ip, int port) override;
    bool disconnectFromHost() override;
    bool send(const std::string& content) const override;
    bool isConnected() const override;

private:
    evpp::TCPClient* m_client;
    evpp::TCPConnPtr m_conn;
    std::atomic<bool> m_is_connected;
};

#endif // IMOL_HAS_EVPP

} // namespace imol
