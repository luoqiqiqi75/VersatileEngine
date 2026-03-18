// ----------------------------------------------------------------------------
// ve/core/impl/bin.h — Binary serialization for Var and Node
// ----------------------------------------------------------------------------
// Internal API. Hand-rolled little-endian format, CBS-compatible semantics.
// No third-party dependency (no cereal, no QDataStream).
// ----------------------------------------------------------------------------
#pragma once

#include "ve/core/var.h"
#include <vector>
#include <cstdint>

namespace ve {

class Node;

namespace bin {

// Node tree <-> binary
VE_API std::vector<uint8_t> exportTree(const Node* node);
VE_API bool                 importTree(Node* node, const uint8_t* data, size_t len);

// Var <-> binary (building blocks, also usable by convert<T>::toBytes)
VE_API void writeVar(const Var& v, std::vector<uint8_t>& buf);
VE_API Var  readVar(const uint8_t*& ptr, const uint8_t* end);

} // namespace bin
} // namespace ve
