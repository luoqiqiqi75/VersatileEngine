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
// ve::registerStep / ve::registerCommand — standard registration into GlobalCommandFactory.
//
// command:: — short aliases: reg -> registerStep, build -> registerCommand, plus call/run/context/...

#pragma once

#include "loop.h"
#include "node.h"
#include "var.h"
#include "factory.h"

namespace ve {

class Pipeline;

namespace convert {
template<> inline bool parse(const Result& r, std::string& s)
{
    if (r.isSuccess()) {
        s = "success";
        return true;
    }
    if (r.isAccepted()) {
        s = "accepted";
        return true;
    }
    s = "error(" + std::to_string(r.code()) + ")";
    if (!r.content().isNull()) {
        s += ": " + r.content().toString();
    }
    return true;
}
}

// ============================================================================
// Step - Var(CALLABLE) + optional LoopRef (std::pair: first = fn, second = loop)
// ============================================================================

struct VE_API Step : std::pair<Var, LoopRef>
{
    VE_INHERIT_CONSTRUCTOR(pair, Step, std::pair<Var, LoopRef>)

    /// SFINAE for templates that take a callable and must reject raw Step.
    template<typename F>
    using EnableIfFn = std::enable_if_t<
        basic::FnTraits<std::decay_t<F>>::IsFunction
        && !std::is_same_v<std::decay_t<F>, Step>, int>;

    template<typename F, EnableIfFn<F> = 0>
    Step(F&& f, LoopRef lr = {})
        : BaseT(Var::callable(std::forward<F>(f)), std::move(lr)) {}

    Var exec(const Var& input = {}) const
    {
        if (!first.isCallable()) {
            return Var::custom(Result::fail(Var()));
        }
        return first.invoke(input);
    }

    explicit operator bool() const { return first.isCallable(); }
};

VE_API Result resultFromStepReturn(const Var& ret);

// ============================================================================
// Command - step definition template (blueprint)
// ============================================================================

class VE_API Command
{
public:
    explicit Command(const std::string& name = "");
    ~Command();

    void addStep(Step step);

    template<typename F, Step::EnableIfFn<F> = 0>
    void addStep(F&& fn, LoopRef lr = {})
    {
        _steps.push_back(Step(Var::callable(std::forward<F>(fn)), std::move(lr)));
    }

    const std::string& name() const;
    int stepCount() const;
    const Vector<Step>& steps() const;

    const std::string& help() const;
    void setHelp(const std::string& h);

    Pipeline* pipeline() const;

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
// Standard registration (explicit Step / builder; full control)
// ============================================================================
//
// registerStep(key, fn, help)              — Step with empty LoopRef
// registerStep(key, fn, LoopRef, help)     — Step bound to loop (e.g. LoopRef::from(myLoop))
// registerStep(key, Step(...), help)       — full Step including loop
//

VE_API void registerStep(const std::string& key, Step step, const std::string& help = "");

template<typename F, Step::EnableIfFn<F> = 0>
void registerStep(const std::string& key, F&& fn, const std::string& help = "")
{
    registerStep(key, Step(std::forward<F>(fn)), help);
}

template<typename F, Step::EnableIfFn<F> = 0>
void registerStep(const std::string& key, F&& fn, LoopRef loop, const std::string& help = "")
{
    registerStep(key, Step(std::forward<F>(fn), std::move(loop)), help);
}

VE_API void registerCommand(const std::string& key,
                            std::function<void(Command&)> builder,
                            const std::string& help = "");

// ============================================================================
// command:: - shortcuts (thin wrappers + execution helpers)
// ============================================================================

namespace command {

// reg(key, fn, help) / reg(key, fn, LoopRef, help) — same split as registerStep

template<typename F, Step::EnableIfFn<F> = 0>
void reg(const std::string& key, F&& fn, const std::string& help = "")
{
    registerStep(key, std::forward<F>(fn), help);
}

template<typename F, Step::EnableIfFn<F> = 0>
void reg(const std::string& key, F&& fn, LoopRef loop, const std::string& help = "")
{
    registerStep(key, std::forward<F>(fn), std::move(loop), help);
}

VE_API void build(const std::string& key,
                  std::function<void(Command&)> builder,
                  const std::string& help = "");

/// ctx null: builds context via context(key); deleted inside call when ctx was allocated by call.
/// wait=false and asynchronous execution: pass non-null detachedOut; *detachedOut receives the Pipeline* (caller deletes pipeline and ctx after completion).
/// The Var overload does not support detachedOut; use the Node* overload for fire-and-forget commands.
VE_API Result call(const std::string& key, Node* ctx, bool wait = true, Pipeline** detachedOut = nullptr);
VE_API Result call(const std::string& key, const Var& input = Var {}, bool wait = true);

/// ctx null: Pipeline allocates context internally. Non-null: caller keeps ctx alive for the pipeline lifetime.
VE_API Pipeline* run(const std::string& key, Node* ctx);
VE_API Pipeline* run(const std::string& key, const Var& input = {});

VE_API Node* context(const std::string& key, Node* currentNode = nullptr);

VE_API Node* declareNode(const std::string& key);

VE_API bool parseArgs(Node* ctx, const std::vector<std::string>& args, int startIdx = 0);

VE_API bool parseArgs(Node* ctx, const Var& input);

// ============================================================================
// Args — lightweight accessor for parsed command arguments
// ============================================================================
//
// Reads from ctx child nodes (populated by parseArgs).
// Key lookup with positional fallback for commands without declare metadata.

struct VE_API Args {
    Node* ctx;
    explicit Args(Node* c) : ctx(c) {}

    std::string string(const std::string& key, const std::string& def = "") const;
    int64_t     integer(const std::string& key, int64_t def = 0) const;
    double      number(const std::string& key, double def = 0.0) const;
    bool        flag(const std::string& key, bool def = false) const;
    Var         var(const std::string& key, const Var& def = {}) const;
    bool        has(const std::string& key) const;
};

VE_API Args args(Node* ctx);

VE_API bool        has(const std::string& key);
VE_API Strings     keys();
VE_API std::string help(const std::string& key);

inline Var invoke(const Var& callable, Node* ctx = nullptr)
{
    return callable.invoke(Var(static_cast<void*>(ctx)));
}

} // namespace command

} // namespace ve
