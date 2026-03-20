#pragma once

#include <ve/rtt/server_net_object.h>
#include <ve/rtt/client_net_object.h>

namespace imol {
namespace net {

inline Manager& mgr() {
    static Manager s_mgr("_imol_net_mgr");
    return s_mgr;
}

#ifdef IMOL_HAS_EVPP
ServerNetObject* createServer(const std::string& name, NetType type = NET_RAW);
ClientNetObject* createClient(const std::string& name, NetType type = NET_RAW);
#endif

} // namespace net
} // namespace imol
