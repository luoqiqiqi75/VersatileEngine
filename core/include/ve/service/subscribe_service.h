// subscribe_service.h — Stateful push service: Node change subscription
//
// Extracted from WsServer. Any Transport that supports push (WS, TCP Binary,
// TCP Text) can share a single SubscribeService instance.
#pragma once

#include "ve/global.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace ve {

class Node;
class Var;

class VE_API SubscribeService
{
public:
    using PushFn = std::function<void(uint64_t session, const std::string& path, const Var& value)>;

    explicit SubscribeService(Node* root);
    ~SubscribeService();

    void start();
    void stop();

    void subscribe(uint64_t session, const std::string& path);
    void unsubscribe(uint64_t session, const std::string& path);
    void removeSession(uint64_t session);

    void setPushCallback(PushFn fn);

private:
    struct Private;
    std::unique_ptr<Private> _p;
};

} // namespace ve
