// ----------------------------------------------------------------------------
// ve/core/impl/json.h — JSON serialization for Var and Node
// ----------------------------------------------------------------------------
// Internal API. Used by service layer (terminal, http, ws).
// Parse side uses simdjson on-demand; stringify is pure C++.
//
// Node export (exportTree): leaf nodes use Var JSON; non-leaf shape is decided by the first child:
//   - no children: serialize node value (or null if Var is NONE / isNull()).
//   - first child has a non-empty name: JSON object (named keys, optional "_value", "" for anonymous children).
//     Repeated named siblings are not representable in JSON and only the first one is exported.
//   - first child has an empty name: JSON array (children in order, names ignored).
// Parse/import follows unique-key schema semantics. Repeated object keys overwrite earlier ones.
// ----------------------------------------------------------------------------
#pragma once

#include "ve/core/var.h"

namespace ve {

class Node;
namespace schema {
struct ImportOptions;
struct ExportOptions;
}

namespace impl::json {

// Var <-> JSON string
VE_API std::string stringify(const Var& v);
VE_API Var         parse(const std::string& json);

// Node tree <-> JSON string
// exportTree(options) can hide internal "_" children.
// importTree(options) performs merge-style import through Node::copy().
VE_API std::string exportTree(const Node* node, int indent = 2);
VE_API std::string exportTree(const Node* node, const schema::ExportOptions& options);
VE_API bool        importTree(Node* node, const std::string& json);
VE_API bool        importTree(Node* node, const std::string& json, const schema::ImportOptions& options);

} // namespace json
} // namespace ve
