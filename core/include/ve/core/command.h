//
// command.h — ve::Result, ve::Step, ve::Command, ve::command::
//
// Command execution engine with Factory-based registration.
// Supports: simple call (zero-alloc), serial Step chain, async via LoopRef.
//
// Design doc: docs/internal/plan/command-design.md
//
// Created by luoqi on 2026/3/16.
//

#pragma once

#include "factory.h"
#include "var.h"
#include "loop.h"

#include <chrono>

namespace ve {

// ============================================================================
// Result — unified command return type
// ============================================================================
//
//   SUCCESS (0)   — command completed synchronously
//   FAIL    (<0)  — error (negative codes)
//   ACCEPT  (>0)  — async: accepted, result delivered via handler
//

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
    Result(int code, const std::string& text = "") : _code(code), _text(text) {}
    Result(bool ok, int err = FAIL) : _code(ok ? SUCCESS : err) {}

    int              code() const { return _code; }
    const std::string& text() const { return _text; }
    const Var&       content() const { return _content; }

    bool isSuccess()  const { return _code == 0; }
    bool isError()    const { return _code < 0; }
    bool isAccepted() const { return _code > 0; }

    Result& setContent(const Var& v) { _content = v; return *this; }
    Result& setContent(Var&& v)      { _content = std::move(v); return *this; }
    Result& setText(const std::string& t) { _text = t; return *this; }

    std::string toString() const;

    explicit operator bool() const { return isSuccess(); }

private:
    int         _code;
    std::string _text;
    Var         _content;
};

// ============================================================================
// Step — single execution unit in a command chain
// ============================================================================

class Command;

class Step
{
public:
    using Fn = std::function<Result(Command&)>;

    Step() = default;
    Step(Fn fn, LoopRef loop = {})
        : _fn(std::move(fn)), _loop(loop) {}
    Step(Fn fn, const std::string& name, LoopRef loop = {})
        : _fn(std::move(fn)), _name(name), _loop(loop) {}

    explicit operator bool() const { return !!_fn; }

    const std::string& name() const { return _name; }
    LoopRef loop() const { return _loop; }

    Result operator()(Command& cmd) const { return _fn(cmd); }

    // --- factory methods ---

    static Step fromVoid(std::function<void()> fn, LoopRef loop = {}) {
        return Step([f = std::move(fn)](Command&) -> Result { f(); return Result::SUCCESS; }, loop);
    }

    static Step fromResult(std::function<Result()> fn, LoopRef loop = {}) {
        return Step([f = std::move(fn)](Command&) -> Result { return f(); }, loop);
    }

    template<typename F>
    static Step fromInput(F fn, LoopRef loop = {});

    template<typename F, typename T>
    static Step fromBind(F fn, T value, LoopRef loop = {}) {
        return Step([f = std::move(fn), v = std::move(value)](Command&) -> Result {
            using Ret = typename basic::FnTraits<F>::RetT;
            if constexpr (std::is_same_v<Ret, Result>)
                return f(v);
            else if constexpr (std::is_same_v<Ret, bool>)
                return Result(f(v));
            else {
                f(v); return Result::SUCCESS;
            }
        }, loop);
    }

private:
    Fn          _fn;
    std::string _name;
    LoopRef     _loop;
};


// ============================================================================
// Command — execution instance (non-copyable)
// ============================================================================

class VE_API Command
{
public:
    using ResultHandler = std::function<void(const Result&)>;

    ~Command();

    // --- identity ---
    const std::string& key() const { return _key; }

    // --- private data storage (Var Dict) ---
    Var              data(const std::string& name) const;
    template<typename T>
    T                data(const std::string& name) const { return data(name).as<T>(); }

    void             setData(const std::string& name, const Var& v);
    template<typename T>
    void             setData(const std::string& name, const T& v) { setData(name, Var(v)); }

    bool             hasData(const std::string& name) const;

    // --- input shorthand ---
    static constexpr const char* INPUT_KEY = "_input";

    Var              input() const { return data(INPUT_KEY); }
    template<typename T>
    T                input() const { return data<T>(INPUT_KEY); }

    void             setInput(const Var& v) { setData(INPUT_KEY, v); }
    template<typename T>
    void             setInput(const T& v) { setData(INPUT_KEY, Var(v)); }

    // --- step chain ---
    void             addStep(const Step& step);
    void             addStep(Step&& step);
    void             prependStep(const Step& step);
    int              stepCount() const { return static_cast<int>(_steps.size()); }

    // --- result handler ---
    void             setResultHandler(const ResultHandler& h) { _result_handler = h; }

    // --- execution ---
    Result           start();
    void             finish(const Result& result);

    // --- state ---
    bool             isRunning()  const { return _running; }
    bool             isFinished() const { return _finished; }

    // --- debug info ---
    int              currentStepIndex() const { return _step_idx; }
    std::string      currentStepName()  const;
    int64_t          elapsedUs()        const;

    // internal: used by command:: namespace and CommandGraph
    explicit Command(const std::string& key);

private:
    Command(const Command&) = delete;
    Command& operator=(const Command&) = delete;

    Result runCurrentStep();
    void   advance(const Result& res);

    std::string   _key;
    Var           _data;             // Dict type
    Vector<Step>  _steps;
    int           _step_idx    = 0;
    ResultHandler _result_handler;
    bool          _running     = false;
    bool          _finished    = false;
    int64_t       _start_time  = 0;
};


// ============================================================================
// Step::fromInput — deferred definition (needs Command)
// ============================================================================

template<typename F>
Step Step::fromInput(F fn, LoopRef loop) {
    return Step([f = std::move(fn)](Command& cmd) -> Result {
        using Traits = basic::FnTraits<F>;
        using ArgT   = typename Traits::template ArgAt<0>;
        auto val = cmd.input<ArgT>();
        using Ret = typename Traits::RetT;
        if constexpr (std::is_same_v<Ret, Result>)
            return f(val);
        else if constexpr (std::is_same_v<Ret, bool>)
            return Result(f(val));
        else {
            f(val); return Result::SUCCESS;
        }
    }, loop);
}


// ============================================================================
// CommandFactory — Factory-based command registry
// ============================================================================

using CommandFactory = Factory<Result(Command&)>;
VE_API CommandFactory& commandFactory();


// ============================================================================
// command:: — namespace-level API (register / execute / query)
// ============================================================================
//
// Registration delegates to commandFactory(). The reg() overloads are
// syntactic sugar that adapt various callable signatures into Step chains.
//

namespace command {

// --- registration (delegates to commandFactory) ---

VE_API void reg(const std::string& key, const Vector<Step>& steps);
VE_API void reg(const std::string& key, std::initializer_list<Step> steps);

// Single-step registration with FnTraits auto-deduction
template<typename F, typename = std::enable_if_t<basic::FnTraits<std::decay_t<F>>::IsFunction>>
void reg(const std::string& key, F fn, LoopRef loop = {}) {
    using Traits = basic::FnTraits<std::decay_t<F>>;
    if constexpr (Traits::ArgCnt == 0) {
        reg(key, { Step::fromResult([f = std::move(fn)]() -> Result {
            using Ret = typename Traits::RetT;
            if constexpr (std::is_same_v<Ret, Result>) return f();
            else if constexpr (std::is_same_v<Ret, bool>) return Result(f());
            else { f(); return Result::SUCCESS; }
        }, loop) });
    } else if constexpr (Traits::ArgCnt == 1 &&
                         std::is_same_v<typename Traits::template ArgAt<0>, Command&>) {
        reg(key, { Step(std::move(fn), loop) });
    } else if constexpr (Traits::ArgCnt == 1) {
        reg(key, { Step::fromInput(std::move(fn), loop) });
    }
}

// Registration with bound parameter
template<typename F, typename T>
void reg(const std::string& key, F fn, T value, LoopRef loop = {}) {
    reg(key, { Step::fromBind(std::move(fn), std::move(value), loop) });
}

// Member function registration
template<typename Ret, typename Class, typename... Args>
void reg(const std::string& key, Ret(Class::*fn)(Args...), Class* obj, LoopRef loop = {}) {
    if constexpr (sizeof...(Args) == 0) {
        reg(key, { Step::fromResult([obj, fn]() -> Result {
            if constexpr (std::is_same_v<Ret, Result>) return (obj->*fn)();
            else if constexpr (std::is_same_v<Ret, bool>) return Result((obj->*fn)());
            else { (obj->*fn)(); return Result::SUCCESS; }
        }, loop) });
    } else {
        reg(key, { Step([obj, fn](Command& cmd) -> Result {
            using ArgT = typename basic::_t_list<Args...>::FirstT;
            auto val = cmd.input<ArgT>();
            if constexpr (std::is_same_v<Ret, Result>) return (obj->*fn)(val);
            else if constexpr (std::is_same_v<Ret, bool>) return Result((obj->*fn)(val));
            else { (obj->*fn)(val); return Result::SUCCESS; }
        }, loop) });
    }
}

VE_API bool    unreg(const std::string& key);

// --- query (delegates to commandFactory) ---

VE_API bool    has(const std::string& key);
VE_API Strings keys();

// --- simple call (zero Command allocation, sync only) ---
// For single-step sync commands: directly invokes the registered function
// without allocating a persistent Command object.

VE_API Result  call(const std::string& key, const Var& input = {});

// --- full execution (allocates Command, supports async step chain) ---

VE_API Command* create(const std::string& key);
VE_API Command* create(const std::string& key, const Command::ResultHandler& handler);

VE_API Result   exec(const std::string& key);
VE_API Result   exec(const std::string& key, const Var& input);
VE_API Result   exec(const std::string& key, const Var& input,
                     const Command::ResultHandler& handler);

template<typename T>
Result exec(const std::string& key, const T& input) {
    return exec(key, Var(input));
}

template<typename T>
Result exec(const std::string& key, const T& input,
            const Command::ResultHandler& handler) {
    return exec(key, Var(input), handler);
}

} // namespace command


// ============================================================================
// CommandGraph — DAG execution (topological sort + parallel dispatch)
// ============================================================================
//
// Build a directed acyclic graph of Steps. Nodes with no unresolved
// dependencies execute in parallel (dispatched via LoopRef if provided).
// Execution fails fast on first error.
//

class VE_API CommandGraph
{
public:
    CommandGraph() = default;
    ~CommandGraph() = default;

    // Add a named step node to the graph
    void addNode(const std::string& id, Step step);

    // Add a dependency edge: `from` must complete before `to` starts
    void addEdge(const std::string& from, const std::string& to);

    // Execute the graph. Blocks until all steps complete or first error.
    // Uses `loop` for parallel dispatch of independent nodes.
    Result execute(LoopRef loop = {});

    int nodeCount() const { return static_cast<int>(_nodes.size()); }

private:
    struct GraphNode {
        std::string id;
        Step        step;
        Strings     deps;     // IDs this node depends on
    };
    Vector<GraphNode> _nodes;
    Vector<std::pair<std::string, std::string>> _edges;
};

} // namespace ve
