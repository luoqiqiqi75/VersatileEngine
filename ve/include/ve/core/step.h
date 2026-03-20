// ----------------------------------------------------------------------------
// step.h — ve::Step: single execution unit wrapping Result(const Var&)
// ----------------------------------------------------------------------------
// Copyright (c) 2023-present Thilo and VersatileEngine contributors.
// Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
// See LICENSE file in the project root for full license information.
// ----------------------------------------------------------------------------

#pragma once

#include "result.h"
#include "loop.h"

namespace ve {

class VE_API Step
{
public:
    using StepFn = std::function<Result(const Var&)>;

    Step() = default;
    Step(StepFn fn);
    Step(const std::string& name, StepFn fn);
    Step(const std::string& name, StepFn fn, LoopRef loop);

    // Convenience template: adapt various function signatures into StepFn.
    //   Result(const Var&)   — direct
    //   Result()             — ignore input
    //   void(const Var&)     — return SUCCESS
    //   void()               — return SUCCESS
    //   bool(const Var&)     — true → SUCCESS, false → FAIL
    //   bool()               — true → SUCCESS, false → FAIL
    //   Result(T)            — Var auto-unpack single arg
    template<typename F, std::enable_if_t<
        basic::FnTraits<std::decay_t<F>>::IsFunction
        && !std::is_convertible_v<F, StepFn>, int> = 0>
    Step(const std::string& name, F fn, LoopRef loop = {})
        : _name(name), _loop(std::move(loop))
    {
        _fn = wrapFn(std::move(fn));
    }

    Result exec(const Var& input = {}) const;

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
                return [f = std::move(fn)](const Var&) -> Result { return f(); };
            else if constexpr (std::is_same_v<Ret, bool>)
                return [f = std::move(fn)](const Var&) -> Result { return Result(f()); };
            else
                return [f = std::move(fn)](const Var&) -> Result { f(); return Result::SUCCESS; };
        }
        else if constexpr (Traits::ArgCnt == 1) {
            using Arg0 = std::decay_t<typename Traits::template ArgAt<0>>;
            if constexpr (std::is_same_v<Arg0, Var>) {
                if constexpr (std::is_same_v<Ret, Result>)
                    return StepFn(std::move(fn));
                else if constexpr (std::is_same_v<Ret, bool>)
                    return [f = std::move(fn)](const Var& v) -> Result { return Result(f(v)); };
                else
                    return [f = std::move(fn)](const Var& v) -> Result { f(v); return Result::SUCCESS; };
            } else {
                if constexpr (std::is_same_v<Ret, Result>)
                    return [f = std::move(fn)](const Var& v) -> Result { return f(v.as<Arg0>()); };
                else if constexpr (std::is_same_v<Ret, bool>)
                    return [f = std::move(fn)](const Var& v) -> Result { return Result(f(v.as<Arg0>())); };
                else
                    return [f = std::move(fn)](const Var& v) -> Result { f(v.as<Arg0>()); return Result::SUCCESS; };
            }
        }
        else {
            return [](const Var&) -> Result { return Result::FAIL; };
        }
    }

    std::string _name;
    StepFn      _fn;
    LoopRef     _loop;
    std::string _inputDesc;
    std::string _outputDesc;
};

} // namespace ve
