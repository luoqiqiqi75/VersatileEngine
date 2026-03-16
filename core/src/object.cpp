#include "ve/core/object.h"
#include "ve/core/var.h"

namespace ve {

struct Object::Private
{
    std::string name;
    mutable MutexT mtx;

    // signal → [(observer, action, loop)]  — flat vector, no nested hash maps
    struct Connection {
        Object*  observer;
        ActionT  action;   // std::function<void(const Var&)>
        LoopRef  loop;     // optional: if set, action is posted to this loop
    };
    UnorderedHashMap<int, Vector<Connection>> connections;

    // track which observer pairs already have cross-OBJECT_DELETED links
    // stored as min/max pointer pair to deduplicate
    struct PtrPair {
        Object* a; Object* b;
        bool operator==(const PtrPair& o) const { return a == o.a && b == o.b; }
    };
    struct PtrPairHash {
        std::size_t operator()(const PtrPair& p) const {
            auto h1 = std::hash<void*>{}(p.a);
            auto h2 = std::hash<void*>{}(p.b);
            return h1 ^ (h2 * 0x9e3779b97f4a7c15ULL);
        }
    };
    std::unordered_set<PtrPair, PtrPairHash> cross_links;

    // add connection (internal, already under lock)
    void addConnection(int signal, Object* observer, const ActionT& action, LoopRef loop = {})
    {
        connections[signal].push_back({observer, action, std::move(loop)});
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

    // remove observer from all signals (internal, already under lock)
    void removeObserverAll(Object* observer)
    {
        for (auto& [_, vec] : connections) {
            vec.erase(std::remove_if(vec.begin(), vec.end(),
                [observer](const Connection& c) { return c.observer == observer; }), vec.end());
        }
        // also clean cross_links involving observer
        for (auto it = cross_links.begin(); it != cross_links.end(); ) {
            if (it->a == observer || it->b == observer) it = cross_links.erase(it);
            else ++it;
        }
    }
};

Object::Object(const std::string& name) : _p(std::make_unique<Private>()) { _p->name = name; }

Object::~Object()
{
    trigger(OBJECT_DELETED, Var());
    // After OBJECT_DELETED fires, clear all connections so dangling refs can't be called
    LockT lk(_p->mtx);
    _p->connections.clear();
    _p->cross_links.clear();
}

const std::string& Object::name() const { return _p->name; }
std::recursive_mutex& Object::mutex() const { return _p->mtx; }

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
    // Phase 1: add the user connection under lock
    {
        LockT lk(_p->mtx);
        _p->addConnection(signal, observer, action, std::move(loop));
    }

    // Phase 2: set up cross OBJECT_DELETED links (outside this->lock to avoid ABBA)
    if (observer && observer != this && signal != OBJECT_DELETED) {
        // Use ordered pointer pair to ensure only one cross-link per pair
        auto lo = (this < observer) ? this : observer;
        auto hi = (this < observer) ? observer : this;
        Private::PtrPair pp{lo, hi};

        bool need_cross = false;
        {
            LockT lk(_p->mtx);
            if (_p->cross_links.find(pp) == _p->cross_links.end()) {
                _p->cross_links.insert(pp);
                need_cross = true;
            }
        }

        if (need_cross) {
            // Lock in consistent pointer order to prevent ABBA deadlock
            auto* first  = lo;
            auto* second = hi;
            LockT lk1(first->_p->mtx);
            LockT lk2(second->_p->mtx);

            // When observer dies → this disconnects observer
            observer->_p->addConnection(OBJECT_DELETED, this, [this, observer](const Var&) { disconnect(observer); });
            // When this dies → observer disconnects this
            this->_p->addConnection(OBJECT_DELETED, observer, [this, observer](const Var&) { observer->disconnect(this); });

            // Also register in observer's cross_links
            observer->_p->cross_links.insert(pp);
        }
    }
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
    // Phase 1: copy (action + loop) under lock
    struct Dispatch { ActionT action; LoopRef loop; };
    Vector<Dispatch> callbacks;
    {
        LockT lk(_p->mtx);
        auto it = _p->connections.find(signal);
        if (it != _p->connections.end()) {
            callbacks.reserve(it->second.size());
            for (auto& c : it->second)
                callbacks.push_back({c.action, c.loop});
        }
    }
    // Phase 2: dispatch outside lock
    auto def = loop::defaultLoop();
    for (auto& d : callbacks) {
        if (d.loop) {
            d.loop.post([action = std::move(d.action), data]() { action(data); });
        } else if (def) {
            def.post([action = std::move(d.action), data]() { action(data); });
        } else {
            d.action(data);
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
