// subscribe_service.h — internal: path subscriptions + NODE_CHANGED push (NodeWsServer, BinTcpServer)
#pragma once

#include "ve/core/var.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace ve {

class Node;

namespace service {

class VE_API SubscribeService
{
public:
    using PushFn = std::function<void(uint64_t session, const std::string& path, const Var& value)>;

    explicit SubscribeService(Node* root);
    ~SubscribeService();

    void start();
    void stop();

    void subscribe(uint64_t session, const std::string& path, bool bubble = false, bool tree = false);
    void unsubscribe(uint64_t session, const std::string& path);
    void removeSession(uint64_t session);

    void setPushCallback(PushFn fn);

    size_t getSubscriberCount(const std::string& path) const;

private:
    VE_DECLARE_UNIQUE_PRIVATE
};

} // namespace service
} // namespace ve
