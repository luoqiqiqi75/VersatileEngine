#include "ve/core/object.h"
#include "ve/core/var.h"

namespace ve {

namespace {

thread_local Object* t_sender = nullptr;

struct Token
{
    struct Block {
        std::atomic<unsigned> refs{1};
        std::atomic<bool> alive{true};
        void* owner = nullptr;
    };

    Block* block = nullptr;

    Token() = default;
    explicit Token(Block* b) : block(b) {}

    Token(const Token& other) : block(other.block) { retain(); }
    Token(Token&& other) noexcept : block(other.block) { other.block = nullptr; }

    Token& operator=(const Token& other)
    {
        if (this == &other) return *this;
        release();
        block = other.block;
        retain();
        return *this;
    }

    Token& operator=(Token&& other) noexcept
    {
        if (this == &other) return *this;
        release();
        block = other.block;
        other.block = nullptr;
        return *this;
    }

    ~Token() { release(); }

    static Token create(void* owner = nullptr)
    {
        auto* b = new Block();
        b->owner = owner;
        return Token(b);
    }

    bool dead() const { return block && !block->alive.load(std::memory_order_acquire); }
    void kill() { if (block) block->alive.store(false, std::memory_order_release); }

    template<typename T>
    T* as() const { return block ? static_cast<T*>(block->owner) : nullptr; }

    explicit operator bool() const { return static_cast<bool>(block); }

private:
    void retain()
    {
        if (block) block->refs.fetch_add(1, std::memory_order_relaxed);
    }

    void release()
    {
        if (!block) return;
        if (block->refs.fetch_sub(1, std::memory_order_acq_rel) == 1) delete block;
        block = nullptr;
    }
};

struct SenderScope {
    Object* prev = nullptr;

    explicit SenderScope(Object* sender)
        : prev(t_sender)
    {
        t_sender = sender;
    }

    ~SenderScope() { t_sender = prev; }
};

} // namespace

struct Object::Private
{
    std::string name;
    mutable MutexT mtx;
    Token token;   // tagged with the owning Object* in Object's ctor

    struct Connection {
        ActionT action;
        Loop*   loop = nullptr;
        Token   target;   // observer's token: alive flag + observer address (empty = no observer)
        Token   shot;     // per-connection oneShot token (empty = persistent)

        Object* observer() const { return target.as<Object>(); }
    };
    UnorderedHashMap<SignalT, Vector<Connection>> connections;


    void addConnection(SignalT signal, const ActionT& action, Token target,
                        Loop* loop = nullptr, bool oneShot = false)
    {
        Token shot = oneShot ? Token::create() : Token{};
        connections[signal].push_back({action, loop, std::move(target), std::move(shot)});
    }

    static bool isDead(const Connection& c)
    {
        return c.target.dead() || c.shot.dead();
    }

    // remove all connections for observer from a signal (internal, already under lock)
    void removeObserver(SignalT signal, Object* observer)
    {
        auto it = connections.find(signal);
        if (it == connections.end()) return;
        auto& vec = it->second;
        vec.erase(std::remove_if(vec.begin(), vec.end(),
            [observer](const Connection& c) { return c.observer() == observer; }), vec.end());
    }

    void removeObserverAll(Object* observer)
    {
        for (auto& [_, vec] : connections) {
            vec.erase(std::remove_if(vec.begin(), vec.end(),
                [observer](const Connection& c) { return c.observer() == observer; }), vec.end());
        }
    }
};

Object::Object(const std::string& name) : _p(std::make_unique<Private>())
{
    _p->name  = name;
    _p->token = Token::create(this);   // owner-tagged: connections recover the observer via target.as<Object>()
}

Object::~Object()
{
    _p->token.kill();
    LockT lk(_p->mtx);
    _p->connections.clear();
}

const std::string& Object::name() const { return _p->name; }
std::recursive_mutex& Object::mutex() const { return _p->mtx; }
Object* Object::sender() { return t_sender; }

bool Object::hasConnection(const SignalT signal, const Object* observer) const
{
    LockT lk(_p->mtx);
    auto it = _p->connections.find(signal);
    if (it == _p->connections.end()) return false;
    if (observer) {
        for (auto& c : it->second)
            if (c.observer() == observer) return true;
    } else {
        return !it->second.empty();
    }
    return false;
}

void Object::connect(const SignalT signal, const Object* observer, const ActionT& action, Loop* loop)
{
    LockT lk(_p->mtx);
    _p->addConnection(signal, action,
                       observer ? observer->_p->token : Token{}, loop);
}

void Object::once(const SignalT signal, const Object* observer, const ActionT& action, Loop* loop)
{
    LockT lk(_p->mtx);
    _p->addConnection(signal, action,
                       observer ? observer->_p->token : Token{}, loop, true);
}

void Object::disconnect(SignalT signal, Object* observer)
{
    LockT lk(_p->mtx);
    _p->removeObserver(signal, observer);
}

void Object::disconnect(Object* observer)
{
    LockT lk(_p->mtx);
    _p->removeObserverAll(observer);
}

void Object::trigger(SignalT signal, const Var& data /*= {}*/)
{
    if (isSilent()) return;

    struct Dispatch { ActionT action; Loop* loop; Token target; Token shot; };
    Vector<Dispatch> callbacks;
    {
        LockT lk(_p->mtx);
        auto it = _p->connections.find(signal);
        if (it != _p->connections.end()) {
            callbacks.reserve(it->second.size());
            for (auto& c : it->second)
                callbacks.push_back({c.action, c.loop, c.target, c.shot});
        }
    }

    auto sender = _p->token;
    Object* sender_obj = sender.as<Object>();
    bool has_dead = false;

    for (auto& d : callbacks) {
        if (d.target.dead() || d.shot.dead()) {
            has_dead = true;
            continue;
        }
        if (d.loop) {
            // Queued delivery rechecks sender and receiver liveness when the
            // loop drains the task.
            Token receiver = d.target;
            d.loop->post([action = std::move(d.action), data, sender, receiver, sender_obj]() {
                if (sender.dead() || receiver.dead()) return;
                SenderScope scope(sender_obj);
                action(data);
            });
        } else {
            // Direct delivery runs on the caller's stack.
            SenderScope scope(sender_obj);
            d.action(data);
        }
        if (d.shot) {
            d.shot.kill();
            has_dead = true;
        }
    }

    if (has_dead) {
        LockT lk(_p->mtx);
        auto it = _p->connections.find(signal);
        if (it != _p->connections.end()) {
            auto& vec = it->second;
            vec.erase(std::remove_if(vec.begin(), vec.end(),
                &Private::isDead), vec.end());
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
