// ----------------------------------------------------------------------------
// loop.h - Event loop base class and core loop accessors
// ----------------------------------------------------------------------------
// Copyright (c) 2023-present Thilo and VersatileEngine contributors.
// Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
// See LICENSE file in the project root for full license information.
// ----------------------------------------------------------------------------

#pragma once

#include "base.h"

namespace ve {

// ============================================================================
// Loop - virtual event loop interface
// ============================================================================
//
// Loop is a borrowed runtime object. Optional loop parameters use Loop*:
// nullptr means inline/direct execution. The core keeps only the official
// loop::main() and loop::pool() pointers; user-defined loops are ordinary
// objects managed by their owner.
//
class VE_API Loop : public Entity
{
public:
    explicit Loop(const std::string& name = "");
    virtual ~Loop();

    virtual void   post(Task task);
    virtual bool   start();
    virtual bool   stop();
    virtual bool   isRunning() const;
    virtual size_t processEvents();

    // Block as the process main loop until quit(). Default: poll processEvents()
    // while isRunning(). Framework loops override exec() with the native one
    // (QApplication::exec etc.); quit() must unblock exec() from any thread.
    virtual int    exec();
    virtual void   quit(int exit_code = 0);

    // --- scheduling primitive ---
    //
    // Raw delayed execution. This is the backend behind Object::startTimer();
    // application code should use that instead — a bare handle has no owner and
    // must be removed by hand.
    //
    // `tick` runs on the loop's own thread. Repeating timers are fixed-rate and
    // skip missed ticks rather than bursting to catch up. Timers do not outlive
    // stop(). Returns 0 when the loop has no scheduler (the plain Loop).
    using TimerHandle = uint64_t;
    virtual TimerHandle addTimer(uint64_t ms, bool repeat, Task tick);
    virtual bool        removeTimer(TimerHandle handle);

protected:
    std::atomic<bool> _running{false};
    std::atomic<int>  _exit_code{0};
};

// Standard core implementations. These are concrete runtime loops users may
// construct directly when they need a temporary event loop.
class VE_API AsioLoop : public Loop
{
public:
    explicit AsioLoop(const std::string& name = "");
    ~AsioLoop() override;

    void   post(Task task) override;
    bool   start() override;
    bool   stop() override;
    size_t processEvents() override;
    int    exec() override;
    void   quit(int exit_code = 0) override;

    TimerHandle addTimer(uint64_t ms, bool repeat, Task tick) override;
    bool        removeTimer(TimerHandle handle) override;

private:
    VE_DECLARE_UNIQUE_PRIVATE
};

class VE_API AsioPoolLoop : public Loop
{
public:
    explicit AsioPoolLoop(const std::string& name = "", unsigned threads = 4);
    ~AsioPoolLoop() override;

    void   post(Task task) override;
    bool   start() override;
    bool   stop() override;
    size_t processEvents() override;

    TimerHandle addTimer(uint64_t ms, bool repeat, Task tick) override;
    bool        removeTimer(TimerHandle handle) override;

private:
    VE_DECLARE_UNIQUE_PRIVATE
};

namespace loop {

// Built-in core loops. setMain/setPool borrow the pointer and never delete it.
VE_API Loop* main();
VE_API Loop* pool();
VE_API void  setMain(Loop* loop);
VE_API void  setPool(Loop* loop);

// The loop whose task is currently executing on this thread (thread-local), or
// nullptr when not inside any loop. Core loops set this while running tasks;
// custom loops may call setCurrent() to participate. Used as the default driver
// for Pipeline::sync()/pipeline::async().
VE_API Loop* current();
VE_API void  setCurrent(Loop* loop);

} // namespace loop

} // namespace ve
