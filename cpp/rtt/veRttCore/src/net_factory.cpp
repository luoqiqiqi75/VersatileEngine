#include <ve/rtt/net_factory.h>

#ifdef IMOL_HAS_EVPP

namespace imol {
namespace net {

ServerNetObject* createServer(const std::string& name, NetType type)
{
    auto* obj = new EvppServerNetObject(name, type);
    mgr().add(obj);
    return obj;
}

ClientNetObject* createClient(const std::string& name, NetType type)
{
    auto* obj = new EvppClientNetObject(name, type);
    mgr().add(obj);
    return obj;
}

} // namespace net
} // namespace imol

#endif // IMOL_HAS_EVPP
