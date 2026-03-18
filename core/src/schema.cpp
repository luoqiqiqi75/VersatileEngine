// ----------------------------------------------------------------------------
// schema.cpp — Schema::build + SchemaTraits<Json/Bin> implementations
// ----------------------------------------------------------------------------
#include "ve/core/schema.h"
#include "ve/core/node.h"
#include "ve/core/impl/json.h"
#include "ve/core/impl/bin.h"

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
    return json::exportTree(node, indent);
}

bool SchemaTraits<Json>::importNode(Node* node, const std::string& data)
{
    return json::importTree(node, data);
}

// ============================================================================
// SchemaTraits<Bin>
// ============================================================================

Bytes SchemaTraits<Bin>::exportNode(const Node* node)
{
    return bin::exportTree(node);
}

bool SchemaTraits<Bin>::importNode(Node* node, const uint8_t* data, size_t len)
{
    return bin::importTree(node, data, len);
}

} // namespace schema
} // namespace ve
