// ----------------------------------------------------------------------------
// pipeline.h — ve::Pipeline: runtime execution engine for Step chains
// ----------------------------------------------------------------------------
// Copyright (c) 2023-present Thilo and VersatileEngine contributors.
// Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
// See LICENSE file in the project root for full license information.
// ----------------------------------------------------------------------------
//
// Pipeline runs a sequence of Steps with a state machine:
//   IDLE -> start -> RUNNING -> (all done) -> DONE
//                             -> (error)    -> ERRORED
//                  -> pause  -> PAUSED -> resume -> RUNNING
//                  -> stop   -> IDLE
//
// Signals: CMD_DONE, CMD_ERROR (emitted on completion / failure)
//
// Context ownership:
//   Pipeline holds the per-call context Node — the shared state every Step reads
//   from and writes to. The constructor decides ownership: pass an existing
//   Node* to share it with the caller (Pipeline does not delete it), or pass
//   nullptr to let Pipeline allocate its own "_ctx" Node (deleted by the dtor).
//
// Each start() copies the Step list into a runtime queue for safe re-execution.
// Steps returning ACCEPT pause the pipeline until finish() is called.

#pragma once

#include "command.h"

namespace ve {

class VE_API Pipeline : public Object
{
public:
    enum State { IDLE, RUNNING, PAUSED, DONE, ERRORED };

    enum Signal : SignalT {
        CMD_DONE  = 0xFFFF'0030,
        CMD_ERROR = 0xFFFF'0031,
    };

    // ctx == nullptr → Pipeline allocates an internal "_ctx" Node and owns it.
    // ctx != nullptr → Pipeline uses the caller's ctx and does NOT delete it.
    explicit Pipeline(const std::string& name = "", Node* ctx = nullptr);
    ~Pipeline();

    // --- build ---
    Pipeline& add(const Step& step);

    int stepCount() const;

    // --- execution state machine ---
    Result start();
    void   pause();
    void   resume();
    void   stop();
    void   finish(const Result& result);
    State  state() const;

    // --- progress ---
    int currentStep() const;
    Node* context() const;
    const Result& lastResult() const;

    // --- result callback (convenience alongside signals) ---
    using ResultHandler = std::function<void(const Result&)>;
    void setResultHandler(const ResultHandler& handler);

    // --- clone (deep copy for thread-safe parallel execution) ---
    Pipeline* clone() const;

private:
    void runNext();
    void handleResult(const Result& result);
    void complete(State finalState, SignalT signal, const Result& result);

    VE_DECLARE_POOL_PRIVATE
};

} // namespace ve
