// ----------------------------------------------------------------------------
// schema.h — Schema definition + format-based Node serialization
// ----------------------------------------------------------------------------
// SchemaField / Schema: describe the expected structure of a Node subtree.
// schema::fromNode<F> / toNode<F>: forward to F::fromNode / F::toNode.
//
// Format structs: schema::JsonS, BinS, XmlS, VarS, MdS
// Runtime extension: schema::registerSchemaFormat() for plugin formats.
// ----------------------------------------------------------------------------
#pragma once

#include "var.h"

namespace ve {

class Node;

// --- SchemaField / Schema --------------------------------------------------

struct SchemaField
{
    std::string name;
    std::shared_ptr<struct Schema> sub;

    SchemaField(const std::string& n) : name(n) {}
    SchemaField(std::string n, std::shared_ptr<struct Schema> s)
        : name(std::move(n)), sub(std::move(s)) {}
};

struct VE_API Schema
{
    Vector<SchemaField> fields;

    Schema() = default;
    Schema(std::initializer_list<SchemaField> f) : fields(f.begin(), f.end()) {}

    static std::shared_ptr<Schema> create(std::initializer_list<SchemaField> f)
    { return std::make_shared<Schema>(f); }

    int fieldCount() const { return static_cast<int>(fields.size()); }
    void build(Node* node) const;
};

// --- Format structs --------------------------------------------------------

namespace schema {

struct JsonS
{
    struct ExportOptions { int indent = 2; bool auto_ignore = true; };
    static ExportOptions compact() { return {0, true}; }

    VE_API static std::string fromNode(const Node* node, int indent = 2);
    VE_API static std::string fromNode(const Node* node, const ExportOptions& options);
    VE_API static bool        toNode(Node* node, const std::string& data);
    VE_API static bool        toNode(Node* node, const std::string& data, int copy_flags);
};

struct BinS
{
    struct ExportOptions { bool auto_ignore = true; };

    VE_API static Bytes fromNode(const Node* node);
    VE_API static Bytes fromNode(const Node* node, const ExportOptions& options);
    VE_API static bool  toNode(Node* node, const uint8_t* data, size_t len);
    VE_API static bool  toNode(Node* node, const uint8_t* data, size_t len, int copy_flags);
};

struct XmlS
{
    struct ExportOptions { int indent = 2; bool auto_ignore = true; };

    VE_API static std::string fromNode(const Node* node, int indent = 2);
    VE_API static std::string fromNode(const Node* node, const ExportOptions& options);
    VE_API static bool        toNode(Node* node, const std::string& data);
    VE_API static bool        toNode(Node* node, const std::string& data, int copy_flags);
};

struct VarS
{
    struct ExportOptions { bool auto_ignore = true; };

    VE_API static Var  fromNode(const Node* node);
    VE_API static Var  fromNode(const Node* node, const ExportOptions& options);
    VE_API static bool toNode(Node* node, const Var& data);
    VE_API static bool toNode(Node* node, const Var& data, int copy_flags);
};

struct MdS
{
    struct ExportOptions { int indent = 2; bool auto_ignore = true; };

    VE_API static std::string fromNode(const Node* node, int indent = 2);
    VE_API static std::string fromNode(const Node* node, const ExportOptions& options);
    VE_API static bool        toNode(Node* node, const std::string& data);
    VE_API static bool        toNode(Node* node, const std::string& data, int copy_flags);
};

// --- Convenience wrappers --------------------------------------------------

template<typename Format, typename... Args>
auto fromNode(const Node* node, Args&&... args)
    -> decltype(Format::fromNode(node, std::forward<Args>(args)...))
{
    return Format::fromNode(node, std::forward<Args>(args)...);
}

template<typename Format, typename... Args>
bool toNode(Node* node, Args&&... args)
{
    return Format::toNode(node, std::forward<Args>(args)...);
}

// --- Runtime format registry for terminal commands ------------------------

struct VE_API SchemaFormatHandler
{
    std::function<std::string(const Node*)> fromNodeFn;
    std::function<bool(Node*, const std::string&)> toNodeFn;
};

VE_API void registerSchemaFormat(const std::string& name, SchemaFormatHandler handler);
VE_API bool hasSchemaFormat(const std::string& name);
VE_API std::vector<std::string> schemaFormatNames();
VE_API std::string schemaFromNode(const std::string& name, const Node* node);
VE_API bool schemaToNode(const std::string& name, Node* node, const std::string& data);

} // namespace schema
} // namespace ve
