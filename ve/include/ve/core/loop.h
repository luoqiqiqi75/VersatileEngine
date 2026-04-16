// ----------------------------------------------------------------------------
// loop.h — Event loop framework (LoopTraits + LoopRef on std::pair)
// ----------------------------------------------------------------------------
// Copyright (c) 2023-present Thilo and VersatileEngine contributors.
// Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
// See LICENSE file in the project root for full license information.
// ----------------------------------------------------------------------------
//
// Design:
//   Loop<T>        — template loop, backend determined by LoopTraits<T>
//   LoopTraits<T>  — specialization point per backend (static polymorphism)
//   LoopRef        — type-erased handle for cross-template usage
//
//   Core default:  Loop<AsioContext>  (alias: EventLoop)
//   Qt extension:  Loop<QEventLoop>   — specialize LoopTraits<QEventLoop> in veQt
//   RTT extension: Loop<RttActivity>  — specialize LoopTraits<RttActivity> in veRtt
//
// ----------------------------------------------------------------------------

#pragma once

#include <utility>

#include "base.h"

namespace ve {

// ============================================================================
// LoopTraits<T> — specialization point
// ============================================================================
//
// Each specialization must define:
//
//   struct Context;                            // backend state (can be opaque)
//   static Context* create(int threads);       // allocate & init
//   static void     destroy(Context*);         // stop & free
//   static void     post(Context*, Task);      // queue a task (thread-safe)
//   static bool     start(Context*);           // begin processing
//   static bool     stop(Context*);            // stop processing
//   static bool     running(const Context*);   // is active?
//
// Optional extensions (add if backend supports):
//   static void     postDelayed(Context*, Task, std::chrono::milliseconds);
//   static int      postRepeating(Context*, Task, std::chrono::milliseconds);
//   static void     cancel(Context*, int timer_id);
//

template<typename T>
struct LoopTraits;   // primary: intentionally undefined — specialize per backend

struct LoopRef;


// ============================================================================
// Loop<T> — generic event loop
// ============================================================================
//
// Wraps a backend via LoopTraits<T>. Non-copyable, non-movable.
//
// Usage:
//   EventLoop main_loop("main");          // asio (default)
//   main_loop.start();
//   main_loop.post([]{ doWork(); });
//   main_loop.stop();
//
//   // In veQt:
//   Loop<QEventLoop> qt_loop("qt");       // Qt backend
//

template<typename T>
class Loop
{
    using Traits  = LoopTraits<T>;
    using Context = typename Traits::Context;

    Context*    _ctx;
    std::string _name;
    Alive  _alive = Alive::create();

public:
    explicit Loop(const std::string& name = "", int threads = 1)
        : _ctx(Traits::create(threads)), _name(name) {}

    ~Loop() {
        _alive.kill();
        if (_ctx) { Traits::destroy(_ctx); _ctx = nullptr; }
    }

    const Alive& alive() const { return _alive; }

    // --- Core API ---
    void post(Task task)         { Traits::post(_ctx, std::move(task)); }
    void post(Alive token, Task task) {
        if (!token) { post(std::move(task)); return; }
        Traits::post(_ctx, [token = std::move(token), task = std::move(task)]() {
            if (!token.dead()) task();
        });
    }
    bool start()                { return Traits::start(_ctx); }
    bool stop()                 { return Traits::stop(_ctx); }
    bool isRunning() const      { return Traits::running(_ctx); }

    // --- Backend access (requires complete Context type) ---
    Context*           contextPtr()       { return _ctx; }
    const Context*     contextPtr() const { return _ctx; }
    const std::string& name()       const { return _name; }

    // --- Implicit conversion to type-erased handle ---
    operator LoopRef();

    Loop(const Loop&) = delete;
    Loop& operator=(const Loop&) = delete;
};


namespace detail {

template<typename U, typename = void>
struct has_loop_ref_interface : std::false_type {};

template<typename U>
struct has_loop_ref_interface<U, std::void_t<
    decltype(std::declval<U&>().post(std::declval<Task>())),
    decltype(std::declval<const U&>().alive())
>> : std::true_type {};

template<typename U>
inline constexpr bool has_loop_ref_interface_v = has_loop_ref_interface<std::decay_t<U>>::value;

} // namespace detail


// ============================================================================
// LoopRef — type-erased loop handle
// ============================================================================
//
// Extends std::pair<std::function<void(Task)>, Alive>: first = queue to loop, second = lifetime
// (Alive false means first may be dangling). Prefer explicit two-arg ctor when wiring by hand.
//
// from(T&) binds to an object that exposes post(Task) and alive() (typically Loop<Backend>,
// e.g. Loop<QEventLoop> after LoopTraits<QEventLoop> exists). It does not copy T; the
// closure holds &loop until the LoopRef is destroyed. Only Alive is shared by value.
//
// Primary use cases:
//   Object::connect() / once() with queued dispatch
//   Pipeline Step async completion
//   loop::post(LoopRef, ...)
//
//   obj.connect(SIG, observer, action, LoopRef::from(some_loop));
//   // trigger → loop.post(action) instead of direct call
//

struct LoopRef : std::pair<std::function<void(Task)>, Alive>
{
    VE_INHERIT_CONSTRUCTOR(pair, LoopRef, std::pair<std::function<void(Task)>, Alive>)

    template<typename T, typename = std::enable_if_t<
        detail::has_loop_ref_interface_v<T> && !std::is_same_v<std::decay_t<T>, LoopRef>>>
    static LoopRef from(T& loop) {
        return LoopRef([&loop](Task t) { loop.post(std::move(t)); }, loop.alive());
    }

    void post(Task task) const {
        if (!first || second.dead()) return;
        first(std::move(task));
    }

    void post(Alive token, Task task) const {
        if (!first || second.dead()) return;
        if (!token) { first(std::move(task)); return; }
        first([token = std::move(token), task = std::move(task)]() {
            if (!token.dead()) task();
        });
    }

    explicit operator bool() const { return static_cast<bool>(first); }
};


template<typename T>
inline Loop<T>::operator LoopRef() { return LoopRef::from(*this); }


// ============================================================================
// AsioContext — default backend (asio::io_context)
// ============================================================================

struct AsioContext;   // tag type

template<>
struct LoopTraits<AsioContext>
{
    struct Context;   // opaque — defined in loop.cpp

    static VE_API Context* create(int threads);
    static VE_API void     destroy(Context*);
    static VE_API void     post(Context*, Task);
    static VE_API bool     start(Context*);
    static VE_API bool     stop(Context*);
    static VE_API bool     running(const Context*);
};


// Default alias
using EventLoop = Loop<AsioContext>;


// ============================================================================
// loop:: — global loop accessors
// ============================================================================
//
// loop::main()  — single-threaded, for signal dispatch / thread-affinity
// loop::pool()  — multi-threaded, for compute / IO tasks
// loop::post()  — convenience: post to main loop
//

namespace loop {

VE_API EventLoop& main();
VE_API EventLoop& pool(int threads = 4);

/// Post to main loop
inline void post(Task task) { main().post(std::move(task)); }

/// Post to any loop
template<typename T>
void post(Loop<T>& loop, Task task) { loop.post(std::move(task)); }

/// Guarded post to main loop. Task is discarded if token is false.
VE_API void post(Alive token, Task task);

/// Guarded post with context to main loop.
VE_API void post(Alive token, void* ctx, Task task);

/// Guarded post to a specific loop.
VE_API void post(LoopRef loop, Alive token, Task task);

/// Returns the owner of the currently executing loop task (nullptr if none).
VE_API void* context();

/// Sets loop context, returns previous value. For internal / Loop-backend use.
VE_API void* setContext(void* ctx);

struct ContextGuard {
    void* prev;
    ContextGuard(void* ctx) : prev(setContext(ctx)) {}
    ~ContextGuard() { setContext(prev); }
    ContextGuard(const ContextGuard&) = delete;
    ContextGuard& operator=(const ContextGuard&) = delete;
};

// ---- Main loop runner (used by entry::run) --------------------------------
//
// Default: blocks on condition_variable until quit() is called.
// Modules (e.g. ve.qt) may replace via setMainRunner() to plug in their
// own event loop (QApplication::exec, etc.).

using RunFunc  = std::function<int()>;
using QuitFunc = std::function<void(int)>;

/// Block on the main event loop. Returns exit code.
VE_API int  run();

/// Request the main event loop to stop with the given exit code.
VE_API void quit(int exit_code = 0);

/// Replace the default main-loop implementation.
VE_API void setMainRunner(RunFunc run_fn, QuitFunc quit_fn);

} // namespace loop

} // namespace ve
