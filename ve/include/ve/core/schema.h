// ----------------------------------------------------------------------------
// schema.h — Schema definition + format-based Node serialization
// ----------------------------------------------------------------------------
// SchemaField / Schema: describe the expected structure of a Node subtree.
// schema::exportAs<F> / importAs<F>: tag-dispatched Node serialization.
//
// Format tags: schema::Json, schema::Bin
// Customization point: schema::SchemaTraits<Format>
// ImportOptions drive merge-style import.
// auto_update=false => set() always emits changed on copied nodes.
// auto_update=true  => update() suppresses unchanged value signals.
// ExportOptions drive formatting and hidden-node filtering.
// Json is schema-oriented and ignores repeated named siblings.
// Bin preserves the full tree, including repeated names and order.
// ----------------------------------------------------------------------------
#pragma once

#include "ve/global.h"
#include "ve/core/base.h"
#include <string>
#include <memory>

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

struct Json {};
struct Bin  {};
struct Xml  {};  // pugixml based; attrs stored as Dict in Var value; only NODE_CHANGED for attr CRUD

struct ImportOptions {
    bool auto_insert  = true;
    bool auto_remove  = false;
    bool auto_update  = false;
};

struct ExportOptions {
    int  indent      = 2;
    bool auto_ignore = false;
};

// --- SchemaTraits (customization point) ------------------------------------
// Specialize for each format tag to provide exportNode / importNode.

template<typename Format>
struct SchemaTraits;

template<>
struct SchemaTraits<Json>
{
    VE_API static std::string exportNode(const Node* node, int indent = 2);
    VE_API static std::string exportNode(const Node* node, const ExportOptions& options);
    VE_API static bool        importNode(Node* node, const std::string& data);
    VE_API static bool        importNode(Node* node, const std::string& data, const ImportOptions& options);
};

template<>
struct SchemaTraits<Bin>
{
    VE_API static Bytes exportNode(const Node* node);
    VE_API static Bytes exportNode(const Node* node, const ExportOptions& options);
    VE_API static bool  importNode(Node* node, const uint8_t* data, size_t len);
    VE_API static bool  importNode(Node* node, const uint8_t* data, size_t len, const ImportOptions& options);
};

template<>
struct SchemaTraits<Xml>
{
    VE_API static std::string exportNode(const Node* node, int indent = 2);
    VE_API static std::string exportNode(const Node* node, const ExportOptions& options);
    VE_API static bool        importNode(Node* node, const std::string& data);
    VE_API static bool        importNode(Node* node, const std::string& data, const ImportOptions& options);
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

} // namespace schema
} // namespace ve
