#pragma once

#include <ve/rtt/net_object.h>

namespace imol {

class ServerNetObject : public NetObject {
public:
    enum Signal : int { CLIENT_CONNECTED = 0x0300, CLIENT_DISCONNECTED = 0x0301 };

    explicit ServerNetObject(const std::string& name, NetType type = NET_RAW)
        : NetObject(name, type) {}

    virtual ~ServerNetObject() = default;

    virtual bool startListening(int port, const std::string& ip = "0.0.0.0") = 0;
    virtual bool stopListening() = 0;
    virtual bool send(const std::string& addr, const std::string& content) const = 0;
    virtual bool broadcast(const std::string& content) const = 0;
    virtual LoopObject* loopOfClient(const std::string& addr) const = 0;
    virtual Strings connectedClients() const = 0;
};

#ifdef IMOL_HAS_EVPP

#include <ve/rtt/evpp_loop.h>
#include <evpp/tcp_server.h>
#include <evpp/tcp_conn.h>
#include <evpp/buffer.h>

class EvppServerNetObject : public ServerNetObject {
public:
    explicit EvppServerNetObject(const std::string& name, NetType type = NET_RAW);
    ~EvppServerNetObject() override;

    bool startListening(int port, const std::string& ip = "0.0.0.0") override;
    bool stopListening() override;
    bool send(const std::string& addr, const std::string& content) const override;
    bool broadcast(const std::string& content) const override;
    LoopObject* loopOfClient(const std::string& addr) const override;
    Strings connectedClients() const override;

private:
    static std::string addr2LoopName(const std::string& addr);
    void onConnection(const evpp::TCPConnPtr& conn);
    void onMessage(const evpp::TCPConnPtr& conn, evpp::Buffer* buf);

    evpp::TCPServer* m_server;
    HashMap<std::string, evpp::TCPConnPtr> m_conns;
};

#endif // IMOL_HAS_EVPP

} // namespace imol
