#pragma once

#include "ve/core/schema.h"

#include <yaml-cpp/yaml.h>

namespace ve::ros::yaml {

VE_API YAML::Node varToYaml(const Var& v);
VE_API Var        yamlToVar(const YAML::Node& yn);

VE_API YAML::Node nodeToYaml(Node* n);
VE_API void       yamlToNode(const YAML::Node& yn, Node* n);

VE_API std::string encode(const Var& v);
VE_API std::string encode(Node* n);
VE_API Var         decode(const std::string& yaml_str);

} // namespace ve::ros::yaml

namespace ve::schema {

struct YamlS {};

template<>
struct SchemaTraits<YamlS>
{
    VE_API static std::string exportNode(const Node* node, int indent = 2);
    VE_API static std::string exportNode(const Node* node, const ExportOptions& options);
    VE_API static bool importNode(Node* node, const std::string& data);
    VE_API static bool importNode(Node* node, const std::string& data, const ImportOptions& options);
};

} // namespace ve::schema
