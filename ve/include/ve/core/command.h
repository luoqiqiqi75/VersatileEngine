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
// command:: namespace provides factory/reg/build/call/run/parseArgs/args.

#pragma once

#include "loop.h"
#include "node.h"
#include "var.h"
#include "factory.h"

namespace ve {

class Pipeline;
class Command;

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
// Constructed internally by `command::reg(key, builder)`; the builder lambda
// receives Command& and calls addStep / setHelp / declare().

class VE_API Command : public NodeRef
{
public:
    // Bind to an existing factory node directly.
    Command(Node* factory_node) : NodeRef(factory_node) {}

    // Resolve key via the global command factory. If the key is absent, `_n`
    // is bound to a singleton sentinel node carrying an "unknown command"
    // fail-callable — every method stays safe to call; dispatch returns a
    // clean Result::fail instead of crashing on a null `_n`.
    // Use `isValid()` to distinguish from a real bind.
    explicit Command(const std::string& key, char sep = VE_FACTORY_KEY_SEP);

    // True when bound to a real command node; false when bound to the
    // missing-command sentinel (i.e., the key was not found at construction).
    bool isValid() const;

    // --- instance accessors ---
    Node* declare() const { return _n->at("declare"); }

    std::string help() const { return _n->get("help").toString(); }
    void setHelp(const std::string& h) { _n->set("help", h); }

    int stepCount() const { return _n->at("steps")->count(); }

    // --- instance builder API (used inside the reg(...) builder lambda) ---

    void addStep(Step step);

    template<typename F, Step::EnableIfFn<F> = 0>
    void addStep(F&& fn, LoopRef lr = {})
    {
        addStep(Step(Step::BaseT{Var::callable(Step::wrap(std::forward<F>(fn))), std::move(lr)}));
    }


    // Assemble a Pipeline from this command's factory node.
    //   ctx == nullptr → Pipeline allocates its own _ctx with this command's declare
    //                    shadow applied (Pipeline owns the ctx).
    //   ctx != nullptr → caller-owned ctx (Pipeline does not delete it); caller is
    //                    responsible for any shadow/parseArgs setup.
    Pipeline* build(Node* ctx = nullptr) const;

    // --- instance dispatch ---

    // Caller-owned ctx variant: Pipeline does not delete ctx. Caller deletes after.
    Result call(Node* ctx, bool wait = true, Pipeline** detachedOut = nullptr);

    // Pipeline-owned ctx variants: Pipeline (and the detached* returned to caller)
    // owns the auto-allocated ctx — caller has no ctx lifetime to manage.
    Result call(const Var& input = Var{}, bool wait = true);
    Result call(const Var& input, Node* currentNode,
                bool wait = true, Pipeline** detachedOut = nullptr);

    Pipeline* run(Node* ctx);
    Pipeline* run(const Var& input = {});
};

// ============================================================================
// command:: namespace
// ============================================================================

namespace command {

// Global command factory (ve/factory/cmd). The single source of truth for
// registration, lookup, and cleanup; `Command` instances are bound to nodes
// within this factory.
VE_API Factory& factory();

// --- registration ---

// Constraint: F is callable AND not invocable as a builder lambda
// `void(Command&)`. This excludes builder lambdas from the single-step
// overload so they cleanly resolve to the multi-step `reg(..., builder, ...)`.
template<typename F>
using EnableIfStepFn = std::enable_if_t<
    basic::FnTraits<std::decay_t<F>>::IsFunction
    && !std::is_same_v<std::decay_t<F>, Step>
    && !std::is_invocable_v<F&, Command&>, int>;

// Single-step command: the callable runs once per pipeline execution.
// `F` may take `Node*` (raw ctx) / no args / N positional args unpacked from ctx.
template<typename F, EnableIfStepFn<F> = 0>
inline void reg(const std::string& key, F&& fn,
                const std::string& help = "", char sep = VE_FACTORY_KEY_SEP)
{
    factory().reg(key, Var::callable(Step::wrap(std::forward<F>(fn))),
                  help, {}, sep);
}

// Single-step command with explicit LoopRef (callable runs on `loop`).
template<typename F, EnableIfStepFn<F> = 0>
inline void reg(const std::string& key, F&& fn, LoopRef loop,
                const std::string& help = "", char sep = VE_FACTORY_KEY_SEP)
{
    factory().reg(key, Var::callable(Step::wrap(std::forward<F>(fn))),
                  help, std::move(loop), sep);
}

// Multi-step command: builder lambda receives Command& to addStep/setHelp/declare().
VE_API void reg(const std::string& key, std::function<void(Command&)> builder,
                const std::string& help = "", char sep = VE_FACTORY_KEY_SEP);

// Builder-side helper: ensure the key exists and expose its declare/ subtree
// for parameter declarations. Does NOT register a callable; pair with a
// subsequent `command::reg(key, fn, ...)` to make the command runnable.
inline Node* declareNode(const std::string& key, char sep = VE_FACTORY_KEY_SEP)
{
    return factory().node(key, sep)->at("declare");
}

// --- query ---

// Lightweight query (no Command instance). Uses const-qualified factory()
// reference to get find-only Node lookup (vs. ensure-exists on non-const).
inline bool has(const std::string& key, char sep = VE_FACTORY_KEY_SEP)
{
    const Factory& cf = factory();
    return cf.node(key, sep) != nullptr;
}
inline Strings keys() { return factory().keys(); }

// Dispatch — each call constructs a Command and forwards. Callers are responsible
// for ensuring the key exists (use command::has(key) first); calling on an unknown
// key crashes. Performance-critical callers should construct Command(key) once.
inline std::string help(const std::string& key) { return Command(key).help(); }

inline Result call(const std::string& key, Node* ctx,
                   bool wait = true, Pipeline** detachedOut = nullptr)
{
    return Command(key).call(ctx, wait, detachedOut);
}
inline Result call(const std::string& key, const Var& input = Var{}, bool wait = true)
{
    return Command(key).call(input, wait);
}
inline Pipeline* run(const std::string& key, Node* ctx)             { return Command(key).run(ctx); }
inline Pipeline* run(const std::string& key, const Var& input = {}) { return Command(key).run(input); }

// --- ctx helpers (not Command-class methods) ---

inline Node* current(Node* ctx) { return ctx ? static_cast<Node*>(ctx->get().toPointer()) : nullptr; }
inline const Node* current(const Node* ctx) { return ctx ? static_cast<const Node*>(ctx->get().toPointer()) : nullptr; }

VE_API bool parseArgs(Node* ctx, const std::vector<std::string>& args, int startIdx = 0);
VE_API bool parseArgs(Node* ctx, const Var& input);

struct VE_API Args : NodeRef {
    using NodeRef::NodeRef;  // ctors

    std::string string(const std::string& key, const std::string& def = "") const;
    int64_t     integer(const std::string& key, int64_t def = 0) const;
    double      number(const std::string& key, double def = 0.0) const;
    bool        flag(const std::string& key, bool def = false) const;
    Var         var(const std::string& key, const Var& def = {}) const;
    bool        has(const std::string& key) const;
};

VE_API Args args(Node* ctx);

inline Var invoke(const Var& callable, Node* ctx = nullptr)
{
    return callable.invoke(Var(static_cast<void*>(ctx)));
}

} // namespace command

} // namespace ve
