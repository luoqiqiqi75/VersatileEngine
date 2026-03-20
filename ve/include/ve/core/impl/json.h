// ----------------------------------------------------------------------------
// ve/core/impl/json.h — JSON serialization for Var and Node
// ----------------------------------------------------------------------------
// Internal API. Used by service layer (terminal, http, ws).
// Parse side uses simdjson on-demand; stringify is pure C++.
// ----------------------------------------------------------------------------
#pragma once

#include "ve/core/var.h"
#include <string>
#include <vector>

namespace ve {

class Node;

namespace json {

// Var <-> JSON string
VE_API std::string stringify(const Var& v);
VE_API Var         parse(const std::string& json);

// Node tree <-> JSON string
VE_API std::string exportTree(const Node* node, int indent = 2);
VE_API bool        importTree(Node* node, const std::string& json);

} // namespace json
} // namespace ve
