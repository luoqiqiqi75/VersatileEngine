// ----------------------------------------------------------------------------
// impl/command_reg_inl.h — command::reg<...> template implementations (inline)
// ----------------------------------------------------------------------------
// Copyright (c) 2023-present Thilo and VersatileEngine contributors.
// Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
// See LICENSE file in the project root for full license information.
// ----------------------------------------------------------------------------
//
// Included from command.h. Defines command::reg<RegProtoT>(...) +
// convenience family (reg / regProc / regVar / regAction /
// regResult). Each generates a Proc via RegProto<tag::X>::wrap and stores it
// to the factory node along with the in/out schema.

#pragma once

namespace ve::command {

// ----- full form ----------------------------------------------------------

template<typename RegProtoT, typename F>
inline void reg(const std::string& key, F&& fn,
                const std::string& help, Loop* lr,
                char sep)
{
    auto& f = factory();
    Node*  nd = f.node(key, sep);   // ensure exists

    Proc      p   = RegProtoT::template wrap<std::decay_t<F>>(std::forward<F>(fn));
    InSchema  ins = RegProtoT::template inSchemaOf <std::decay_t<F>>();
    OutSchema outs= RegProtoT::template outSchemaOf<std::decay_t<F>>();

    nd->at("_proc")     ->set(Var::custom(std::move(p)));
    nd->at("_in_schema")->set(Var::custom(std::move(ins)));
    nd->at("_out_schema")->set(Var::custom(std::move(outs)));

    if (!help.empty()) nd->at("help")->set(help);
    if (lr)            nd->at("loop")->set(Var(static_cast<void*>(lr)));

    // tracked-key bookkeeping: call factory::reg with a sentinel callable so the
    // key shows up in factory().keys(). The actual proc is in _proc subnode.
    auto keys = f.keys();
    if (std::find(keys.begin(), keys.end(), key) == keys.end()) {
        f.reg(key, nd, Var(), help, nullptr);
    }
}


// ----- convenience family (different names, no overload ambiguity) -------

// SmartProto: pick the most-specific RegProto tag from F's signature.
// This is the only place where "auto tag detection" happens — all other
// convenience functions (regProc / regAction / regVar / ...) bind a fixed tag.
// Explicit form `reg<RegProto<tag::X>>(...)` always overrides this.
namespace detail {

template<typename F>
struct SmartProto
{
    using Tr   = basic::FnTraits<std::decay_t<F>>;
    static constexpr std::size_t N = Tr::ArgCnt;

private:
    // SafeArg<I>: returns std::decay_t<ArgAt<I>> when I < N, else void.
    // Required because `std::is_same_v<ArgAt<1>, Node*>` would force
    // instantiation of ArgAt<1> even when N < 2 (tuple-out-of-bounds).
    template<std::size_t I, bool InRange = (I < N)>
    struct SafeArg { using type = void; };
    template<std::size_t I>
    struct SafeArg<I, true> { using type = std::decay_t<typename Tr::template ArgAt<I>>; };

    template<std::size_t I> using A = typename SafeArg<I>::type;

public:
    static constexpr bool is0_node = std::is_same_v<A<0>, Node*>;
    static constexpr bool is1_node = std::is_same_v<A<1>, Node*>;
    static constexpr bool is0_var  = std::is_same_v<A<0>, Var>;

    // 2-node Proc signature: (Node* in, Node* out).
    //   N==0                    → VoidAction
    //   N==1, Arg0=Var          → VarSingle
    //   N==2, Arg0=Node*, Arg1=Node* → FullProc (raw Proc)
    //   anything else (positional R fn(A1,…,An)) → PositionalArgs
    using Tag =
        std::conditional_t<N == 0,                                             tag::VoidAction,
        std::conditional_t<N == 1 && is0_var,                                  tag::VarSingle,
        std::conditional_t<N == 2 && is0_node && is1_node,                     tag::FullProc,
                                                                               tag::PositionalArgs>>>;
};

} // namespace detail

template<typename F>
inline void reg(const std::string& key, F&& fn, const std::string& help, Loop* lr)
{
    using Tag = typename detail::SmartProto<F>::Tag;
    reg<RegProto<Tag>>(key, std::forward<F>(fn), help, lr);
}

// Three-arg form: reg(key, fn, Loop*) - help defaults to empty.
// (matches the historical user pattern `command::reg(key, fn, loop)`.)
template<typename F>
inline void reg(const std::string& key, F&& fn, Loop* lr)
{
    reg(key, std::forward<F>(fn), std::string{}, lr);
}

// Four-arg form: reg(key, fn, Loop*, help) - legacy "loop-before-help" order.
template<typename F>
inline void reg(const std::string& key, F&& fn, Loop* lr, const std::string& help)
{
    reg(key, std::forward<F>(fn), help, lr);
}

template<typename F>
inline void regProc(const std::string& key, F&& fn, const std::string& help, Loop* lr)
{ reg<RegProto<tag::FullProc>>(key, std::forward<F>(fn), help, lr); }

template<typename F>
inline void regVar(const std::string& key, F&& fn, const std::string& help, Loop* lr)
{ reg<RegProto<tag::VarSingle>>(key, std::forward<F>(fn), help, lr); }

template<typename F>
inline void regAction(const std::string& key, F&& fn, const std::string& help, Loop* lr)
{ reg<RegProto<tag::VoidAction>>(key, std::forward<F>(fn), help, lr); }

template<typename F>
inline void regResult(const std::string& key, F&& fn, const std::string& help, Loop* lr)
{ reg<RegProto<tag::ResultArgs>>(key, std::forward<F>(fn), help, lr); }

} // namespace ve::command


// ============================================================================
// Command::call<CallProtoT> — defined here (after Pipeline is includable)
// ============================================================================
//
// Note: Command::call<CallProtoT> body needs Pipeline (transient instance).
// Pipeline is forward-declared in command.h; full definition in pipeline.h.
// Implementation lives in command.cpp as an explicit function (not template-inline)
// to keep header dependency minimal — pipeline.h includes command.h.

// (Command::call<CallProtoT> implementation is in command.cpp via a non-template
// dispatch helper that takes a Proc-wrapping closure. See command.cpp.)
