#pragma once

#include "ve/global.h"
#include "ve/core/var.h"

#include <cstdint>
#include <functional>
#include <string>

namespace ve {

class Node;

namespace service {

class Session;
class NodeTaskService;

using NodeEventFn = std::function<void(const Node&)>;

struct CommandOutcome {
    enum Code { OK, ACCEPTED, NOT_FOUND, INVALID, FAILED, NO_TASKS };
    Code code = OK;
    Var  data;
    std::string taskId;
    std::string errCode;
    std::string message;
    bool ok() const { return code == OK || code == ACCEPTED; }
};

VE_API CommandOutcome dispatchCommand(const std::string& name, const Var& args, bool wait,
                                      NodeTaskService* tasks = nullptr,
                                      const Var& id = {},
                                      const NodeEventFn& onEvent = {});

// session: per-connection Session (subscriptions). Null on sessionless transports
// (udp), where subscribe/unsubscribe are reported unsupported.
VE_API void dispatchNodeProtocol(Node* root, Node* req, Node* rep,
                                 Session* session = nullptr,
                                 NodeTaskService* tasks = nullptr,
                                 int batchLimit = 500,
                                 bool allowAsyncEvents = false,
                                 const NodeEventFn& sendEvent = {});

} // namespace service
} // namespace ve
