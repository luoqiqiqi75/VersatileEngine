#include "ve/core/object.h"
#include "ve/core/log.h"
#include "ve/core/loop.h"
#include "ve/core/var.h"

#include <algorithm>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

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

// Keeps a loop's tick from touching a dead Object. The gate outlives the Object
// (the tick holds a shared_ptr); ~Object closes it and drains in-flight ticks.
struct TimerGate
{
    std::mutex mtx;
    std::condition_variable cv;
    bool alive = true;
    std::vector<std::thread::id> inflight;

    // Returns false when the Object is gone and the tick must be dropped.
    bool enter()
    {
        std::lock_guard<std::mutex> lk(mtx);
        if (!alive) return false;
        inflight.push_back(std::this_thread::get_id());
        return true;
    }

    void leave()
    {
        std::unique_lock<std::mutex> lk(mtx);
        auto it = std::find(inflight.begin(), inflight.end(), std::this_thread::get_id());
        if (it != inflight.end()) inflight.erase(it);
        lk.unlock();
        cv.notify_all();
    }

    // Waits out ticks running on other threads. A tick that destroys its own
    // owner is this very call stack and cannot be waited on — excluded, so the
    // pathological case degrades instead of deadlocking.
    void close()
    {
        std::unique_lock<std::mutex> lk(mtx);
        alive = false;
        const auto self = std::this_thread::get_id();
        cv.wait(lk, [&] {
            return std::none_of(inflight.begin(), inflight.end(),
                                [&](std::thread::id t) { return t != self; });
        });
    }
};

// Scoped enter/leave — a throwing slot must not strand close() forever.
struct TimerPass
{
    TimerGate* gate = nullptr;

    explicit TimerPass(const std::shared_ptr<TimerGate>& g)
        : gate(g->enter() ? g.get() : nullptr)
    {}

    ~TimerPass() { if (gate) gate->leave(); }

    explicit operator bool() const { return gate != nullptr; }
};

} // namespace

struct Object::Private
{
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

    struct Timer {
        Loop*             loop   = nullptr;
        Loop::TimerHandle handle = 0;
        int64_t           count  = 0;
        bool              repeat = true;
    };
    UnorderedHashMap<SignalT, Timer> timers;
    std::shared_ptr<TimerGate> gate = std::make_shared<TimerGate>();


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

Object::Object(const std::string& name) : Entity(name), _p(std::make_unique<Private>())
{
    _p->token = Token::create(this);   // owner-tagged: connections recover the observer via target.as<Object>()
}

Object::~Object()
{
    killTimers();
    _p->gate->close();
    _p->token.kill();
    LockT lk(_p->mtx);
    _p->connections.clear();
}

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

void Object::disconnectAll(SignalT signal)
{
    LockT lk(_p->mtx);
    _p->connections.erase(signal);
}

void Object::disconnectAll()
{
    LockT lk(_p->mtx);
    _p->connections.clear();
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

// ============================================================================
// Timers
// ============================================================================

Object::SignalT Object::startTimer(uint64_t ms, bool repeat, Loop* lp)
{
    if (!lp) lp = loop::current();
    if (!lp) lp = loop::main();
    if (!lp) return 0;

    static std::atomic<SignalT> seq{TIMER_BASE};
    const SignalT id = seq.fetch_add(1, std::memory_order_relaxed);

    auto gate = _p->gate;

    // Held across addTimer(): a tick that fires on the loop thread before
    // addTimer() even returns blocks on this same lock, so it can never observe
    // a half-registered timer. Lock order is always Object::mtx -> the loop's
    // timer mutex; killTimer() releases this lock before calling back into the
    // loop, so the reverse edge never exists.
    LockT lk(_p->mtx);
    _p->timers[id] = Private::Timer{lp, 0, 0, repeat};

    const Loop::TimerHandle handle = lp->addTimer(ms, repeat, [this, gate, id] {
        TimerPass pass(gate);
        if (!pass) return;

        int64_t count = 0;
        bool    done  = false;
        {
            LockT tick_lk(_p->mtx);
            auto it = _p->timers.find(id);
            if (it == _p->timers.end()) return;   // killed between fire and here
            count = ++it->second.count;
            done  = !it->second.repeat;
            if (done) _p->timers.erase(it);       // single shot: self-removing
        }

        // A throwing slot would otherwise escape into the loop's run() and take
        // the whole thread down with it.
        try {
            trigger(id, Var(count));
        } catch (const std::exception& e) {
            veLogE << "timer slot threw:" << e.what();
        } catch (...) {
            veLogE << "timer slot threw";
        }

        if (done) disconnectAll(id);
    });

    if (!handle) {
        _p->timers.erase(id);   // the loop has no scheduler
        return 0;
    }
    // Not find()-then-assign: a single-shot tick may already have erased the
    // record, and re-inserting it would strand a timer nothing can kill.
    if (auto* rec = _p->timers.ptr(id)) rec->handle = handle;
    return id;
}

bool Object::killTimer(SignalT id)
{
    Private::Timer timer;
    {
        LockT lk(_p->mtx);
        auto it = _p->timers.find(id);
        if (it == _p->timers.end()) return false;
        timer = it->second;
        _p->timers.erase(it);
    }
    if (timer.loop && timer.handle) timer.loop->removeTimer(timer.handle);
    disconnectAll(id);
    return true;
}

bool Object::hasTimer(SignalT id) const
{
    LockT lk(_p->mtx);
    return _p->timers.find(id) != _p->timers.end();
}

void Object::killTimers()
{
    UnorderedHashMap<SignalT, Private::Timer> dead;
    {
        LockT lk(_p->mtx);
        dead.swap(_p->timers);
    }
    for (auto& kv : dead) {
        if (kv.second.loop && kv.second.handle) kv.second.loop->removeTimer(kv.second.handle);
        disconnectAll(kv.first);
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
