// node_commands.h — shared node-protocol command family + generic dispatch
#pragma once

#include "ve/core/var.h"
#include "ve/core/schema.h"

#include <string>

namespace ve {

class Node;

namespace service {

class Session;

// Register the shared node command family into factory "standard/node".
//   node.get / node.set / node.list / node.put / node.remove / node.trigger
//   command.list
//   subscribe / unsubscribe   (session-scoped: read Session* from ctx/node/session)
// Idempotent — call once (e.g. ServerModule::init).
VE_API void registerNodeCommands();

// Generic, command-based dispatch for the node protocol — the ONE dispatch for
// every node transport (http /ve & /jsonrpc, ws, tcp, bin). Reads the request
// Node (op/path/value/...), runs the matching standard/node command through a
// pipeline, and writes the reply envelope into rep:
//   success -> { ok:true,  [id], data:{...} }
//   failure -> { ok:false, [id], code, error }
// session: per-connection Session enabling subscribe/unsubscribe; null on
// sessionless transports (http, udp), where those ops report "unsupported".
VE_API void dispatchNode(Node* root, Node* req, Node* rep, Session* session = nullptr,
                         int batchLimit = 500);

// Shared render helpers, reused by transport-specific renderers (e.g. http /at).
VE_API Var  exportNodeTree(Node* target, int depth, const schema::ExportOptions& options);
VE_API void writeNodeMeta(Node* out, Node* root, Node* target);

} // namespace service
} // namespace ve
