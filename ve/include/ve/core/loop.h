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
// nullptr means inline/direct execution. The core owns only loop::main() and
// loop::pool(); user-defined loops are ordinary objects managed by their owner.
//
class VE_API Loop
{
public:
    explicit Loop(const std::string& name = "");
    virtual ~Loop();

    const std::string& name() const;

    virtual void   post(Task task);
    virtual bool   start();
    virtual bool   stop();
    virtual bool   isRunning() const;
    virtual bool   isCurrentThread() const;
    virtual size_t processEvents();

private:
    std::string _name;
};

namespace loop {

// Built-in core loops. Implementations live in loop.cpp and do not expose asio.
VE_API Loop* main();
VE_API Loop* pool();

// ---- Main loop runner (used by entry::run) --------------------------------

using RunFunc  = std::function<int()>;
using QuitFunc = std::function<void(int)>;

VE_API int  run();
VE_API void quit(int exit_code = 0);
VE_API void setMainRunner(RunFunc run_fn, QuitFunc quit_fn);

} // namespace loop

} // namespace ve
