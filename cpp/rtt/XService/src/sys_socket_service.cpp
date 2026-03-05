#include <ve/rtt/sys_socket_service.h>

namespace imol {
namespace xservice {

SysSocketService::SysSocketService() = default;
SysSocketService::~SysSocketService() = default;

void SysSocketService::setServer(ServerNetObject* server)
{
    m_server = server;
}

void SysSocketService::regCmd(const std::string& text_key, const std::string& cmd_key)
{
    m_cmd_map[text_key] = cmd_key;
}

void SysSocketService::regQuery(const std::string& text_key, const QueryFunc& func)
{
    m_query_map[text_key] = func;
}

int SysSocketService::handleMessage(const std::string& addr, const std::string& content)
{
    // Check command mapping first
    if (m_cmd_map.has(content)) {
        execCmd(addr, m_cmd_map.value(content));
        return 0;
    }

    // Then check query mapping
    if (m_query_map.has(content)) {
        execQuery(addr, m_query_map.value(content));
        return 0;
    }

    // Unknown command
    if (m_server) m_server->send(addr, "error\r");
    return -1;
}

void SysSocketService::execCmd(const std::string& addr, const std::string& cmd_key) const
{
    auto* server = m_server;
    auto* cobj = command::copy(cmd_key, [server, addr](const Result& res) {
        if (server) {
            server->send(addr, std::string(res.isSuccess() ? "true" : "false") + "\r");
        }
    });

    if (cobj) {
        cobj->start();
    } else if (server) {
        server->send(addr, "false\r");
    }
}

void SysSocketService::execQuery(const std::string& addr, const QueryFunc& func) const
{
    if (!m_server) return;
    std::string result = func();
    m_server->send(addr, result + "\r");
}

} // namespace xservice
} // namespace imol
