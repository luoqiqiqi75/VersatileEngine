// subscribe_service.cpp — SubscribeService: per-node subscription with optional bubble
//
// Default: connects to target node's NODE_CHANGED signal (precise)
// Optional: bubble=true connects to NODE_ACTIVATED for subtree changes
//
// Lifecycle: each subscription has its own Object observer.
//   - Node deleted -> sender connections cleared, callback stops
//   - Session removed -> observer destructed -> alive token killed, callback stops

#include "subscribe_service.h"
#include "ve/core/node.h"
#include "ve/core/var.h"

#include <mutex>
#include <unordered_map>
#include <vector>

namespace ve {
namespace service {

struct SubEntry
{
    std::string path;
    bool bubble;
    Object observer{"_sub"};
};

struct SubscribeService::Private
{
    Node* root = nullptr;
    std::mutex mtx;
    // session -> list of subscriptions (observer lifetime = subscription lifetime)
    std::unordered_map<uint64_t, std::vector<std::unique_ptr<SubEntry>>> sessions;
    PushFn pushFn;
};

SubscribeService::SubscribeService(Node* root)
    : _p(std::make_unique<Private>())
{
    _p->root = root;
}

SubscribeService::~SubscribeService()
{
    stop();
}

void SubscribeService::start()
{
}

void SubscribeService::stop()
{
    std::lock_guard<std::mutex> lock(_p->mtx);
    _p->sessions.clear();
}

void SubscribeService::subscribe(uint64_t session, const std::string& path, bool bubble)
{
    Node* target = path.empty() ? _p->root : _p->root->find(path);
    if (!target) {
        target = _p->root->at(path);
    }
    if (!target) return;

    auto entry = std::make_unique<SubEntry>();
    entry->path = path;
    entry->bubble = bubble;

    if (bubble) {
        target->watchAll(true);
        target->connect<Node::NODE_ACTIVATED>(
            &entry->observer, [this, session, root = _p->root](int64_t signal, void* ptr) {
                if (signal != Node::NODE_CHANGED || !ptr || !_p->pushFn) return;
                auto* src = static_cast<Node*>(ptr);
                _p->pushFn(session, src->path(root), src->get());
            });
    } else {
        target->connect<Node::NODE_CHANGED>(
            &entry->observer, [this, session, path, target](int64_t, void*) {
                if (!_p->pushFn) return;
                _p->pushFn(session, path, target->get());
            });
    }

    std::lock_guard<std::mutex> lock(_p->mtx);
    _p->sessions[session].push_back(std::move(entry));
}

void SubscribeService::unsubscribe(uint64_t session, const std::string& path)
{
    std::lock_guard<std::mutex> lock(_p->mtx);
    auto it = _p->sessions.find(session);
    if (it == _p->sessions.end()) return;

    auto& entries = it->second;
    entries.erase(std::remove_if(entries.begin(), entries.end(),
        [&path](const std::unique_ptr<SubEntry>& e) {
            return e->path == path;
        }), entries.end());

    if (entries.empty()) {
        _p->sessions.erase(it);
    }
}

void SubscribeService::removeSession(uint64_t session)
{
    std::lock_guard<std::mutex> lock(_p->mtx);
    _p->sessions.erase(session);
    // SubEntry destructed -> observer destructed -> alive killed -> callbacks stop
}

void SubscribeService::setPushCallback(PushFn fn)
{
    _p->pushFn = std::move(fn);
}

} // namespace service
} // namespace ve
