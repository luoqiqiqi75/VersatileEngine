#include <ve/rtt/net_object.h>

namespace imol {

// --- MsgHandler implementations ---

int RawMsgHandler::handle(const NetHandler& handler, const std::string& addr, const char* data, size_t len)
{
    return handler(addr, std::string(data, len));
}

int CacheMsgHandler::handle(const NetHandler& handler, const std::string& addr, const char* data, size_t len)
{
    m_bytes.append(data, len);
    if (m_bytes.size() > 256 * 1024 * 1024) {
        m_bytes.clear();
        return -1;
    }
    int consumed = handler(addr, m_bytes);
    if (consumed > 0 && consumed <= (int)m_bytes.size()) {
        m_bytes.erase(0, consumed);
    }
    return consumed;
}

int BackslashRMsgHandler::handle(const NetHandler& handler, const std::string& addr, const char* data, size_t len)
{
    m_bytes.append(data, len);
    size_t pos;
    while ((pos = m_bytes.find('\r')) != std::string::npos) {
        std::string line = m_bytes.substr(0, pos);
        m_bytes.erase(0, pos + 1);
        if (!line.empty()) {
            handler(addr, line);
        }
    }
    return 0;
}

MsgHandler* createMsgHandler(NetType type)
{
    switch (type) {
    case NET_RAW:         return new RawMsgHandler();
    case NET_CACHE:       return new CacheMsgHandler();
    case NET_BACKSLASH_R: return new BackslashRMsgHandler();
    }
    return new RawMsgHandler();
}

// --- NetObject ---

NetObject::NetObject(const std::string& name, NetType type)
    : Object(name)
    , m_msg_handler(createMsgHandler(type)) {}

NetObject::~NetObject()
{
    delete m_msg_handler;
}

int NetObject::dispatchMessage(const std::string& addr, const char* data, size_t len)
{
    if (!m_handler || !m_msg_handler) return -1;
    return m_msg_handler->handle(m_handler, addr, data, len);
}

} // namespace imol
