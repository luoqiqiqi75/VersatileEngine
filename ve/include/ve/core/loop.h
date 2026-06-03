// ----------------------------------------------------------------------------
// loop.h — Event loop framework (Loop handle + LoopContext<T> backend)
// ----------------------------------------------------------------------------
// Copyright (c) 2023-present Thilo and VersatileEngine contributors.
// Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
// See LICENSE file in the project root for full license information.
// ----------------------------------------------------------------------------
//
// Two types, one role each:
//
//   Loop            — non-owning, copyable handle (a bare pointer into the global
//                     loop store). Copying copies the pointer; it never owns or
//                     tears down a backend. Empty Loop (default-constructed) ⇒
//                     inline / synchronous dispatch.
//   LoopContext<T>  — per-backend implementation. Owned permanently by the loop
//                     store (one cpp-global, intentional-leak — like Pool/factory).
//
// A Loop owns a thread (or several): the store is the single place that accounts
// for how many exist. Create via the factory:
//
//   loop::reg(name, factory)     record name → how to build its backend
//   loop::instance(name)         get-or-create the loop for a name
//   loop::main() / loop::pool()  the two official loops
//   Loop::from<T>()              build backend T directly (needs T's definition)
//
// Backend factories keep the backend type out of headers:
//   loop::asio()        → single-thread io_context (affinity / signal dispatch)
//   loop::asioPool(n)   → N-thread io_context (compute / IO offload)
//
// Usage:
//   loop::reg("sensor-io", loop::asio());
//   Loop l = loop::instance("sensor-io");
//   l.start();
//   l.post([]{ doWork(); });
//   l.stop();
//
// ----------------------------------------------------------------------------

#pragma once

#include <memory>
#include <utility>

#include "base.h"

namespace ve {

// LoopContext<T> — per-backend implementation, owned permanently by the loop
// store. Specialize per backend with member functions:
//
//   void   post(Task);              // queue a task (thread-safe)
//   bool   start();                 // begin processing (spawn worker[s])
//   bool   stop();                  // stop & join worker[s]
//   bool   running() const;         // is active?
//   bool   isCurrentThread() const; // is caller on a worker thread of this loop?
//   size_t processEvents();         // run all pending tasks (non-blocking); count run
//
// isCurrentThread()/processEvents() enable re-entrant sync dispatch: a nested
// command::callSync on its target loop's own thread drains the loop instead of
// CV-waiting (which would self-starve). Meaningful only for single-thread
// backends; a multi-thread pool has no affinity, so don't rely on it there.
template<typename T>
struct LoopContext;   // primary: intentionally undefined — specialize per backend


// ============================================================================
// Loop — non-owning, copyable loop handle
// ============================================================================

class Loop
{
public:
    // Type-erased backend operations. One Ops lives permanently in the store
    // alongside the backend it captures; a Loop just points at it.
    struct Ops {
        std::function<void(Task)> post;
        std::function<bool()>     start;
        std::function<bool()>     stop;
        std::function<bool()>     running;
        std::function<bool()>     isCurrentThread;
        std::function<size_t()>   processEvents;   // run pending tasks; returns # run
    };

    Loop() = default;

    // Internal: wrap an Ops owned by the store. Prefer the factory entry points
    // (Loop::from / loop::reg / loop::instance) over calling this directly.
    explicit Loop(const Ops* ops) : _ops(ops) {}

    // Create a fresh, anonymous LoopContext<T> in the store and return a handle.
    // The backend is permanent (the store never erases) — the handle is just a
    // view, so copies and destruction of handles never tear it down.
    template<typename T, typename... Args>
    static Loop from(Args&&... args);

    // --- core API (defined in loop.cpp: post() applies the task's guards and
    //     sets the running task's token, so it needs the thread_local token
    //     machinery) ---
    VE_API void   post(Task task) const;
    VE_API bool   start() const;
    VE_API bool   stop() const;
    VE_API bool   isRunning() const;
    VE_API bool   isCurrentThread() const;
    VE_API size_t processEvents() const;

    // True iff bound to a backend.
    explicit operator bool() const { return _ops != nullptr; }

    // Internal: the store-owned Ops this handle points at (null if empty). Used
    // by the loop store to cache a materialized handle; not for general use.
    const Ops* _opsPtr() const { return _ops; }

private:
    const Ops* _ops = nullptr;   // non-owning: points into the global loop store
};

// ============================================================================
// Core backends — declared here as the official examples; State + member bodies
// live in loop.cpp, so this header pulls in NO asio headers. Callers normally
// reach them through the opaque factories loop::asio() / loop::asioPool(); the
// tags are public mainly so Loop::from<AsioContext>() works and they document
// the LoopContext specialization shape for custom backends.
// ============================================================================

struct AsioContext;   // single-thread io_context backend (affinity / signal dispatch)
struct AsioPool;      // N-thread io_context backend (compute / IO offload; no affinity)

template<>
struct LoopContext<AsioContext>
{
    VE_API LoopContext();
    VE_API ~LoopContext();

    VE_API void   post(Task task);
    VE_API bool   start();
    VE_API bool   stop();
    VE_API bool   running() const;

    // Re-entrant sync support for callSync.
    VE_API bool   isCurrentThread() const;
    VE_API size_t processEvents();   // run all pending tasks (non-blocking)

    LoopContext(const LoopContext&) = delete;
    LoopContext& operator=(const LoopContext&) = delete;

private:
    struct State;            // opaque — defined in loop.cpp (holds asio io_context)
    State* _s = nullptr;
};

template<>
struct LoopContext<AsioPool>
{
    VE_API explicit LoopContext(unsigned threads = 4);
    VE_API ~LoopContext();

    VE_API void   post(Task task);
    VE_API bool   start();
    VE_API bool   stop();
    VE_API bool   running() const;

    VE_API bool   isCurrentThread() const;   // true iff caller is one of the workers
    VE_API size_t processEvents();

    LoopContext(const LoopContext&) = delete;
    LoopContext& operator=(const LoopContext&) = delete;

private:
    struct State;
    State* _s = nullptr;
};

// To add your own backend (Qt, RTT, a test double): specialize LoopContext<Tag>
// in your own TU with the same member shape, then hand Loop::from<Tag>() to a
// loop::reg(name, ...) factory — only that TU needs the backend's definition.

// Store adoption: build a permanent Ops wrapping `impl` (ownership transferred
// to the global store — intentional leak, never torn down) and return a
// non-owning Loop pointing at it. Defined in loop.cpp.
namespace loop {
VE_API const Loop::Ops* storeAdopt(void* impl, Loop::Ops ops, void (*deleter)(void*));
}

// Loop::from<T> — instantiate backend T, adopt it into the store, return a
// (non-owning) handle. The only place LoopContext<T> must be a complete type,
// so it is instantiated in the caller's TU where the specialization is visible.
template<typename T, typename... Args>
Loop Loop::from(Args&&... args)
{
    auto* p = new LoopContext<T>(std::forward<Args>(args)...);
    const Ops* ops = loop::storeAdopt(p, Ops{
        [p](Task t) { p->post(std::move(t)); },
        [p]         { return p->start(); },
        [p]         { return p->stop(); },
        [p]         { return p->running(); },
        [p]         { return p->isCurrentThread(); },
        [p]         { return p->processEvents(); },
    }, [](void* q) { delete static_cast<LoopContext<T>*>(q); });
    return Loop(ops);
}


// ============================================================================
// loop:: — named-loop factory + the two official loops
// ============================================================================
//
// A loop is registered under a NAME with a lazy FACTORY (how to build its
// backend). The backend is created on first instance(name); the handle is a
// non-owning view into the permanent store. This mirrors ve::factory: reg only
// records, instance() materializes, the store owns forever (no dangling).
//
//   loop::main()  — single-thread loop for signal dispatch / thread-affinity
//   loop::pool()  — N-thread pool for offloading compute / IO
//   loop::reg     — record name → factory (first registration wins)
//   loop::instance— get-or-create the loop for a name
//
// Backend independence: loop::asio()/asioPool() return factories whose backend
// type lives entirely in loop.cpp, so callers register/use loops without
// including any backend header. A host (e.g. veQt) overrides a name by reg-ing
// it BEFORE first use — then core's loop::main().post(...) lands on that
// backend (e.g. the Qt main thread) with zero backend-specific code in core.

namespace loop {

// How to build a loop's backend. Returns a non-owning handle (typically via
// Loop::from<T>()); invoked at most once per name, on first instance().
using Factory = std::function<Loop()>;

// Record name → factory. First registration wins (later reg for the same name
// is ignored), so a host can pre-empt a default by reg-ing earlier. Returns
// true if this call installed the factory.
VE_API bool reg(const std::string& name, Factory make);

// Get-or-create the loop for a name. Runs the factory once on first call; later
// calls return the same handle. Empty Loop if the name was never registered.
VE_API Loop instance(const std::string& name);

// Built-in backend factories — backend type stays in loop.cpp.
VE_API Factory asio();                       // single-thread io_context
VE_API Factory asioPool(unsigned threads = 4); // N-thread io_context

VE_API const Loop& main();
VE_API const Loop& pool();

// All posting funnels through Loop::post (the only place that applies a task's
// guards and sets the running task's context). These are thin conveniences.
// To post a guarded task, build a Task with its guard tokens: Task{fn, {owner, …}}.

/// Post to the main loop.
inline void post(Task task) { main().post(std::move(task)); }

/// The token of the task currently running on this thread's loop. Empty when no
/// loop task is running (e.g. test main). Set only by Loop::post — there is no
/// external setter. Recover the scheduler's identity with loop::token().as<T>().
VE_API Token token();

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
