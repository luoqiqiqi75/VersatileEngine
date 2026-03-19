// ----------------------------------------------------------------------------
// pipeline.h — ve::Pipeline: runtime execution engine for Step chains
// ----------------------------------------------------------------------------
// Copyright (c) 2023-present Thilo and VersatileEngine contributors.
// Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
// See LICENSE file in the project root for full license information.
// ----------------------------------------------------------------------------
//
// Pipeline runs a sequence of Steps with a state machine:
//   IDLE → start → RUNNING → (all done) → DONE
//                           → (error)    → ERRORED
//                  → pause  → PAUSED → resume → RUNNING
//                  → stop   → IDLE
//
// Signals: CMD_DONE, CMD_ERROR (emitted on completion / failure)
//
// Each start() deep-copies the Step list for safe re-execution.
// Steps returning ACCEPT pause the pipeline until finish() is called.

#pragma once

#include "step.h"
#include "factory.h"

namespace ve {

class VE_API Pipeline : public Object
{
public:
    enum State { IDLE, RUNNING, PAUSED, DONE, ERRORED };

    enum Signal : SignalT {
        CMD_DONE  = 0xFFFF'0030,
        CMD_ERROR = 0xFFFF'0031,
    };

    explicit Pipeline(const std::string& name = "");
    ~Pipeline();

    // --- build ---
    Pipeline& add(const Step& step);
    Pipeline& add(const std::string& name, Step::StepFn fn);
    Pipeline& add(const std::string& name, Step::StepFn fn, LoopRef loop);
    int stepCount() const;

    // --- execution state machine ---
    Result start(const Var& input = {});
    void   pause();
    void   resume();
    void   stop();
    void   finish(const Result& result);
    State  state() const;

    // --- progress ---
    int currentStep() const;
    const Var& input() const;

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
