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
VE_API Result runtimeInfo(Node* out);

VE_API Var::ListV listNodes(const std::string& filter = "");
VE_API Result listParams(const std::string& node_name, Node* out);
VE_API Result getParam(const std::string& node_name, const std::string& name, Node* out);
VE_API Result setParam(const std::string& node_name, const std::string& name, const Var& value, Node* out);

} // namespace ve::ros
