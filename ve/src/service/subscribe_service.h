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

class SubscribeService
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

} // namespace service
} // namespace ve
