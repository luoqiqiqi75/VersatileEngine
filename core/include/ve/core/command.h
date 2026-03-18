//
// command.h — Node-centric command system
//
// Step   = Node + connected slot (fn + loop + result)
// Command = Node subtree under /command/
// DAG     = shadow edges between step Nodes
//
// Design doc: docs/internal/plan/command-design.md
//

#pragma once

#include "node.h"

namespace ve {

// ============================================================================
// Result — unified command return type
// ============================================================================

class VE_API Result
{
public:
    enum Code : int {
        SUCCESS =  0,
        FAIL    = -1,
        ACCEPT  =  1,
    };

    Result() : _code(SUCCESS) {}
    Result(Code code) : _code(code) {}
    Result(int code, const Var& content) : _code(code), _content(content) {}
    Result(int code, const std::string& err) : _code(code), _content(err) {}
    Result(bool ok, int err = FAIL) : _code(ok ? SUCCESS : err) {}

    int        code() const { return _code; }
    const Var& content() const { return _content; }

    bool isSuccess()  const { return _code == 0; }
    bool isError()    const { return _code < 0; }
    bool isAccepted() const { return _code > 0; }

    Result& setContent(const Var& v) { _content = v; return *this; }
    Result& setContent(Var&& v)      { _content = std::move(v); return *this; }

    std::string toString() const;

    explicit operator bool() const { return isSuccess(); }

private:
    int _code;
    Var _content;
};

// ============================================================================
// Command signal
// ============================================================================

enum CommandSignal : Object::SignalT {
    CMD_DONE = 0x0030,   // (Var result) — step/command completed
};

// ============================================================================
// StepFn / StepInfo — step function type + registration helper
// ============================================================================

using StepFn = std::function<Result(Node*)>;

struct StepInfo {
    std::string name;
    StepFn      fn;
    LoopRef     loop = {};
};

// ============================================================================
// command:: — namespace-level API
// ============================================================================
//
// Registration creates Node subtrees under /command/.
// For multi-step, shadow chains are auto-wired sequentially.
// CMD_DONE signals chain async steps.
//

namespace command {

VE_API Node* root();

// --- registration ---

VE_API Node* reg(const std::string& key, StepFn fn, LoopRef loop = {});
VE_API Node* reg(const std::string& key, std::initializer_list<StepInfo> steps);

template<typename F, typename = std::enable_if_t<basic::FnTraits<std::decay_t<F>>::IsFunction
    && !std::is_convertible_v<F, StepFn>>>
Node* reg(const std::string& key, F fn, LoopRef loop = {}) {
    using Traits = basic::FnTraits<std::decay_t<F>>;
    using Ret = typename Traits::RetT;

    if constexpr (Traits::ArgCnt == 1 &&
                  std::is_same_v<typename Traits::template ArgAt<0>, Node*>) {
        if constexpr (std::is_same_v<Ret, Result>)
            return reg(key, StepFn(std::move(fn)), loop);
        else if constexpr (std::is_same_v<Ret, bool>)
            return reg(key, StepFn([f = std::move(fn)](Node* n) -> Result { return Result(f(n)); }), loop);
        else
            return reg(key, StepFn([f = std::move(fn)](Node* n) -> Result { f(n); return Result::SUCCESS; }), loop);
    }
    else if constexpr (Traits::ArgCnt == 0) {
        if constexpr (std::is_same_v<Ret, Result>)
            return reg(key, StepFn([f = std::move(fn)](Node*) -> Result { return f(); }), loop);
        else if constexpr (std::is_same_v<Ret, bool>)
            return reg(key, StepFn([f = std::move(fn)](Node*) -> Result { return Result(f()); }), loop);
        else
            return reg(key, StepFn([f = std::move(fn)](Node*) -> Result { f(); return Result::SUCCESS; }), loop);
    }
    else if constexpr (Traits::ArgCnt == 1) {
        using ArgT = typename Traits::template ArgAt<0>;
        if constexpr (std::is_same_v<Ret, Result>)
            return reg(key, StepFn([f = std::move(fn)](Node* n) -> Result { return f(n->get<ArgT>()); }), loop);
        else if constexpr (std::is_same_v<Ret, bool>)
            return reg(key, StepFn([f = std::move(fn)](Node* n) -> Result { return Result(f(n->get<ArgT>())); }), loop);
        else
            return reg(key, StepFn([f = std::move(fn)](Node* n) -> Result { f(n->get<ArgT>()); return Result::SUCCESS; }), loop);
    }
    else {
        return nullptr;
    }
}

template<typename Ret, typename Class, typename... Args>
Node* reg(const std::string& key, Ret(Class::*fn)(Args...), Class* obj, LoopRef loop = {}) {
    if constexpr (sizeof...(Args) == 0) {
        return reg(key, [obj, fn](Node*) -> Result {
            if constexpr (std::is_same_v<Ret, Result>) return (obj->*fn)();
            else if constexpr (std::is_same_v<Ret, bool>) return Result((obj->*fn)());
            else { (obj->*fn)(); return Result::SUCCESS; }
        }, loop);
    } else {
        using ArgT = typename basic::_t_list<Args...>::FirstT;
        return reg(key, [obj, fn](Node* n) -> Result {
            auto val = n->get<ArgT>();
            if constexpr (std::is_same_v<Ret, Result>) return (obj->*fn)(val);
            else if constexpr (std::is_same_v<Ret, bool>) return Result((obj->*fn)(val));
            else { (obj->*fn)(val); return Result::SUCCESS; }
        }, loop);
    }
}

VE_API bool unreg(const std::string& key);

// --- query ---

VE_API bool    has(const std::string& key);
VE_API Strings keys();

// --- execution ---

// Sync call: traverses step chain, returns final Result.
VE_API Result call(const std::string& key, const Var& input = {});

// Full execution: supports async steps via LoopRef.
VE_API Result exec(const std::string& key, const Var& input = {});
VE_API Result exec(const std::string& key, const Var& input,
                   std::function<void(const Result&)> onDone);

template<typename T>
Result exec(const std::string& key, const T& input) { return exec(key, Var(input)); }

template<typename T>
Result exec(const std::string& key, const T& input,
            const std::function<void(const Result&)>& onDone) {
    return exec(key, Var(input), onDone);
}

} // namespace command

} // namespace ve
