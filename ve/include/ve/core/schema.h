// ----------------------------------------------------------------------------
// schema.h — Schema definition + format-based Node serialization
// ----------------------------------------------------------------------------
// SchemaField / Schema: describe the expected structure of a Node subtree.
// schema::exportAs<F> / importAs<F>: tag-dispatched Node serialization.
//
// Format tags: schema::JsonS, schema::BinS, schema::XmlS, schema::VarS, schema::MdS
// Customization point: schema::SchemaTraits<Format>
// Runtime extension: schema::registerSchemaFormat() for plugin formats.
// Merge-style import goes through Node::copy(); importNode takes Node::CopyFlag
// bits (copy_flags) — see node.h for the per-flag semantics.
// ExportOptions<Format> drives formatting and hidden-node filtering; each
// format specializes it with only the knobs it supports (VarS/BinS have no
// indent).
// auto_ignore defaults to true: "_"-prefixed internal children stay unexported.
// JsonS is schema-oriented and ignores repeated named siblings.
// BinS preserves the full tree, including repeated names and order.
// XmlS uses pugixml; attrs stored as Dict in Var value.
// VarS exports Node tree to a single Var (Dict/List/Value).
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

// --- Format tags -----------------------------------------------------------

namespace schema {

struct JsonS
{
    struct ExportOptions
    {
        int indent = 2;
        bool auto_ignore = true;
    };

    static ExportOptions compact() { return {0, true}; }
};
struct BinS
{
    struct ExportOptions { bool auto_ignore = true; };
};
struct XmlS // pugixml based; attrs stored as Dict in Var value; only NODE_CHANGED for attr CRUD
{
    struct ExportOptions { int indent = 2; bool auto_ignore = true; };
};
struct VarS // Var based; exports Node tree to a single Var (Dict/List/Value)
{
    struct ExportOptions { bool auto_ignore = true; };
};
struct MdS // Markdown based; headings -> Node hierarchy, content -> _content child
{
    struct ExportOptions { int indent = 2; bool auto_ignore = true; };
};

// --- SchemaTraits (customization point) ------------------------------------
// Specialize for each format tag to provide exportNode / importNode.
// Each specialization carries its own nested ExportOptions with only the knobs
// the format supports; pass it braced ({0}, {.indent = 0}) or spell it via the
// schema::ExportOptions<Format> alias below.

template<typename Format>
struct SchemaTraits;

template<>
struct SchemaTraits<JsonS>
{
    VE_API static std::string exportNode(const Node* node, int indent = 2);
    VE_API static std::string exportNode(const Node* node, const JsonS::ExportOptions& options);
    VE_API static bool        importNode(Node* node, const std::string& data);
    VE_API static bool        importNode(Node* node, const std::string& data, int copy_flags);
};

template<>
struct SchemaTraits<BinS>
{
    VE_API static Bytes exportNode(const Node* node);
    VE_API static Bytes exportNode(const Node* node, const BinS::ExportOptions& options);
    VE_API static bool  importNode(Node* node, const uint8_t* data, size_t len);
    VE_API static bool  importNode(Node* node, const uint8_t* data, size_t len, int copy_flags);
};

template<>
struct SchemaTraits<XmlS>
{
    VE_API static std::string exportNode(const Node* node, int indent = 2);
    VE_API static std::string exportNode(const Node* node, const XmlS::ExportOptions& options);
    VE_API static bool        importNode(Node* node, const std::string& data);
    VE_API static bool        importNode(Node* node, const std::string& data, int copy_flags);
};

template<>
struct SchemaTraits<VarS>
{
    VE_API static Var exportNode(const Node* node);
    VE_API static Var exportNode(const Node* node, const VarS::ExportOptions& options);
    VE_API static bool    importNode(Node* node, const Var& data);
    VE_API static bool    importNode(Node* node, const Var& data, int copy_flags);
};

template<>
struct SchemaTraits<MdS>
{
    VE_API static std::string exportNode(const Node* node, int indent = 2);
    VE_API static std::string exportNode(const Node* node, const MdS::ExportOptions& options);
    VE_API static bool        importNode(Node* node, const std::string& data);
    VE_API static bool        importNode(Node* node, const std::string& data, int copy_flags);
};

// --- Convenience functions -------------------------------------------------

template<typename Format, typename... Args>
auto exportAs(const Node* node, Args&&... args)
    -> decltype(SchemaTraits<Format>::exportNode(node, std::forward<Args>(args)...))
{
    return SchemaTraits<Format>::exportNode(node, std::forward<Args>(args)...);
}

template<typename Format, typename... Args>
bool importAs(Node* node, Args&&... args)
{
    return SchemaTraits<Format>::importNode(node, std::forward<Args>(args)...);
}

// --- Runtime format registry for terminal commands ------------------------

struct VE_API SchemaFormatHandler
{
    std::function<std::string(const Node*)> exportFn;
    std::function<bool(Node*, const std::string&)> importFn;
};

VE_API void registerSchemaFormat(const std::string& name, SchemaFormatHandler handler);
VE_API bool hasSchemaFormat(const std::string& name);
VE_API std::vector<std::string> schemaFormatNames();
VE_API std::string exportSchemaFormat(const std::string& name, const Node* node);
VE_API bool importSchemaFormat(const std::string& name, Node* node, const std::string& data);

} // namespace schema
} // namespace ve
