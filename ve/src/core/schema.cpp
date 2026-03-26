// ----------------------------------------------------------------------------
// schema.cpp — Schema::build + SchemaTraits<Json/Bin> implementations
// ----------------------------------------------------------------------------
#include "ve/core/schema.h"
#include "ve/core/node.h"
#include "ve/core/impl/json.h"
#include "ve/core/impl/bin.h"
#include "ve/core/impl/xml.h"  // for SchemaTraits<Xml>

namespace ve {

// ============================================================================
// Schema::build
// ============================================================================

void Schema::build(Node* node) const
{
    if (!node) return;
    for (auto& f : fields) {
        if (f.sub) f.sub->build(node->append(f.name));
    }
}

// ============================================================================
// SchemaTraits<Json>
// ============================================================================

namespace schema {

std::string SchemaTraits<Json>::exportNode(const Node* node, int indent)
{
    return impl::json::exportTree(node, indent);
}

std::string SchemaTraits<Json>::exportNode(const Node* node, const ExportOptions& options)
{
    return impl::json::exportTree(node, options);
}

bool SchemaTraits<Json>::importNode(Node* node, const std::string& data)
{
    return impl::json::importTree(node, data);
}

bool SchemaTraits<Json>::importNode(Node* node, const std::string& data, const ImportOptions& options)
{
    return impl::json::importTree(node, data, options);
}

// ============================================================================
// SchemaTraits<Bin>
// ============================================================================

Bytes SchemaTraits<Bin>::exportNode(const Node* node)
{
    return impl::bin::exportTree(node);
}

Bytes SchemaTraits<Bin>::exportNode(const Node* node, const ExportOptions& options)
{
    return impl::bin::exportTree(node, options);
}

bool SchemaTraits<Bin>::importNode(Node* node, const uint8_t* data, size_t len)
{
    return impl::bin::importTree(node, data, len);
}

bool SchemaTraits<Bin>::importNode(Node* node, const uint8_t* data, size_t len, const ImportOptions& options)
{
    return impl::bin::importTree(node, data, len, options);
}

// ============================================================================
// SchemaTraits<Xml>
// ============================================================================

std::string SchemaTraits<Xml>::exportNode(const Node* node, int indent)
{
    return impl::xml::exportTree(node, indent);
}

std::string SchemaTraits<Xml>::exportNode(const Node* node, const ExportOptions& options)
{
    return impl::xml::exportTree(node, options);
}

bool SchemaTraits<Xml>::importNode(Node* node, const std::string& data)
{
    return impl::xml::importTree(node, data);
}

bool SchemaTraits<Xml>::importNode(Node* node, const std::string& data, const ImportOptions& options)
{
    return impl::xml::importTree(node, data, options);
}

} // namespace schema
} // namespace ve
