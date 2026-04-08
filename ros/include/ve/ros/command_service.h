#pragma once

#include "ve/core/command.h"
#include "ve/core/node.h"
#include "ve/ros/yaml_schema.h"

#include <yaml-cpp/yaml.h>

namespace ve::ros {

namespace yaml {

VE_API YAML::Node varToYaml(const Var& v);
VE_API Var        yamlToVar(const YAML::Node& yn);

VE_API YAML::Node nodeToYaml(Node* n);
VE_API void       yamlToNode(const YAML::Node& yn, Node* n);

VE_API std::string encode(const Var& v);
VE_API std::string encode(Node* n);
VE_API Var         decode(const std::string& yaml_str);

} // namespace yaml

class VE_API CommandService
{
    VE_DECLARE_PRIVATE

public:
    explicit CommandService(const std::string& prefix = "ve");
    ~CommandService();

    bool start(std::string* error = nullptr);
    void stop();

    bool isRunning() const;
    std::string backendKey() const;
};

} // namespace ve::ros
