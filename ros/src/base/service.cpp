#include "ve/ros/service.h"

#include "ve/ros/runtime.h"

namespace ve::ros {

Var::ListV listServices(const std::string& filter)
{
    if (auto current = activeBackend())
        return current->listServices(filter);
    return {};
}

Result serviceInfo(const std::string& service_name, Node* out)
{
    auto current = activeBackend();
    if (!current) return Result::fail("no active ROS backend");
    return current->serviceInfo(service_name, out);
}

Result callService(const ServiceCallRequest& request, Node* out)
{
    auto current = activeBackend();
    if (!current) return Result::fail("no active ROS backend");
    return current->callService(request, out);
}

} // namespace ve::ros
