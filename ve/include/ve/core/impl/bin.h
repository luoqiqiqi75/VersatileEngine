// ve/core/impl/bin.h — Binary serialization for Var and Node
// Internal API. Hand-rolled little-endian format, CBS-compatible semantics.
#pragma once

#include "ve/core/var.h"
#include <cstdint>

namespace ve {

class Node;

namespace bin {

VE_API Bytes exportTree(const Node* node);
VE_API bool  importTree(Node* node, const uint8_t* data, size_t len);

VE_API void writeVar(const Var& v, Bytes& buf);
VE_API Var  readVar(const uint8_t*& ptr, const uint8_t* end);

} // namespace bin
} // namespace ve
