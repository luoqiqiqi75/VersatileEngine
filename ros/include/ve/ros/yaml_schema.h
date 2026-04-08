#pragma once

#include "ve/core/schema.h"

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
