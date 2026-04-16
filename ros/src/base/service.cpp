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

} // namespace ve::ros
