#include "ve/core/loop.h"

// standalone asio (header-only, from deps/asio2/3rd)
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <asio/io_context.hpp>
#include <asio/executor_work_guard.hpp>
#include <asio/post.hpp>

#include <atomic>
#include <mutex>
#include <optional>
#include <thread>
#include <deque>
#include <vector>
#include <string>
#include <unordered_map>

namespace ve {

// ============================================================================
// LoopContext<AsioContext> — asio io_context backend, single worker thread
// ============================================================================

using AsioWorkGuard = asio::executor_work_guard<asio::io_context::executor_type>;

struct LoopContext<AsioContext>::State
{
    asio::io_context io;
    std::optional<AsioWorkGuard> guard;
    std::thread worker;
    std::atomic<bool> is_running{false};
    std::mutex mtx;   // protects start/stop

    State()
        : io(1)   // concurrency hint: single thread
        , guard(asio::make_work_guard(io))
    {}
};

LoopContext<AsioContext>::LoopContext()
    : _s(new State())
{}

LoopContext<AsioContext>::~LoopContext()
{
    stop();
    delete _s;
}

void LoopContext<AsioContext>::post(Task task)
{
    asio::post(_s->io, std::move(task));
}

bool LoopContext<AsioContext>::start()
{
    std::lock_guard<std::mutex> lk(_s->mtx);
    if (_s->is_running) return false;

    _s->io.restart();
    _s->guard.emplace(asio::make_work_guard(_s->io));
    _s->is_running = true;

    State* st = _s;
    _s->worker = std::thread([st]() { st->io.run(); });
    return true;
}

bool LoopContext<AsioContext>::stop()
{
    std::lock_guard<std::mutex> lk(_s->mtx);
    if (!_s->is_running) return false;

    _s->is_running = false;
    _s->guard.reset();     // drop work guard → io_context::run() returns when idle
    _s->io.stop();         // interrupt immediately

    if (_s->worker.joinable()) _s->worker.join();
    return true;
}

bool LoopContext<AsioContext>::running() const
{
    return _s->is_running;
}

bool LoopContext<AsioContext>::isCurrentThread() const
{
    // asio io_context exposes this directly. True iff the calling thread is
    // currently executing inside this io_context's run()/run_one() — i.e. the
    // worker thread.
    return _s && _s->io.get_executor().running_in_this_thread();
}

size_t LoopContext<AsioContext>::processEvents()
{
    if (!_s) return 0;
    // Run all currently-ready handlers without blocking; returns the number run.
    // Safe to call recursively from the worker thread (asio re-enters cleanly) —
    // used by re-entrant callSync to drain its target loop instead of CV-waiting.
    return _s->io.poll();
}

// ============================================================================
// LoopContext<AsioPool> — asio io_context backend, N worker threads
// ============================================================================

struct LoopContext<AsioPool>::State
{
    asio::io_context io;
    std::optional<AsioWorkGuard> guard;
    std::vector<std::thread> workers;
    unsigned threads;
    std::atomic<bool> is_running{false};
    std::mutex mtx;   // protects start/stop

    explicit State(unsigned n)
        : io(static_cast<int>(n))             // concurrency hint: N threads
        , guard(asio::make_work_guard(io))
        , threads(n ? n : 1)
    {}
};

LoopContext<AsioPool>::LoopContext(unsigned threads)
    : _s(new State(threads ? threads : 1))
{}

LoopContext<AsioPool>::~LoopContext()
{
    stop();
    delete _s;
}

void LoopContext<AsioPool>::post(Task task)
{
    asio::post(_s->io, std::move(task));
}

bool LoopContext<AsioPool>::start()
{
    std::lock_guard<std::mutex> lk(_s->mtx);
    if (_s->is_running) return false;

    _s->io.restart();
    _s->guard.emplace(asio::make_work_guard(_s->io));
    _s->is_running = true;

    State* st = _s;
    _s->workers.reserve(st->threads);
    for (unsigned i = 0; i < st->threads; ++i)
        _s->workers.emplace_back([st]() { st->io.run(); });
    return true;
}

bool LoopContext<AsioPool>::stop()
{
    std::lock_guard<std::mutex> lk(_s->mtx);
    if (!_s->is_running) return false;

    _s->is_running = false;
    _s->guard.reset();
    _s->io.stop();

    for (auto& w : _s->workers)
        if (w.joinable()) w.join();
    _s->workers.clear();
    return true;
}

bool LoopContext<AsioPool>::running() const
{
    return _s->is_running;
}

bool LoopContext<AsioPool>::isCurrentThread() const
{
    // True iff the caller is one of this pool's worker threads.
    return _s && _s->io.get_executor().running_in_this_thread();
}

size_t LoopContext<AsioPool>::processEvents()
{
    if (!_s) return 0;
    return _s->io.poll();
}

// ============================================================================
// loop:: — current task token (thread-local; set only by Loop::post)
// ============================================================================

// The token of the task currently executing on this thread's loop. Carries the
// scheduler's identity (token.as<T>()) + liveness. Set/restored by Loop::post
// via TokenScope; there is no external setter.
thread_local Token t_token;

namespace {
struct TokenScope {
    Token prev;
    explicit TokenScope(const Token& cur) : prev(std::move(t_token)) { t_token = cur; }
    ~TokenScope() { t_token = std::move(prev); }
};
} // anonymous

Token loop::token() { return t_token; }

// ============================================================================
// Loop — runtime methods (here, not the header, because post() drives the
// thread_local context above).
// ============================================================================

void Loop::post(Task task) const
{
    if (!_ops) return;
    if (task.guards.empty()) {
        _ops->post(std::move(task));   // unguarded: run as-is
        return;
    }
    // Guarded: re-check liveness when the loop drains the task (a guard may die
    // between enqueue and execution), and expose owner (guards[0]) as loop::token().
    _ops->post(Task{[task = std::move(task)]() {
        if (!task.alive()) return;          // a guard died after enqueue → drop
        TokenScope _scope(task.owner());
        task();
    }});
}

bool   Loop::start()           const { return _ops && _ops->start(); }
bool   Loop::stop()            const { return _ops && _ops->stop(); }
bool   Loop::isRunning()       const { return _ops && _ops->running(); }
bool   Loop::isCurrentThread() const { return _ops && _ops->isCurrentThread(); }
size_t Loop::processEvents()   const { return _ops ? _ops->processEvents() : 0; }

// ============================================================================
// loop store — owns every backend; maps names → lazy factories
// ============================================================================
//
// Intentional-leak singleton (like Pool / ve::factory): owns every LoopContext
// and Ops for the life of the process. `entries` is a deque so adopted Ops keep
// stable addresses (handles are bare pointers into it). `named` records a lazy
// factory per name plus the handle once materialized — reg only records,
// instance() runs the factory once. Nothing is ever erased ⇒ no dangling.

namespace {

struct Entry {
    void*       impl;
    void      (*deleter)(void*);   // unused in practice (leak), kept for symmetry
    Loop::Ops   ops;
};

struct Named {
    loop::Factory make;
    const Loop::Ops* ops = nullptr;   // null until first instance() materializes it
};

struct LoopStore {
    std::recursive_mutex mtx;          // recursive: instance() runs a factory that may re-enter
    std::deque<Entry> entries;         // stable addresses for &ops
    std::unordered_map<std::string, Named> named;

    static LoopStore& instance() { static auto* s = new LoopStore(); return *s; }
};

} // anonymous

const Loop::Ops* loop::storeAdopt(void* impl, Loop::Ops ops, void (*deleter)(void*))
{
    auto& st = LoopStore::instance();
    std::lock_guard<std::recursive_mutex> lk(st.mtx);
    st.entries.push_back(Entry{impl, deleter, std::move(ops)});
    return &st.entries.back().ops;
}

bool loop::reg(const std::string& name, Factory make)
{
    auto& st = LoopStore::instance();
    std::lock_guard<std::recursive_mutex> lk(st.mtx);
    if (st.named.count(name)) return false;   // first registration wins
    st.named[name] = Named{std::move(make), nullptr};
    return true;
}

Loop loop::instance(const std::string& name)
{
    auto& st = LoopStore::instance();
    std::lock_guard<std::recursive_mutex> lk(st.mtx);
    auto it = st.named.find(name);
    if (it == st.named.end()) return Loop();          // never registered
    Named& n = it->second;
    if (!n.ops) {                                     // materialize once
        Loop l = n.make ? n.make() : Loop();
        n.ops = l ? l._opsPtr() : nullptr;
    }
    return Loop(n.ops);
}

// ============================================================================
// Built-in backend factories — backend type stays in this TU
// ============================================================================

loop::Factory loop::asio()
{
    return [] { return Loop::from<AsioContext>(); };
}

loop::Factory loop::asioPool(unsigned threads)
{
    return [threads] { return Loop::from<AsioPool>(threads); };
}

// ============================================================================
// loop:: — the two official loops (intentional-leak, started once)
// ============================================================================
//
// reg installs the default factory only if no host (e.g. veQt) already claimed
// the name — first registration wins, so an earlier reg("main", ...) pre-empts.

const Loop& loop::main()
{
    static Loop* s = [] {
        loop::reg("main", loop::asio());      // no-op if already registered
        auto* l = new Loop(loop::instance("main"));
        l->start();
        return l;
    }();
    return *s;
}

const Loop& loop::pool()
{
    static Loop* s = [] {
        loop::reg("pool", loop::asioPool(4));
        auto* l = new Loop(loop::instance("pool"));
        l->start();
        return l;
    }();
    return *s;
}

} // namespace ve
