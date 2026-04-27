// ----------------------------------------------------------------------------
// command.h - ve::Step, ve::Command, command:: namespace
// ----------------------------------------------------------------------------
// Copyright (c) 2023-present Thilo and VersatileEngine contributors.
// Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
// See LICENSE file in the project root for full license information.
// ----------------------------------------------------------------------------
//
// Step    = single callable + LoopRef.
//           User-facing: wraps any callable, writes to / reads from factory node.
//
// Command = multi-step builder backed by a factory node.
//           User-facing: addStep() writes to _steps/#N children,
//           build() assembles a Pipeline from those children.
//
// Factory node layout for a registered command:
//
//   ve/factory/cmd/{key}/          <- single-step: node value = CALLABLE
//     help                         <- help string
//     loop                         <- LoopRef (single-step only)
//     declare/                     <- parameter declarations
//       param_name
//       ...
//     steps/                       <- multi-step only
//       #0  (value=CALLABLE, loop child)
//       #1  (value=CALLABLE, loop child)
//       ...
//
// command:: namespace provides reg/build/call/run/context/parseArgs/args.

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
    if (r.isSuccess()) { s = "success"; return true; }
    if (r.isAccepted()) { s = "accepted"; return true; }
    s = "error(" + std::to_string(r.code()) + ")";
    if (!r.content().isNull()) s += ": " + r.content().toString();
    return true;
}
}

// ============================================================================
// Step - callable + LoopRef, with factory node I/O
// ============================================================================

struct VE_API Step : std::pair<Var, LoopRef>
{
    VE_INHERIT_CONSTRUCTOR(pair, Step, std::pair<Var, LoopRef>)

    template<typename F>
    using EnableIfFn = std::enable_if_t<
        basic::FnTraits<std::decay_t<F>>::IsFunction
        && !std::is_same_v<std::decay_t<F>, Step>, int>;

    template<typename F, EnableIfFn<F> = 0>
    Step(F&& f, LoopRef lr = {})
        : BaseT(Var::callable(std::forward<F>(f)), std::move(lr)) {}

    Var exec(const Var& input = {}) const
    {
        if (!first.isCallable()) return Var::custom(Result::fail(Var()));
        return first.invoke(input);
    }

    explicit operator bool() const { return first.isCallable(); }

    // Write this step to a factory node:
    //   node value <- callable,  _loop child <- LoopRef (if set)
    void writeTo(Node* nd) const;

    // Read callable + LoopRef from a factory node and append a Step to pipe.
    static void addToPipeline(Node* nd, Pipeline& pipe);

    // --- callable wrapping ---

    template<typename F, size_t... I>
    static auto wrapMultiArg(F&& fn, std::index_sequence<I...>)
    {
        auto callable = Var::callable(std::forward<F>(fn));
        return [callable = std::move(callable)](Node* ctx) -> Var {
            Var::ListV args;
            args.reserve(sizeof...(I));
            if (ctx) {
                ((args.push_back(ctx->at(static_cast<int>(I), true)
                    ? ctx->at(static_cast<int>(I), true)->get() : Var())), ...);
            } else {
                ((args.push_back(Var()), (void)I), ...);
            }
            return callable.invoke(Var(std::move(args)));
        };
    }

    template<typename F>
    static auto wrap(F&& fn)
    {
        using Traits = basic::FnTraits<std::decay_t<F>>;
        if constexpr (Traits::ArgCnt == 0) {
            return std::decay_t<F>(std::forward<F>(fn));
        } else if constexpr (Traits::ArgCnt == 1) {
            using Arg0 = std::decay_t<typename Traits::template ArgAt<0>>;
            if constexpr (std::is_same_v<Arg0, Node*> || std::is_same_v<Arg0, const Node*>)
                return std::decay_t<F>(std::forward<F>(fn));
            else
                return wrapMultiArg(std::forward<F>(fn), std::index_sequence<0>{});
        } else {
            return wrapMultiArg(std::forward<F>(fn), std::make_index_sequence<Traits::ArgCnt>{});
        }
    }
};

VE_API Result resultFromStepReturn(const Var& ret);

// ============================================================================
// Command - multi-step builder backed by a factory node
// ============================================================================
//
// Constructed internally by registerCommand / command::build.
// The builder lambda receives Command& and calls addStep / setHelp / declare().

class VE_API Command
{
    Node* _nd;  // factory node (ve/factory/cmd/{key})

public:
    explicit Command(Node* factory_node);

    const std::string& name() const;

    // Append a step to _steps/#N child of the factory node.
    void addStep(Step step);

    template<typename F, Step::EnableIfFn<F> = 0>
    void addStep(F&& fn, LoopRef lr = {})
    {
        addStep(Step(Step::BaseT{Var::callable(Step::wrap(std::forward<F>(fn))), std::move(lr)}));
    }

    void setHelp(const std::string& h);
    std::string help() const;

    // Number of steps: 1 if node value is callable, else count of steps/ children.
    int stepCount() const;

    // Parameter declaration subtree (_declare/ under factory node).
    Node* declare();

    // Assemble a Pipeline from this command's factory node.
    // Single-step: reads node value + _loop.
    // Multi-step:  iterates _steps/#N children.
    Pipeline* build() const;

    Node* node() const { return _nd; }
};

// ============================================================================
// Registration
// ============================================================================

// Global command factory (ve/factory/cmd). Exposed for test access and cleanup.
VE_API Factory& GlobalCommandFactory();

VE_API void registerStep(const std::string& key, Step step, const std::string& help = "");

template<typename F, Step::EnableIfFn<F> = 0>
void registerStep(const std::string& key, F&& fn, const std::string& help = "")
{
    registerStep(key, Step(Step::BaseT{Var::callable(Step::wrap(std::forward<F>(fn))), LoopRef{}}), help);
}

template<typename F, Step::EnableIfFn<F> = 0>
void registerStep(const std::string& key, F&& fn, LoopRef loop, const std::string& help = "")
{
    registerStep(key, Step(Step::BaseT{Var::callable(Step::wrap(std::forward<F>(fn))), std::move(loop)}), help);
}

VE_API void registerCommand(const std::string& key,
                            std::function<void(Command&)> builder,
                            const std::string& help = "");

// ============================================================================
// command:: namespace
// ============================================================================

namespace command {

template<typename F, Step::EnableIfFn<F> = 0>
void reg(const std::string& key, F&& fn, const std::string& help = "")
{
    registerStep(key, Step(Step::BaseT{Var::callable(Step::wrap(std::forward<F>(fn))), LoopRef{}}), help);
}

template<typename F, Step::EnableIfFn<F> = 0>
void reg(const std::string& key, F&& fn, LoopRef loop, const std::string& help = "")
{
    registerStep(key, Step(Step::BaseT{Var::callable(Step::wrap(std::forward<F>(fn))), std::move(loop)}), help);
}

VE_API void build(const std::string& key,
                  std::function<void(Command&)> builder,
                  const std::string& help = "");

VE_API Result call(const std::string& key, Node* ctx, bool wait = true, Pipeline** detachedOut = nullptr);
VE_API Result call(const std::string& key, const Var& input = Var{}, bool wait = true);

VE_API Pipeline* run(const std::string& key, Node* ctx);
VE_API Pipeline* run(const std::string& key, const Var& input = {});

VE_API Node* context(const std::string& key, Node* currentNode = nullptr);
inline Node* current(Node* ctx) { return ctx ? static_cast<Node*>(ctx->get().toPointer()) : nullptr; }
inline const Node* current(const Node* ctx) { return ctx ? static_cast<const Node*>(ctx->get().toPointer()) : nullptr; }

VE_API Node* declareNode(const std::string& key);

VE_API bool parseArgs(Node* ctx, const std::vector<std::string>& args, int startIdx = 0);
VE_API bool parseArgs(Node* ctx, const Var& input);

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
