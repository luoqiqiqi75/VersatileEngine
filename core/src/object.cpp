#include "ve/core/object.h"
#include "ve/core/var.h"

namespace ve {

struct Object::Private
{
    std::string name;
    mutable MutexT mtx;
    Alive alive = std::make_shared<std::atomic<bool>>(true);

    struct Connection {
        Object*    observer;
        ActionT    action;
        LoopRef    loop;
        Alive alive;  // observer's alive token (captured at connect time)
    };
    UnorderedHashMap<int, Vector<Connection>> connections;


    void addConnection(int signal, Object* observer, const ActionT& action,
                        LoopRef loop = {}, Alive token = {})
    {
        connections[signal].push_back({observer, action, std::move(loop), std::move(token)});
    }

    // remove all connections for observer from a signal (internal, already under lock)
    void removeObserver(int signal, Object* observer)
    {
        auto it = connections.find(signal);
        if (it == connections.end()) return;
        auto& vec = it->second;
        vec.erase(std::remove_if(vec.begin(), vec.end(),
            [observer](const Connection& c) { return c.observer == observer; }), vec.end());
    }

    void removeObserverAll(Object* observer)
    {
        for (auto& [_, vec] : connections) {
            vec.erase(std::remove_if(vec.begin(), vec.end(),
                [observer](const Connection& c) { return c.observer == observer; }), vec.end());
        }
    }
};

Object::Object(const std::string& name) : _p(std::make_unique<Private>()) { _p->name = name; }

Object::~Object()
{
    _p->alive->store(false, std::memory_order_release);
    trigger(OBJECT_DELETED, Var());
    LockT lk(_p->mtx);
    _p->connections.clear();
}

const std::string& Object::name() const { return _p->name; }
std::recursive_mutex& Object::mutex() const { return _p->mtx; }

bool Object::hasConnection(int signal)
{
    LockT lk(_p->mtx);
    auto it = _p->connections.find(signal);
    return it != _p->connections.end() && !it->second.empty();
}

bool Object::hasConnection(int signal, Object* observer)
{
    LockT lk(_p->mtx);
    auto it = _p->connections.find(signal);
    if (it == _p->connections.end()) return false;
    for (auto& c : it->second)
        if (c.observer == observer) return true;
    return false;
}

void Object::connect(int signal, Object* observer, const ActionT& action, LoopRef loop)
{
    LockT lk(_p->mtx);
    _p->addConnection(signal, observer, action, std::move(loop),
                       observer ? observer->_p->alive : Alive{});
}

void Object::disconnect(int signal, Object* observer)
{
    LockT lk(_p->mtx);
    _p->removeObserver(signal, observer);
}

void Object::disconnect(Object* observer)
{
    LockT lk(_p->mtx);
    _p->removeObserverAll(observer);
}

void Object::trigger(int signal, const Var& data /*= {}*/)
{
    if (signal != OBJECT_DELETED && isSilent()) return;

    struct Dispatch { Object* observer; ActionT action; LoopRef loop; Alive alive; };
    Vector<Dispatch> callbacks;
    {
        LockT lk(_p->mtx);
        auto it = _p->connections.find(signal);
        if (it != _p->connections.end()) {
            callbacks.reserve(it->second.size());
            for (auto& c : it->second)
                callbacks.push_back({c.observer, c.action, c.loop, c.alive});
        }
    }

    auto sender_alive = _p->alive;
    bool has_dead = false;

    for (auto& d : callbacks) {
        if (d.alive && !d.alive->load(std::memory_order_acquire)) {
            has_dead = true;
            continue;
        }
        if (d.loop) {
            void* ctx = loop::context();
            d.loop.post([sender_alive, action = std::move(d.action), data, ctx]() {
                if (!sender_alive->load(std::memory_order_acquire)) return;
                void* prev = loop::setContext(ctx);
                action(data);
                loop::setContext(prev);
            });
        } else {
            d.action(data);
        }
    }

    if (has_dead) {
        LockT lk(_p->mtx);
        auto it = _p->connections.find(signal);
        if (it != _p->connections.end()) {
            auto& vec = it->second;
            vec.erase(std::remove_if(vec.begin(), vec.end(),
                [](const Private::Connection& c) {
                    return c.alive && !c.alive->load(std::memory_order_acquire);
                }), vec.end());
        }
    }
}

Manager::Manager(const std::string &name) : Object(name)
{
}

Manager::~Manager()
{
    for (auto& kv : *this) delete kv.second;
}

Object* Manager::add(Object* obj, bool delete_if_failed)
{
    if (!obj) return nullptr;
    if (has(obj->name())) {
        if (delete_if_failed) delete obj;
        return nullptr;
    }
    (*this)[obj->name()] = obj;
    return obj;
}

bool Manager::remove(Object *obj, bool auto_delete)
{
    if (!obj) return false;
    bool found = erase(obj->name()) > 0;
    if (!found) {
        for (const auto& kv : *this) {
            if (obj == kv.second) {
                found = erase(kv.first) > 0;
                break;
            }
        }
    }
    if (found && auto_delete) delete obj;
    return found;
}

bool Manager::remove(const std::string &name, bool auto_delete)
{
    Object* obj = get(name);
    if (!obj || erase(name) == 0) return false;
    if (auto_delete) delete obj;
    return true;
}

Object* Manager::get(const std::string &key) const
{
    auto it = find(key);
    return it == end() ? nullptr : it->second;
}

}
