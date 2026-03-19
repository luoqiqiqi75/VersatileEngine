// subscribe_service.cpp — SubscribeService: NODE_ACTIVATED push logic
//
// Watches root for signal bubbling, matches changed paths against per-session
// subscriptions, and invokes the Transport-provided push callback.

#include "ve/service/subscribe_service.h"
#include "ve/core/node.h"
#include "ve/core/var.h"
#include "ve/core/impl/json.h"

#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <atomic>

namespace ve {

static bool matchSubscription(const std::string& changedPath, const std::string& subPath)
{
    if (subPath.empty()) return true;
    if (changedPath == subPath) return true;
    if (changedPath.size() > subPath.size()
        && changedPath[subPath.size()] == '/'
        && changedPath.compare(0, subPath.size(), subPath) == 0)
        return true;
    return false;
}

struct SubscribeService::Private
{
    Node* root = nullptr;
    Object observer{"_subscribe_observer"};
    std::mutex mtx;
    std::unordered_map<uint64_t, std::unordered_set<std::string>> sessions;
    std::atomic<int> subCount{0};
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
    _p->root->watchAll(true);

    _p->root->connect<Node::NODE_ACTIVATED>(
        &_p->observer, [this](int64_t signal, void* ptr) {
            if (signal != Node::NODE_CHANGED || !ptr) return;
            if (_p->subCount.load(std::memory_order_relaxed) == 0) return;
            if (!_p->pushFn) return;

            auto* source = static_cast<Node*>(ptr);
            std::string changedPath = source->path(_p->root);
            Var value = source->value();

            std::lock_guard<std::mutex> lock(_p->mtx);
            for (auto& [sessionId, subs] : _p->sessions) {
                for (auto& subPath : subs) {
                    if (matchSubscription(changedPath, subPath)) {
                        _p->pushFn(sessionId, changedPath, value);
                        break;
                    }
                }
            }
        });
}

void SubscribeService::stop()
{
    _p->root->disconnect(&_p->observer);
    _p->root->watchAll(false);
    std::lock_guard<std::mutex> lock(_p->mtx);
    _p->sessions.clear();
    _p->subCount.store(0, std::memory_order_relaxed);
}

void SubscribeService::subscribe(uint64_t session, const std::string& path)
{
    std::lock_guard<std::mutex> lock(_p->mtx);
    _p->sessions[session].insert(path);
    _p->subCount.store(static_cast<int>(_p->sessions.size()), std::memory_order_relaxed);
}

void SubscribeService::unsubscribe(uint64_t session, const std::string& path)
{
    std::lock_guard<std::mutex> lock(_p->mtx);
    auto it = _p->sessions.find(session);
    if (it != _p->sessions.end())
        it->second.erase(path);
}

void SubscribeService::removeSession(uint64_t session)
{
    std::lock_guard<std::mutex> lock(_p->mtx);
    _p->sessions.erase(session);
    _p->subCount.store(static_cast<int>(_p->sessions.size()), std::memory_order_relaxed);
}

void SubscribeService::setPushCallback(PushFn fn)
{
    _p->pushFn = std::move(fn);
}

} // namespace ve
