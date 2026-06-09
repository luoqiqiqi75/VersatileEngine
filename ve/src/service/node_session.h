// node_session.h — per-connection Session (an Object that owns its subscriptions)
#pragma once

#include "ve/core/object.h"

#include <functional>
#include <string>
#include <unordered_map>

namespace ve {

class Node;

namespace service {

// One Session per client connection (ws / tcp / bin).
//
// A subscription is just a connection from a node's NODE_CHANGED/NODE_ACTIVATED
// signal to THIS Object. When the connection closes, the Session is destroyed and
// the Object destructor tears down every connection where it is the observer —
// so subscriptions clean themselves up with no central registry and no bookkeeping.
class VE_API Session : public Object
{
public:
    // Delivers a changed value back to this connection's transport.
    using PushFn = std::function<void(const std::string& path, const Var& value)>;

    Session(Node* root, PushFn push);
    ~Session();

    void subscribe(const std::string& path, bool bubble = false, bool tree = false);
    void unsubscribe(const std::string& path);

private:
    Node* _root = nullptr;
    PushFn _push;
    std::unordered_map<std::string, Node*> _subs;   // path -> subscribed node (for unsubscribe)
};

} // namespace service
} // namespace ve
