#pragma once

#include "ve/core/var.h"

namespace ve::ros {

struct ServiceCallRequest
{
    std::string service;
    std::string type;
    std::string request;
    std::string payload_format = "yaml";
};

VE_API Var::ListV listServices(const std::string& filter = "");
VE_API Var::DictV serviceInfo(const std::string& service_name);
VE_API Var::DictV callService(const ServiceCallRequest& request);

} // namespace ve::ros
