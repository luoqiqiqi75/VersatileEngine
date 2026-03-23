// ----------------------------------------------------------------------------
// ve/core/impl/json.h — JSON serialization for Var and Node
// ----------------------------------------------------------------------------
// Internal API. Used by service layer (terminal, http, ws).
// Parse side uses simdjson on-demand; stringify is pure C++.
//
// Node export (exportTree): leaf nodes use Var JSON; non-leaf shape is decided by the first child:
//   - no children: serialize node value (or null if Var is NONE / isNull()).
//   - first child has a non-empty name: JSON object (named keys, optional "_value", "" for anonymous children).
//   - first child has an empty name: JSON array (children in order, names ignored).
// ----------------------------------------------------------------------------
#pragma once

#include "ve/core/var.h"
#include <string>
#include <vector>

namespace ve {

class Node;

namespace impl::json {

// Var <-> JSON string
VE_API std::string stringify(const Var& v);
VE_API Var         parse(const std::string& json);

// Node tree <-> JSON string
VE_API std::string exportTree(const Node* node, int indent = 2);
VE_API bool        importTree(Node* node, const std::string& json);

} // namespace json
} // namespace ve
