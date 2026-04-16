#pragma once

#include "ve/core/var.h"

namespace ve::ros {

VE_API Var::ListV listServices(const std::string& filter = "");
VE_API Var::DictV serviceInfo(const std::string& service_name);

} // namespace ve::ros
