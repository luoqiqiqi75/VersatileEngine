#pragma once

#include "ve/core/command.h"
#include "ve/core/var.h"

namespace ve::ros {

struct ServiceCallRequest
{
    std::string service;
    std::string type;
    std::string request;
    std::string payload_format = "yaml";
    int timeout_wait_ms = 5000;
    int timeout_response_ms = 10000;
};

VE_API Var::ListV listServices(const std::string& filter = "");
VE_API Result serviceInfo(const std::string& service_name, Node* out);
VE_API Result callService(const ServiceCallRequest& request, Node* out);

} // namespace ve::ros
