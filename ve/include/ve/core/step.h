// ----------------------------------------------------------------------------
// step.h — ve::Step: single execution unit wrapping Result(Node* ctx)
// ----------------------------------------------------------------------------
// Copyright (c) 2023-present Thilo and VersatileEngine contributors.
// Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
// See LICENSE file in the project root for full license information.
// ----------------------------------------------------------------------------

#pragma once

#include "loop.h"
#include "var.h"

namespace ve {

using Result = ResultT<Var>;

namespace convert {
template<> inline bool parse(const Result& r, std::string& s)
{
    if (r.isSuccess()) s = "success";
    if (r.isAccepted()) s = "accepted";
    s = "error(" + std::to_string(r.code()) + ")";
    if (!r.content().isNull()) s += ": " + r.content().toString();
    return true;
}
}

class Node;

class VE_API Step
{
public:
    using StepFn = std::function<Result(Node* ctx)>;

    Step() = default;
    Step(StepFn fn);
    Step(const std::string& name, StepFn fn);
    Step(const std::string& name, StepFn fn, LoopRef loop);

    // Convenience template: adapt various function signatures into StepFn.
    //   Result(Node*)        - direct (new primary)
    //   Result(const Var&)   - legacy: ctx->get() as input
    //   Result()             - ignore input
    //   void(Node*)          - return SUCCESS
    //   void(const Var&)     - return SUCCESS
    //   void()               - return SUCCESS
    //   bool(Node*)          - true -> SUCCESS, false -> FAIL
    //   bool(const Var&)     - true -> SUCCESS, false -> FAIL
    //   bool()               - true -> SUCCESS, false -> FAIL
    template<typename F, std::enable_if_t<
        basic::FnTraits<std::decay_t<F>>::IsFunction
        && !std::is_convertible_v<F, StepFn>, int> = 0>
    Step(const std::string& name, F fn, LoopRef loop = {})
        : _name(name), _loop(std::move(loop))
    {
        _fn = wrapFn(std::move(fn));
    }

    Result exec(Node* ctx = nullptr) const;
    Result exec(const Var& input) const;  // backward compat: wraps in temp node

    const std::string& name() const { return _name; }
    const LoopRef& loop() const { return _loop; }

    Step& setInput(const std::string& desc)  { _inputDesc = desc; return *this; }
    Step& setOutput(const std::string& desc) { _outputDesc = desc; return *this; }
    const std::string& inputDesc() const  { return _inputDesc; }
    const std::string& outputDesc() const { return _outputDesc; }

    Step clone() const;
    explicit operator bool() const { return !!_fn; }

private:
    template<typename F>
    static StepFn wrapFn(F fn)
    {
        using Traits = basic::FnTraits<std::decay_t<F>>;
        using Ret = typename Traits::RetT;

        if constexpr (Traits::ArgCnt == 0) {
            if constexpr (std::is_same_v<Ret, Result>)
                return [f = std::move(fn)](Node*) -> Result { return f(); };
            else if constexpr (std::is_same_v<Ret, bool>)
                return [f = std::move(fn)](Node*) -> Result { return Result(f()); };
            else
                return [f = std::move(fn)](Node*) -> Result { f(); return Result::SUCCESS; };
        }
        else if constexpr (Traits::ArgCnt == 1) {
            using Arg0 = std::decay_t<typename Traits::template ArgAt<0>>;

            if constexpr (std::is_same_v<Arg0, Node*>) {
                // New primary: Result(Node*)
                if constexpr (std::is_same_v<Ret, Result>)
                    return StepFn(std::move(fn));
                else if constexpr (std::is_same_v<Ret, bool>)
                    return [f = std::move(fn)](Node* ctx) -> Result { return Result(f(ctx)); };
                else
                    return [f = std::move(fn)](Node* ctx) -> Result { f(ctx); return Result::SUCCESS; };
            }
            else if constexpr (std::is_same_v<Arg0, Var>) {
                // Legacy: Result(const Var&) - extract value from context node
                if constexpr (std::is_same_v<Ret, Result>)
                    return [f = std::move(fn)](Node* ctx) -> Result { return f(ctx ? ctx->get() : Var()); };
                else if constexpr (std::is_same_v<Ret, bool>)
                    return [f = std::move(fn)](Node* ctx) -> Result { return Result(f(ctx ? ctx->get() : Var())); };
                else
                    return [f = std::move(fn)](Node* ctx) -> Result { f(ctx ? ctx->get() : Var()); return Result::SUCCESS; };
            }
            else {
                if constexpr (std::is_same_v<Ret, Result>)
                    return [f = std::move(fn)](Node* ctx) -> Result { return f((ctx ? ctx->get() : Var()).as<Arg0>()); };
                else if constexpr (std::is_same_v<Ret, bool>)
                    return [f = std::move(fn)](Node* ctx) -> Result { return Result(f((ctx ? ctx->get() : Var()).as<Arg0>())); };
                else
                    return [f = std::move(fn)](Node* ctx) -> Result { f((ctx ? ctx->get() : Var()).as<Arg0>()); return Result::SUCCESS; };
            }
        }
        else {
            return [](Node*) -> Result { return Result::FAIL; };
        }
    }

    std::string _name;
    StepFn      _fn;
    LoopRef     _loop;
    std::string _inputDesc;
    std::string _outputDesc;
};

} // namespace ve
