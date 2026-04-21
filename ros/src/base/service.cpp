#include "ve/ros/service.h"

#include "ve/ros/runtime.h"

namespace ve::ros {

Var::ListV listServices(const std::string& filter)
{
    if (auto current = activeBackend())
        return current->listServices(filter);
    return {};
}

Var::DictV serviceInfo(const std::string& service_name)
{
    if (auto current = activeBackend())
        return current->serviceInfo(service_name);

    Var::DictV result;
    result["ok"] = Var(false);
    result["message"] = Var("no active ROS backend");
    result["service"] = Var(service_name);
    return result;
}

Var::DictV callService(const ServiceCallRequest& request)
{
    if (auto current = activeBackend())
        return current->callService(request.service, request.type, request.request, request.payload_format);

    Var::DictV result;
    result["ok"] = Var(false);
    result["message"] = Var("no active ROS backend");
    result["service"] = Var(request.service);
    return result;
}

} // namespace ve::ros
