// ----------------------------------------------------------------------------
// command.h - ve::Command + command:: namespace
// ----------------------------------------------------------------------------
// Copyright (c) 2023-present Thilo and VersatileEngine contributors.
// Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
// See LICENSE file in the project root for full license information.
// ----------------------------------------------------------------------------
//
// Command  = named collection of Steps with optional declare node.
//            Does not execute itself; call pipeline() to create a Pipeline.
//
// Declare  = Node at /ve/command/declare/{key}, defines parameter metadata.
//            Context nodes shadow to declare for defaults + help.
//
// command:: = convenience namespace for end users:
//             reg()      register a command (single-step or multi-step)
//             call()     sync execute
//             run()      async execute -> Pipeline*
//             context()  create context node (shadowed to declare)
//             parseArgs  parse POSIX args into context node

#pragma once

#include "pipeline.h"
#include "factory.h"

namespace ve {

class Node;

// ============================================================================
// Command - step definition template (blueprint)
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
// Global factory
// ============================================================================

using CommandFactory = Factory<Command*()>;

VE_API CommandFactory& GlobalCommandFactory();

// ============================================================================
// command:: - user convenience API
// ============================================================================

namespace command {

// --- registration ---
// Register a single-step command.
// fn: Result(Node*) — new primary signature.
// If a declare node exists at /ve/command/declare/{key}, it is auto-linked.
VE_API void reg(const std::string& key, Step::StepFn fn,
                const std::string& help = "");

// Register via Step object (supports legacy signatures via Step's wrapFn).
// Usage: command::reg("save", Step("save", [](const Var& v) -> Result { ... }));
VE_API void reg(const std::string& key, const Step& step,
                const std::string& help = "");

// Register a multi-step command via builder callback.
VE_API void build(const std::string& key,
                  std::function<void(Command&)> builder,
                  const std::string& help = "");

// --- execution ---
// Sync call: create context, execute, return Result.
VE_API Result call(const std::string& key, Node* ctx);
VE_API Result call(const std::string& key, const Var& input = {});  // backward compat

// Async: create Pipeline, start, return Pipeline* (caller owns).
VE_API Pipeline* run(const std::string& key, Node* ctx);
VE_API Pipeline* run(const std::string& key, const Var& input = {});

// --- context ---
// Create a context node shadowed to /ve/command/declare/{key}.
// currentNode is stored as _current (the caller's working node).
// Caller owns the returned Node and must delete it.
VE_API Node* context(const std::string& key, Node* currentNode = nullptr);

// Get declare node (may be nullptr if no declare registered).
VE_API Node* declareNode(const std::string& key);

// --- argument parsing ---
// Parse POSIX-style args into context node using declare metadata.
// Reads _pos, _short from declare (via shadow) to map args to named children.
// Returns false on parse error.
VE_API bool parseArgs(Node* ctx, const std::vector<std::string>& args, int startIdx = 0);

// --- query ---
VE_API bool        has(const std::string& key);
VE_API Strings     keys();
VE_API std::string help(const std::string& key);

} // namespace command

} // namespace ve
