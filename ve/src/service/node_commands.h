// node_commands.h — node-protocol command family + generic dispatch
#pragma once

#include "ve/core/object.h"

namespace ve {

class Node;

namespace service {

// ============================================================================
// Service-layer error codes (not in Result::Code — these are protocol-level).
// ============================================================================
enum : int {
    ERR_INVALID     = -2,
    ERR_NOT_FOUND   = -3,
    ERR_UNSUPPORTED = -4,
};

struct Session : Object
{
    Node* root = nullptr;
    Node* current = nullptr;

    explicit Session(Node* r_n, Node* c_n) : Object("_session"), root(r_n), current(c_n) {}
};

VE_API void registerNodeCommands();

// Process request in ctx ({cmd, params?, id?, ...}), mutate ctx to become the
// reply ({code, message?, data?, id?, ...}).  _-prefixed internal nodes
// (_session) are auto-ignored on export.
// Returns true if a reply should be sent; false = accepted (command owns response).
VE_API bool dispatch(Session* session, Node* ctx);

} // namespace service
} // namespace ve
