// ----------------------------------------------------------------------------
// ve/core/impl/xml.h — XML serialization for Node (pugixml based)
// ----------------------------------------------------------------------------
// Per user requirement:
// - Attributes become child nodes with "@" prefix (@id, @class).
// - Text content becomes the node's value().
// - Repeated children use name#N (b#0, b#1).
// - Example: <a id="1" class="A"><b><c/></b><b/>123</a>
//   → a node with value="123", children: @id, @class, b#0(with c), b#1
// - Uses Node::copy() + ImportOptions for merge (auto_insert/remove/update).
// ----------------------------------------------------------------------------
#pragma once

#include "ve/core/var.h"
#include <string>

namespace ve {

class Node;
namespace schema {
struct ImportOptions;
struct ExportOptions;
}

namespace impl::xml {

// Node tree <-> XML string
VE_API std::string exportTree(const Node* node, int indent = 2);
VE_API std::string exportTree(const Node* node, const schema::ExportOptions& options);
VE_API bool        importTree(Node* node, const std::string& xml);
VE_API bool        importTree(Node* node, const std::string& xml, const schema::ImportOptions& options);

} // namespace xml
} // namespace ve
