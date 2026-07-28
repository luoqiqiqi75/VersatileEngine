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

// YAML's compact form is flow style ({a: 1, b: [2, 3]}), not a line-ending
// change, so it uses its own options rather than schema::TextExportOptions.
struct YamlS
{
    struct ExportOptions
    {
        int  indent = 2;
        bool flow   = false;  // true = single-line flow style
    };
    static ExportOptions compact() { return {2, true}; }

    VE_API static std::string fromNode(const Node* node, int indent = 2);
    VE_API static std::string fromNode(const Node* node, const ExportOptions& options);
    VE_API static bool toNode(Node* node, const std::string& data);
    VE_API static bool toNode(Node* node, const std::string& data, int copy_flags);
};

} // namespace ve::schema
