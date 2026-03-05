#include <ve/rtt/server_net_object.h>

#ifdef IMOL_HAS_EVPP

namespace imol {

EvppServerNetObject::EvppServerNetObject(const std::string& name, NetType type)
    : ServerNetObject(name, type)
    , m_server(nullptr) {}

EvppServerNetObject::~EvppServerNetObject()
{
    stopListening();
}

bool EvppServerNetObject::startListening(int port, const std::string& ip)
{
    if (m_server) return false;
    auto* lo = new EvppLoopObject("_net_srv_" + name());
    lo->start();
    setLoop(lo);
    loop::mgr().add(lo);

    std::string addr = ip + ":" + std::to_string(port);
    m_server = new evpp::TCPServer(
        static_cast<EvppLoopObject*>(loop())->eventLoop(),
        addr, name(), 0);

    m_server->SetConnectionCallback([this](const evpp::TCPConnPtr& conn) {
        onConnection(conn);
    });
    m_server->SetMessageCallback([this](const evpp::TCPConnPtr& conn, evpp::Buffer* buf) {
        onMessage(conn, buf);
    });

    m_server->Init();
    m_server->Start();
    return true;
}

bool EvppServerNetObject::stopListening()
{
    if (!m_server) return false;
    m_server->Stop();
    delete m_server;
    m_server = nullptr;
    m_conns.clear();
    return true;
}

bool EvppServerNetObject::send(const std::string& addr, const std::string& content) const
{
    auto it = m_conns.find(addr);
    if (it == m_conns.end() || !it->second) return false;
    it->second->Send(content);
    return true;
}

bool EvppServerNetObject::broadcast(const std::string& content) const
{
    for (const auto& kv : m_conns) {
        if (kv.second) kv.second->Send(content);
    }
    return true;
}

LoopObject* EvppServerNetObject::loopOfClient(const std::string& addr) const
{
    return loop::get(addr2LoopName(addr), false);
}

Strings EvppServerNetObject::connectedClients() const
{
    Strings result;
    for (const auto& kv : m_conns) result.push_back(kv.first);
    return result;
}

std::string EvppServerNetObject::addr2LoopName(const std::string& addr)
{
    return "_net_conn_" + addr;
}

void EvppServerNetObject::onConnection(const evpp::TCPConnPtr& conn)
{
    if (conn->IsConnected()) {
        m_conns[conn->name()] = conn;
        auto* clo = new EvppLoopObject(addr2LoopName(conn->name()), conn->loop());
        loop::mgr().add(clo);
        trigger(CLIENT_CONNECTED);
    } else {
        loop::mgr().remove(addr2LoopName(conn->name()));
        m_conns.erase(conn->name());
        trigger(CLIENT_DISCONNECTED);
    }
}

void EvppServerNetObject::onMessage(const evpp::TCPConnPtr& conn, evpp::Buffer* buf)
{
    size_t len = buf->length();
    const char* data = buf->data();
    dispatchMessage(conn->name(), data, len);
    buf->Reset();
}

} // namespace imol

#endif // IMOL_HAS_EVPP
