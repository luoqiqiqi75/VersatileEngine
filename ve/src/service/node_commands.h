// node_commands.h — node-protocol command family + envelope helpers
#pragma once

#include "ve/core/object.h"

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

    // Keep the token through the execution chain and its completion callbacks.
    std::shared_ptr<void> beginAsync()
    {
        auto state = _async;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            ++state->pending;
        }
        return std::shared_ptr<void>(state.get(), [state](void*) {
            std::lock_guard<std::mutex> lock(state->mutex);
            --state->pending;
            state->done.notify_all();
        });
    }

    void waitAsync()
    {
        auto state = _async;
        std::unique_lock<std::mutex> lock(state->mutex);
        state->done.wait(lock, [state] { return state->pending == 0; });
    }

private:
    struct AsyncState {
        std::mutex mutex;
        std::condition_variable done;
        std::size_t pending = 0;
    };
    std::shared_ptr<AsyncState> _async = std::make_shared<AsyncState>();
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
