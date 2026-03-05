#include <ve/rtt/client_net_object.h>

#ifdef IMOL_HAS_EVPP

namespace imol {

EvppClientNetObject::EvppClientNetObject(const std::string& name, NetType type)
    : ClientNetObject(name, type)
    , m_client(nullptr)
    , m_is_connected(false) {}

EvppClientNetObject::~EvppClientNetObject()
{
    disconnectFromHost();
}

bool EvppClientNetObject::connectToHost(const std::string& ip, int port)
{
    if (m_client) return false;
    auto* lo = new EvppLoopObject("_net_cli_" + name());
    lo->start();
    setLoop(lo);
    loop::mgr().add(lo);

    std::string addr = ip + ":" + std::to_string(port);
    m_client = new evpp::TCPClient(
        static_cast<EvppLoopObject*>(loop())->eventLoop(),
        addr, name());

    m_client->set_auto_reconnect(false);

    m_client->SetConnectionCallback([this](const evpp::TCPConnPtr& conn) {
        if (conn->IsConnected()) {
            m_conn = conn;
            m_is_connected = true;
            trigger(CONNECTED);
        } else {
            m_conn.reset();
            m_is_connected = false;
            trigger(DISCONNECTED);
        }
    });
    m_client->SetMessageCallback([this](const evpp::TCPConnPtr& conn, evpp::Buffer* buf) {
        size_t len = buf->length();
        const char* data = buf->data();
        dispatchMessage(conn->name(), data, len);
        buf->Reset();
    });

    m_client->Connect();
    return true;
}

bool EvppClientNetObject::disconnectFromHost()
{
    if (!m_client) return false;
    m_client->Disconnect();
    delete m_client;
    m_client = nullptr;
    m_conn.reset();
    m_is_connected = false;
    return true;
}

bool EvppClientNetObject::send(const std::string& content) const
{
    if (!m_conn || !m_is_connected) return false;
    m_conn->Send(content);
    return true;
}

bool EvppClientNetObject::isConnected() const { return m_is_connected; }

} // namespace imol

#endif // IMOL_HAS_EVPP
