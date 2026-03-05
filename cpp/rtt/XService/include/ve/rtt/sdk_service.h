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

    /// Main message handler — parses JSON, dispatches g/s/c, returns error code.
    int handleMessage(const std::string& addr, const std::string& msg);

    /// Send a raw string to a specific client.
    void send(const std::string& addr, const std::string& content) const;

    /// Notify async result (push to client).
    void notify(const std::string& addr, const std::string& request_id,
                const std::string& cmd_key, const Json& result) const;

private:
    // g(et) — read data objects
    Json gCmd(const std::string& data_key) const;

    // s(et) — write data objects
    Json sCmd(const std::string& data_key, const Json& input) const;

    // c(ommand) — execute command; returns true if sync, false if async
    bool cCmd(const std::string& addr, const std::string& request_id,
              const std::string& cmd_key, const Json& input,
              Json& out_result);

    ServerNetObject* m_server = nullptr;
    CipRegistry m_cips;
};

} // namespace xservice
} // namespace imol
