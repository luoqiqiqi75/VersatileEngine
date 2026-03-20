// ----------------------------------------------------------------------------
// command.h — ve::Command + GlobalStepFactory / GlobalCommandFactory + command::
// ----------------------------------------------------------------------------
// Copyright (c) 2023-present Thilo and VersatileEngine contributors.
// Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
// See LICENSE file in the project root for full license information.
// ----------------------------------------------------------------------------
//
// Command  = named collection of Steps (blueprint/recipe).
//            Does not execute itself; call pipeline() to create a Pipeline.
//
// Factory  = GlobalStepFactory()   — stores Step functions by key
//            GlobalCommandFactory() — stores Command creators by key
//
// command:: = convenience namespace for end users:
//             run()  → Pipeline*  (async)
//             step() → Pipeline*  (single-step)
//             call() → Result     (sync)

#pragma once

#include "pipeline.h"
#include "factory.h"

namespace ve {

class Node;

// ============================================================================
// Command — step definition template (blueprint)
// ============================================================================

class VE_API Command
{
public:
    explicit Command(const std::string& name = "");
    ~Command();

    // --- build ---
    void addStep(const Step& step);
    void addStep(const std::string& name, Step::StepFn fn);
    void addStep(const std::string& name, Step::StepFn fn, LoopRef loop);

    template<typename F, std::enable_if_t<
        basic::FnTraits<std::decay_t<F>>::IsFunction
        && !std::is_convertible_v<F, Step::StepFn>, int> = 0>
    void addStep(const std::string& name, F fn, LoopRef loop = {})
    {
        addStep(Step(name, std::move(fn), std::move(loop)));
    }

    const std::string& name() const;
    int stepCount() const;
    const Vector<Step>& steps() const;

    // --- metadata ---
    const std::string& help() const;
    void setHelp(const std::string& h);

    // --- create execution instance (deep-copies Steps) ---
    Pipeline* pipeline() const;

    // --- optional Node for public data (lazy-created) ---
    Node* node() const;

private:
    std::string   _name;
    std::string   _help;
    Vector<Step>  _steps;
    mutable Node* _node = nullptr;
};

// ============================================================================
// Global factories
// ============================================================================

using StepFactory    = Factory<Result(const Var&)>;
using CommandFactory = Factory<Command*()>;

VE_API StepFactory&    GlobalStepFactory();
VE_API CommandFactory& GlobalCommandFactory();

// ============================================================================
// command:: — user convenience API
// ============================================================================

namespace command {

// Execute a registered Command → create Pipeline, start, return Pipeline*.
// Caller owns the returned Pipeline.
VE_API Pipeline* run(const std::string& key, const Var& input = {});
VE_API Pipeline* run(const std::string& key, Node* input);

// Execute a single registered Step → wrap in a Pipeline, start.
VE_API Pipeline* step(const std::string& key, const Var& input = {});

// Synchronous call: run to completion, return final Result.
VE_API Result call(const std::string& key, const Var& input = {});

// Query
VE_API bool        has(const std::string& key);
VE_API Strings     keys();
VE_API std::string help(const std::string& key);

// Convenience registration: wraps GlobalStepFactory + stores help text.
// Designed for terminal builtin commands.
VE_API void reg(const std::string& key, Step::StepFn fn,
                const std::string& help = "");

// Built-in terminal commands
VE_API void initBuiltins();

} // namespace command

} // namespace ve
