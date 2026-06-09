// node_session.cpp — Session: per-connection subscriptions via Object connect
//
// Default : connect to the target's NODE_CHANGED (precise scalar push)
// bubble  : connect to NODE_ACTIVATED for subtree changes
// tree    : push the whole subtree on change
//
// Teardown: unsubscribe(path) disconnects this observer from that node; ~Session
// (Object destructor) disconnects everything at once — no alive bookkeeping here.

#include "node_session.h"

#include "ve/core/node.h"
#include "ve/core/schema.h"

namespace ve {
namespace service {

static std::string normalizePath(std::string path)
{
    while (!path.empty() && path.front() == '/') path.erase(path.begin());
    while (!path.empty() && path.back() == '/') path.pop_back();
    return path;
}

Session::Session(Node* root, PushFn push)
    : Object("_session"), _root(root), _push(std::move(push))
{
}

Session::~Session() = default;   // Object dtor disconnects all subscriptions

void Session::subscribe(const std::string& path, bool bubble, bool tree)
{
    if (!_root) return;
    std::string norm = normalizePath(path);
    if (_subs.count(norm)) return;   // already subscribed on this path

    Node* target = norm.empty() ? _root : _root->find(norm);
    if (!target) target = _root->at(norm);
    if (!target) return;
    _subs[norm] = target;

    if (bubble) {
        target->watchAll(true);
        target->connect<Node::NODE_ACTIVATED>(this, [this, root = _root](const Var& data) {
            if (!data.isList() || data.toList().size() < 2) return;
            if (data[0].toInt64() != Node::NODE_CHANGED) return;
            auto* src = static_cast<Node*>(data[1].toPointer());
            if (src && _push) _push(src->path(root), src->get());
        });
    } else if (tree) {
        target->connect<Node::NODE_CHANGED>(this, [this, norm, target]() {
            if (_push) _push(norm, schema::exportAs<schema::VarS>(target));
        });
    } else {
        target->connect<Node::NODE_CHANGED>(this, [this, norm, target]() {
            if (_push) _push(norm, target->get());
        });
    }
}

void Session::unsubscribe(const std::string& path)
{
    std::string norm = normalizePath(path);
    auto it = _subs.find(norm);
    if (it == _subs.end()) return;
    it->second->disconnect(this);   // drop this observer's connection on that node
    _subs.erase(it);
}

} // namespace service
} // namespace ve
