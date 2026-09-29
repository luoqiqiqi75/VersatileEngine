// node_commands.h — node-protocol command family + envelope helpers
#pragma once

#include "ve/core/object.h"

#include <functional>

namespace ve {

class Node;
class Factory;
class Pipeline;
struct Result;

namespace service {

enum : int {
    ERR_INVALID     = -2,
    ERR_NOT_FOUND   = -3,
    ERR_UNSUPPORTED = -4,
};

struct Session : Object
{
    using SendFn = std::function<void(std::string)>;

    Node* root = nullptr;
    Node* current = nullptr;

    SendFn send;

    explicit Session(Node* r_n, Node* c_n, SendFn s = {})
        : Object("_session"), root(r_n), current(c_n), send(std::move(s)) {}
};

VE_API void registerNodeCommands(Factory& f);

struct CmdRef { Factory* factory = nullptr; std::string key; };
VE_API CmdRef resolveCmd(Node* ctx);

// Mutate pipe.contextNode() into reply format (erase cmd/params, set code/message).
// Returns true if reply should be sent; false = accepted.
VE_API bool finalizeReply(Pipeline& pipe);

using NodeReply = std::function<void(const Node&)>;
// Executes an already parsed envelope. Foreground requests stay serial on the
// caller's connection loop; async requests acknowledge and dispatch to VE loops.
VE_API Result executeNodeRequest(Pipeline pipe, const std::shared_ptr<Session>& session,
                                NodeReply reply, bool push_async_result = true);

} // namespace service
} // namespace ve
