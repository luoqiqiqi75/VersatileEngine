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

#include <algorithm>
#include <mutex>
#include <memory>
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

struct SharedRegistry
{
    std::mutex mtx;
    std::unordered_map<std::string, size_t> counts;
};

static std::mutex g_registry_mtx;
static std::unordered_map<Node*, std::weak_ptr<SharedRegistry>> g_registries;

static std::shared_ptr<SharedRegistry> sharedRegistryFor(Node* root)
{
    std::lock_guard<std::mutex> lock(g_registry_mtx);
    auto& weak = g_registries[root];
    auto shared = weak.lock();
    if (!shared) {
        shared = std::make_shared<SharedRegistry>();
        weak = shared;
    }
    return shared;
}

static std::string normalizePath(std::string path)
{
    while (!path.empty() && path.front() == '/') {
        path.erase(path.begin());
    }
    while (!path.empty() && path.back() == '/') {
        path.pop_back();
    }
    return path;
}

struct SubscribeService::Private
{
    Node* root = nullptr;
    std::mutex mtx;
    // session -> list of subscriptions (observer lifetime = subscription lifetime)
    std::unordered_map<uint64_t, std::vector<std::unique_ptr<SubEntry>>> sessions;
    PushFn pushFn;
    std::shared_ptr<SharedRegistry> shared;

    void addCount(const std::string& path)
    {
        if (!shared) {
            return;
        }
        std::lock_guard<std::mutex> lock(shared->mtx);
        ++shared->counts[path];
    }

    void removeCount(const std::string& path)
    {
        if (!shared) {
            return;
        }
        std::lock_guard<std::mutex> lock(shared->mtx);
        auto it = shared->counts.find(path);
        if (it == shared->counts.end()) {
            return;
        }
        if (it->second > 1) {
            --it->second;
        } else {
            shared->counts.erase(it);
        }
    }
};

SubscribeService::SubscribeService(Node* root)
    : _p(std::make_unique<Private>())
{
    _p->root = root;
    _p->shared = sharedRegistryFor(root);
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
    for (const auto& [sid, entries] : _p->sessions) {
        for (const auto& entry : entries) {
            _p->removeCount(entry->path);
        }
    }
    _p->sessions.clear();
}

void SubscribeService::subscribe(uint64_t session, const std::string& path, bool bubble)
{
    std::string normalized = normalizePath(path);
    Node* target = normalized.empty() ? _p->root : _p->root->find(normalized);
    if (!target) {
        target = _p->root->at(normalized);
    }
    if (!target) return;

    auto entry = std::make_unique<SubEntry>();
    entry->path = normalized;
    entry->bubble = bubble;

    if (bubble) {
        target->watchAll(true);
        target->connect<Node::NODE_ACTIVATED>(
            &entry->observer, [this, session, root = _p->root](const Var& data) {
                if (!data.isList() || data.toList().size() < 2) return;
                auto signal = data[0].toInt64();
                if (signal != Node::NODE_CHANGED) return;
                auto* src = static_cast<Node*>(data[1].toPointer());
                if (!src || !_p->pushFn) return;
                _p->pushFn(session, src->path(root), src->get());
            });
    } else {
        target->connect<Node::NODE_CHANGED>(
            &entry->observer, [this, session, normalized, target]() {
                if (!_p->pushFn) return;
                _p->pushFn(session, normalized, target->get());
            });
    }

    std::lock_guard<std::mutex> lock(_p->mtx);
    _p->sessions[session].push_back(std::move(entry));
    _p->addCount(normalized);
}

void SubscribeService::unsubscribe(uint64_t session, const std::string& path)
{
    std::lock_guard<std::mutex> lock(_p->mtx);
    std::string normalized = normalizePath(path);
    auto it = _p->sessions.find(session);
    if (it == _p->sessions.end()) return;

    auto& entries = it->second;
    entries.erase(std::remove_if(entries.begin(), entries.end(),
        [this, &normalized](const std::unique_ptr<SubEntry>& e) {
            if (e->path == normalized) {
                _p->removeCount(e->path);
                return true;
            }
            return false;
        }), entries.end());

    if (entries.empty()) {
        _p->sessions.erase(it);
    }
}

void SubscribeService::removeSession(uint64_t session)
{
    std::lock_guard<std::mutex> lock(_p->mtx);
    auto it = _p->sessions.find(session);
    if (it != _p->sessions.end()) {
        for (const auto& entry : it->second) {
            _p->removeCount(entry->path);
        }
    }
    _p->sessions.erase(session);
    // SubEntry destructed -> observer destructed -> alive killed -> callbacks stop
}

void SubscribeService::setPushCallback(PushFn fn)
{
    _p->pushFn = std::move(fn);
}

size_t SubscribeService::getSubscriberCount(const std::string& path) const
{
    if (!_p->shared) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(_p->shared->mtx);
    auto it = _p->shared->counts.find(normalizePath(path));
    return it == _p->shared->counts.end() ? 0 : it->second;
}

} // namespace service
} // namespace ve
