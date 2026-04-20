#pragma once

#include "ve/global.h"

#include <cstdint>
#include <functional>
#include <string>

namespace ve {

class Node;

namespace service {

class SubscribeService;
class NodeTaskService;

using NodeEventFn = std::function<void(const Node&)>;

VE_API void dispatchNodeProtocol(Node* root, Node* req, Node* rep,
                                 SubscribeService* subscribe = nullptr,
                                 NodeTaskService* tasks = nullptr,
                                 int batchLimit = 500,
                                 bool allowSubscriptions = false,
                                 uint64_t sessionId = 0,
                                 bool allowAsyncEvents = false,
                                 const NodeEventFn& sendEvent = {});

} // namespace service
} // namespace ve
