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

    // --- registration ---

    VE_API static void reg(const std::string& key, Step step,
                           const std::string& help = "", char sep = VE_FACTORY_KEY_SEP);

    template<typename F, EnableIfFn<F> = 0>
    static void reg(const std::string& key, F&& fn, const std::string& help = "",
                    char sep = VE_FACTORY_KEY_SEP)
    {
        reg(key, Step(BaseT{Var::callable(wrap(std::forward<F>(fn))), LoopRef{}}), help, sep);
    }

    template<typename F, EnableIfFn<F> = 0>
    static void reg(const std::string& key, F&& fn, LoopRef loop,
                    const std::string& help = "", char sep = VE_FACTORY_KEY_SEP)
    {
        reg(key, Step(BaseT{Var::callable(wrap(std::forward<F>(fn))), std::move(loop)}), help, sep);
    }
};

VE_API Result resultFromStepReturn(const Var& ret);

// ============================================================================
// Command - multi-step builder backed by a factory node
// ============================================================================
//
// Constructed internally by Command::reg / command::build.
// The builder lambda receives Command& and calls addStep / setHelp / declare().

class VE_API Command : public NodeRef
{
public:
    // Bind to an existing factory node directly.
    Command(Node* factory_node) : NodeRef(factory_node) {}

    // Resolve key via the global command factory (find-only — invalid Command if missing).
    explicit Command(const std::string& key, char sep = VE_FACTORY_KEY_SEP);

    // --- instance accessors ---
    int stepCount() const;
    Node* declare();

    std::string help() const
    {
        // use_shadow=true: fall back to prototype's help if this command has none
        if (auto* h = _n->find("help")) return h->getString();
        return {};
    }

    // --- instance builder API (used inside the reg(...) builder lambda) ---

    void addStep(Step step);

    template<typename F, Step::EnableIfFn<F> = 0>
    void addStep(F&& fn, LoopRef lr = {})
    {
        addStep(Step(Step::BaseT{Var::callable(Step::wrap(std::forward<F>(fn))), std::move(lr)}));
    }

    void setHelp(const std::string& h)
    {
        if (!h.empty()) _n->at("help")->set(Var(h));
    }

    // Assemble a Pipeline from this command's factory node.
    Pipeline* build() const;

    // --- instance dispatch ---

    Node* context(Node* currentNode = nullptr);

    Result call(Node* ctx, bool wait = true, Pipeline** detachedOut = nullptr);
    Result call(const Var& input = Var{}, bool wait = true);

    Pipeline* run(Node* ctx);
    Pipeline* run(const Var& input = {});

    // --- static factory & registration ---

    // Global command factory (ve/factory/cmd). Exposed for test access and cleanup.
    static Factory& factory();

    // Register a multi-step command; builder lambda receives Command& to addStep/setHelp/declare().
    static void reg(const std::string& key, std::function<void(Command&)> builder,
                    const std::string& help = "", char sep = VE_FACTORY_KEY_SEP);

    // Lightweight query (1-liner inlines — no Command instance constructed)
    static bool has(const std::string& key, char sep = VE_FACTORY_KEY_SEP)
    {
        const Factory& f = factory();
        return f.node(key, sep) != nullptr;
    }
    static Strings keys() { return factory().keys(); }

    // Build-time helper: create _declare/ subtree for parameter declarations.
    static Node* declareNode(const std::string& key, char sep = VE_FACTORY_KEY_SEP);
};

// ============================================================================
// command:: namespace
// ============================================================================

namespace command {

// --- thin forwarders (default sep only; advanced users instantiate Command/Step directly) ---

template<typename F, Step::EnableIfFn<F> = 0>
inline void reg(const std::string& key, F&& fn, const std::string& help = "")
{
    Step::reg(key, std::forward<F>(fn), help);
}

template<typename F, Step::EnableIfFn<F> = 0>
inline void reg(const std::string& key, F&& fn, LoopRef loop, const std::string& help = "")
{
    Step::reg(key, std::forward<F>(fn), std::move(loop), help);
}

inline void build(const std::string& key,
                  std::function<void(Command&)> builder,
                  const std::string& help = "")
{
    Command::reg(key, std::move(builder), help);
}

// Lightweight query (no Command instance)
inline bool    has(const std::string& key) { return Command::has(key); }
inline Strings keys()                      { return Command::keys(); }

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

inline Node* context(const std::string& key, Node* currentNode = nullptr)
{
    return Command(key).context(currentNode);
}
inline Node* declareNode(const std::string& key) { return Command::declareNode(key); }

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
