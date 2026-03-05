#pragma once

#include <ve/rtt/command_object.h>
#include <ve/rtt/global_data.h>
#include <ve/rtt/cip.h>
#include <ve/rtt/server_net_object.h>
#include <ve/rtt/xservice_error.h>

namespace imol {
namespace xservice {

class SdkService {
public:
    SdkService();
    ~SdkService();

    void setServer(ServerNetObject* server);
    ServerNetObject* server() const { return m_server; }

    CipRegistry& cips() { return m_cips; }

    // Main message handler — returns error code (0 = ok)
    int handleMessage(const std::string& addr, const std::string& msg);

    // Send response to specific client
    void send(const std::string& addr, const std::string& content) const;

    // Notify async result (push to client)
    void notify(const std::string& addr, const std::string& request_id,
                const std::string& cmd_key, const SimpleJson& result) const;

private:
    // g(et) — read data objects
    SimpleJson gCmd(const std::string& data_key) const;

    // s(et) — write data objects
    SimpleJson sCmd(const std::string& data_key, const SimpleJson& input) const;

    // c(ommand) — execute command, returns true if sync (result available), false if async
    bool cCmd(const std::string& addr, const std::string& request_id,
              const std::string& cmd_key, const SimpleJson& input,
              SimpleJson& out_result);

    ServerNetObject* m_server = nullptr;
    CipRegistry m_cips;
};

} // namespace xservice
} // namespace imol
