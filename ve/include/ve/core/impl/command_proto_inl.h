// ----------------------------------------------------------------------------
// impl/command_proto_inl.h — RegProto<tag::X> specializations (inline, 2-node)
// ----------------------------------------------------------------------------
// Copyright (c) 2023-present Thilo and VersatileEngine contributors.
// Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
// See LICENSE file in the project root for full license information.
// ----------------------------------------------------------------------------
//
// Included from command.h. Each RegProto<tag::X> wraps a user callable into a
// Proc — `Result(Node* in, Node* out)`. Compile-time signature checks via
// static_assert (Q-pc-2). std::exception caught and mapped to
// Result::fail(Result::EXCEPTION, e.what()) (Q-reg-3).

#pragma once

// command.h includes this; we rely on its declarations of Proc / Result / InSchema / OutSchema / tag::*.

namespace ve {

namespace detail {

// ----- exception trampoline ---------------------------------------------------
template<typename F>
inline Result invokeWithExceptionGuard(F&& body)
{
    try {
        return body();
    } catch (const std::exception& e) {
        return Result::fail(Result::EXCEPTION, e.what());
    } catch (...) {
        return Result::fail(Result::EXCEPTION, "<unknown exception>");
    }
}

// ----- argument extraction helpers --------------------------------------------
template<typename T>
inline T extractPositional(Node* in, int idx, Node* declare)
{
    // 1. try in->atKey(idx)  (const variant; find-only, returns nullptr if missing)
    if (in) {
        if (Node* p = const_cast<const Node*>(in)->atKey(idx, false)) {
            Var v = p->get();
            if (!v.isNull()) return v.as<T>();
        }
    }
    // 2. fall back to declare/<idx>/_default if declare exists
    if (declare) {
        if (Node* slot = declare->child(idx)) {
            if (Node* def = slot->find("_default", false)) {
                Var v = def->get();
                if (!v.isNull()) return v.as<T>();
            }
        }
    }
    // 3. type default (0 / "" / false / Var())
    return T{};
}

// ----- return-type write helpers ----------------------------------------------
template<typename R, typename F, typename... Args>
inline Result invokeAndPackReturn(F&& fn, Node* out, Args&&... args)
{
    using Ret = std::decay_t<R>;
    if constexpr (std::is_void_v<Ret>) {
        fn(std::forward<Args>(args)...);
        return Result::ok();
    }
    else if constexpr (std::is_same_v<Ret, Result>) {
        // user returned full Result; framework will pack to ctx/code/message + import data to out
        return fn(std::forward<Args>(args)...);
    }
    else if constexpr (std::is_same_v<Ret, int>) {
        int c = fn(std::forward<Args>(args)...);
        return Result(c);
    }
    else {
        Ret r = fn(std::forward<Args>(args)...);
        if (out) out->set(Var(std::move(r)));
        return Result::ok();
    }
}

} // namespace detail


// ============================================================================
// RegProto<tag::FullProc>  — raw 2-node Proc passthrough
// ============================================================================
//
// user fn signature: Result fn(Node* in, Node* out)
//
// This is the most flexible form — equivalent to defining a Proc directly.

template<>
struct RegProto<tag::FullProc>
{
    template<typename F>
    static Proc wrap(F&& fn)
    {
        using Tr = basic::FnTraits<std::decay_t<F>>;
        static_assert(Tr::IsFunction, "RegProto<FullProc>: fn is not callable");
        static_assert(Tr::ArgCnt == 2,
            "RegProto<FullProc>: fn must take (Node* in, Node* out)");
        return [fn = std::forward<F>(fn)](Node* in, Node* out) -> Result {
            return detail::invokeWithExceptionGuard([&]{ return Result(fn(in, out)); });
        };
    }

    template<typename F> static InSchema  inSchemaOf () { return {InSchema::FreeForm}; }
    template<typename F> static OutSchema outSchemaOf() { return {OutSchema::FreeForm}; }
};


// ============================================================================
// RegProto<tag::VoidAction>  — side-effect only (no args)
// ============================================================================
//
// user fn signature: void fn() / int fn() / Result fn()

template<>
struct RegProto<tag::VoidAction>
{
    template<typename F>
    static Proc wrap(F&& fn)
    {
        using Tr = basic::FnTraits<std::decay_t<F>>;
        static_assert(Tr::IsFunction, "RegProto<VoidAction>: fn is not callable");
        static_assert(Tr::ArgCnt == 0, "RegProto<VoidAction>: fn must take no args");
        using Ret = typename Tr::RetT;
        return [fn = std::forward<F>(fn)](Node* /*in*/, Node* out) -> Result {
            return detail::invokeWithExceptionGuard([&]{
                return detail::invokeAndPackReturn<Ret>(fn, out);
            });
        };
    }

    template<typename F> static InSchema  inSchemaOf () { return {InSchema::Empty}; }
    template<typename F> static OutSchema outSchemaOf() { return {OutSchema::FreeForm}; }
};


// ============================================================================
// RegProto<tag::VarSingle>  — single Var in / R out (functional style)
// ============================================================================
//
// user fn signature: R fn(Var) — R may be void / Var / Result / scalar / string

template<>
struct RegProto<tag::VarSingle>
{
    template<typename F>
    static Proc wrap(F&& fn)
    {
        using Tr = basic::FnTraits<std::decay_t<F>>;
        static_assert(Tr::IsFunction, "RegProto<VarSingle>: fn is not callable");
        static_assert(Tr::ArgCnt == 1, "RegProto<VarSingle>: fn must take a single Var arg");
        using Arg0 = std::decay_t<typename Tr::template ArgAt<0>>;
        static_assert(std::is_same_v<Arg0, Var>,
            "RegProto<VarSingle>: fn arg must be Var");
        using Ret = typename Tr::RetT;
        return [fn = std::forward<F>(fn)](Node* in, Node* out) -> Result {
            return detail::invokeWithExceptionGuard([&]{
                Var v = in ? in->get() : Var{};
                return detail::invokeAndPackReturn<Ret>(fn, out, std::move(v));
            });
        };
    }

    template<typename F> static InSchema  inSchemaOf () { return {InSchema::FreeForm}; }
    template<typename F> static OutSchema outSchemaOf() { return {OutSchema::FreeForm}; }
};


// ============================================================================
// RegProto<tag::PositionalArgs>  — positional args (most common default)
// ============================================================================
//
// user fn signature: R fn(A1, A2, ..., An) — R as in VarSingle

namespace detail {

// Ret comes first so callers can specify it explicitly while letting F /
// I / Args deduce from arguments.
template<typename Ret, typename F, std::size_t... I, typename... Args>
inline Result invokePositionalImpl(F&& fn, Node* in, Node* out, Node* declare,
                                   std::index_sequence<I...>,
                                   basic::_t_list<Args...>*)
{
    return invokeAndPackReturn<Ret>(std::forward<F>(fn), out,
        extractPositional<std::decay_t<Args>>(in, static_cast<int>(I), declare)...);
}

} // namespace detail

template<>
struct RegProto<tag::PositionalArgs>
{
    template<typename F>
    static Proc wrap(F&& fn)
    {
        using Tr = basic::FnTraits<std::decay_t<F>>;
        static_assert(Tr::IsFunction, "RegProto<PositionalArgs>: fn is not callable");
        static_assert(Tr::ArgCnt > 0,
            "RegProto<PositionalArgs>: fn must take at least one positional arg "
            "(use regAction / VoidAction for no-arg side-effects)");
        return [fn = std::forward<F>(fn)](Node* in, Node* out) mutable -> Result {
            // Re-derive Tr / Ret / ArgsT inside the lambda (don't rely on outer
            // captures — MSVC's two-phase lookup can fail to see them at the
            // make_index_sequence<N> instantiation point).
            using Tr2    = basic::FnTraits<std::decay_t<F>>;
            using Ret2   = typename Tr2::RetT;
            using ArgsT2 = typename Tr2::ArgsT;
            return detail::invokeWithExceptionGuard([&]{
                // declare/<param>/_default lookup: declare may be exposed via
                // the in node's shadow chain (set by caller / RegProto reg helper).
                Node* declare = nullptr;
                if (in) {
                    if (const Node* sh = in->shadow())
                        declare = const_cast<Node*>(sh);
                }
                return detail::invokePositionalImpl<Ret2>(
                    fn, in, out, declare,
                    std::make_index_sequence<Tr2::ArgCnt>{},
                    static_cast<ArgsT2*>(nullptr));
            });
        };
    }

    template<typename F> static InSchema  inSchemaOf () { return {InSchema::Positional}; }
    template<typename F> static OutSchema outSchemaOf() { return {OutSchema::FreeForm}; }
};


// ============================================================================
// RegProto<tag::ResultArgs>  — positional args + user explicitly returns Result
// ============================================================================
//
// Identical wrap to PositionalArgs but enforces Ret = Result at compile time.

template<>
struct RegProto<tag::ResultArgs>
{
    template<typename F>
    static Proc wrap(F&& fn)
    {
        using Tr = basic::FnTraits<std::decay_t<F>>;
        static_assert(Tr::IsFunction, "RegProto<ResultArgs>: fn is not callable");
        static_assert(std::is_same_v<typename Tr::RetT, Result>,
            "RegProto<ResultArgs>: fn must return Result");
        return RegProto<tag::PositionalArgs>::wrap(std::forward<F>(fn));
    }

    template<typename F> static InSchema  inSchemaOf () { return {InSchema::Positional}; }
    template<typename F> static OutSchema outSchemaOf() { return {OutSchema::FreeForm}; }
};

} // namespace ve
