#pragma once

#include "ve/ros/backend.h"

namespace ve::ros {

VE_API bool activateBackend(const std::string& requested_key,
                            Node* runtime_node,
                            std::string& error);
VE_API void deactivateBackend();
VE_API BackendPtr activeBackend();
VE_API std::string activeBackendKey();

VE_API bool refreshRuntime(Node* runtime_node, std::string& error);
VE_API Var::DictV runtimeInfo();

VE_API Var::ListV listNodes(const std::string& filter = "");
VE_API Var::DictV listParams(const std::string& node_name = "");
VE_API Var::DictV getParam(const std::string& node_name, const std::string& name);
VE_API Var::DictV setParam(const std::string& node_name, const std::string& name, const Var& value);

} // namespace ve::ros
