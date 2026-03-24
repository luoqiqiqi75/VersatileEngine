// command_service.h — ve::dds::CommandService
//
// Exposes standard VE commands (ls, get, set, info, etc.) as DDS services.
// Request/response payloads use YAML serialization, compatible with ROS2
// service clients.
//
// Standard services registered:
//   ve/command   — generic dispatcher: { command, path, args } → { code, result }
//   ve/data_list — list node children (YAML)
//   ve/data_get  — get node value (YAML)
//   ve/data_set  — set node value from YAML
//
// Usage:
//   auto& p = ve::dds::Participant::instance();
//   ve::dds::CommandService svc(p);
//   svc.start();   // registers all DDS service endpoints

#pragma once

#include "ve/ros/dds/participant.h"
#include "ve/core/node.h"
#include "ve/core/command.h"

#include <yaml-cpp/yaml.h>

namespace ve::dds {

// ============================================================================
// YAML ↔ Var / Node conversion utilities
// ============================================================================

namespace yaml {

VE_API YAML::Node varToYaml(const Var& v);
VE_API Var        yamlToVar(const YAML::Node& yn);

VE_API YAML::Node nodeToYaml(Node* n);
VE_API void       yamlToNode(const YAML::Node& yn, Node* n);

VE_API std::string encode(const Var& v);
VE_API std::string encode(Node* n);
VE_API Var         decode(const std::string& yamlStr);

} // namespace yaml

// ============================================================================
// CommandService — DDS services for VE commands
// ============================================================================

class VE_API CommandService
{
    VE_DECLARE_PRIVATE

public:
    explicit CommandService(Participant& p, const std::string& prefix = "ve");
    ~CommandService();

    void start();
    void stop();

    bool isRunning() const;
};

} // namespace ve::dds
