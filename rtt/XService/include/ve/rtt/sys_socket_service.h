#pragma once

#include <ve/rtt/command_object.h>
#include <ve/rtt/client_net_object.h>
#include <ve/rtt/server_net_object.h>

namespace imol {
namespace xservice {

using QueryFunc = std::function<std::string()>;

class SysSocketService {
public:
    SysSocketService();
    ~SysSocketService();

    void setServer(ServerNetObject* server);
    ServerNetObject* server() const { return m_server; }

    // Register text→command mapping
    void regCmd(const std::string& text_key, const std::string& cmd_key);

    // Register text→query mapping
    void regQuery(const std::string& text_key, const QueryFunc& func);

    // Main message handler (BACKSLASH_R protocol, text per line)
    int handleMessage(const std::string& addr, const std::string& content);

private:
    void execCmd(const std::string& addr, const std::string& cmd_key) const;
    void execQuery(const std::string& addr, const QueryFunc& func) const;

    ServerNetObject* m_server = nullptr;
    HashMap<std::string, std::string> m_cmd_map;
    HashMap<std::string, QueryFunc> m_query_map;
};

} // namespace xservice
} // namespace imol
