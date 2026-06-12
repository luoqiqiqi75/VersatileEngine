// command.h - Proc, Command instance and command factory helpers
#pragma once

#include "factory.h"
#include "loop.h"
#include "node.h"
#include "var.h"

namespace ve {

struct Result
{
    enum Code : int {
        SUCCESS         = 0,
        FAILED          = -1,
        ACCEPTED        = 1
    };

    Result() = default;
    Result(const int c, std::string m = {}) : _code(c), _message(std::move(m)) {}
    Result(const Result& o) : _code(o.code()), _message(o._message) {}
    Result(Result&& o) noexcept : _code(o.code()), _message(std::move(o._message)) {}
    Result& operator=(const Result& o) { _code = o.code(); _message = o._message; return *this; }
    Result& operator=(Result&& o) noexcept { _code = o.code(); _message = std::move(o._message); return *this; }

    int code() const { return _code; }
    // rvalue overload returns by value: result().message() on a temporary
    // would otherwise hand out a reference into a destroyed Result
    const std::string& message() const& { return _message; }
    std::string message() && { return std::move(_message); }

    void setCode(int c) { _code = c; }
    template<typename E> std::enable_if_t<std::is_enum_v<E>> setCode(E ec) { setCode(static_cast<int>(ec)); }

    bool isSuccess() const { return code() == SUCCESS; }
    bool isError() const { return code() < 0; }
    bool isAccepted() const { return code() > 0; }
    explicit operator bool() const { return isSuccess(); }

    static Result ok() { return {SUCCESS}; }
    static Result fail(std::string message = {}) { return {FAILED, std::move(message)}; }
    static Result fail(int c, std::string message = {}) { return {c, std::move(message)}; }
    static Result accept(std::string message = {}) { return {ACCEPTED, std::move(message)}; }
    static Result accept(int c, std::string message = {}) { return {c < 0 ? -c : c, std::move(message)}; }

    template<typename E> static std::enable_if_t<std::is_enum_v<E>, Result> fail(E ec, std::string message = {})
    { return fail(static_cast<int>(ec), std::move(message)); }
    template<typename E> static std::enable_if_t<std::is_enum_v<E>, Result> accept(E ec, std::string message = {})
    { return accept(static_cast<int>(ec), std::move(message)); }

private:
    std::atomic<int> _code{FAILED};
    std::string      _message;
};

using Proc = std::function<Result(Node* ctx, Node* in, Node* out)>;

namespace convert
{

namespace detail {

// Generic callable -> Proc (the same indexed unpack as Var::wrapCallableIndexed):
// in is exported once as VarS and treated as the argument list — arg I comes
// from args[I].as<ArgT>(). The return value, unless Result/void, is imported
// onto out as VarS; the Proc's own Result is always ok.
template<typename F, std::size_t... I>
inline Proc wrapProc(F f, std::index_sequence<I...>)
{
    using T = basic::FnTraits<std::decay_t<F>>;
    using Ret = typename T::RetT;
    static_assert((!std::is_same_v<basic::_t_bare<typename T::template ArgAt<I>>, Node> && ...),
        "Node* args must be Result(Node* ctx), Result(Node* in, Node* out) or Result(Node*, Node*, Node*)");
    return [f = std::move(f)] (Node*, Node* in, Node* out) -> Result {
        [[maybe_unused]] Var args;
        if constexpr (sizeof...(I) > 0) args = schema::exportAs<schema::VarS>(in);
        if constexpr (std::is_void_v<Ret>) {
            f(args[I].template as<std::decay_t<typename T::template ArgAt<I>>>()...);
            return Result::ok();
        } else if constexpr (std::is_same_v<Ret, Result>) {
            return f(args[I].template as<std::decay_t<typename T::template ArgAt<I>>>()...);
        } else {
            schema::importAs<schema::VarS>(out, Var(f(args[I].template as<std::decay_t<typename T::template ArgAt<I>>>()...)));
            return Result::ok();
        }
    };
}

} // namespace detail

// Any callable -> Proc, dispatched on its real signature via FnTraits.
//   Result(Node*, Node*, Node*)   already a Proc, assigned as-is
//   Result(Node* in, Node* out)   Proc without ctx
//   Result(Node* ctx)             Proc input output use ctx
//   Result()                      Proc with nothing
//   anything else                 args unpacked from in (VarS list, arg I =
//                                 args[I].as<ArgT>()); a non-Result return is
//                                 imported onto out as VarS, Result is ok.
//   e.g. [](int a, int b) { return a + b; }   reads in/0 + in/1, writes out
template<typename F, std::enable_if_t<basic::Meta<std::decay_t<F>>::is_callable
    && !std::is_member_function_pointer_v<std::decay_t<F>>, int> = 0>
inline bool parse(F f, Proc& p)
{
    using T = basic::FnTraits<std::decay_t<F>>;
    constexpr bool is_ret_result = std::is_same_v<typename T::RetT, Result>;
    using ArgsT = typename T::ArgsTuple;
    constexpr bool is_args_n1 = std::is_same_v<ArgsT, std::tuple<Node*>>;
    constexpr bool is_args_n2 = std::is_same_v<ArgsT, std::tuple<Node*, Node*>>;
    constexpr bool is_args_n3 = std::is_same_v<ArgsT, std::tuple<Node*, Node*, Node*>>;

    if constexpr (is_ret_result && is_args_n3) {
        p = std::move(f); // Result(Node* ctx_n, Node* in_n, Node* out_n)
    } else if constexpr (is_ret_result && is_args_n2) {
        p = [f = std::move(f)] (Node*, Node* in_n, Node* out_n) -> Result { return f(in_n, out_n); }; // Result(Node* in_n, Node* out_n)
    } else if constexpr (is_ret_result && is_args_n1) {
        p = [f = std::move(f)] (Node* ctx_n, Node*, Node*) -> Result { return f(ctx_n); }; // Result(Node* ctx_n)
    } else if constexpr (is_ret_result && T::ArgCnt == 0) {
        p = [f = std::move(f)] (Node*, Node*, Node*) -> Result { return f(); }; // Result()
    } else {
        p = detail::wrapProc(std::move(f), std::make_index_sequence<T::ArgCnt>{});
    }
    return true;
}

}

class VE_API Command : public NodeRef
{
public:
    using Callback = std::function<void(Command&)>;

public:
    explicit Command(Node* factory_n, Node* ctx_n = nullptr, Node* in_n = nullptr, Node* out_n = nullptr);
    ~Command();

    std::string help() const { return node()->get("help").toString(); } // global

    Node* contextNode() const;
    Node* inputNode() const;
    Node* outputNode() const;
    void setContextNodes(Node* ctx_n, Node* in_n, Node* out_n);

    bool valid() const;

    Command& run();

    Loop* loop() const;
    void setLoop(Loop* l);

    Result result() const;

    void call(Callback cb, Loop* cb_loop = nullptr) const;

public:
    template<typename SchemaS = schema::VarS, typename... Args>
    bool input(Args&&... args) { return schema::importAs<SchemaS>(inputNode(), std::forward<Args>(args)...); }
    template<typename SchemaS = schema::VarS, typename... Args>
    bool input(const std::string& path, Args&&... args) { return schema::importAs<SchemaS>(inputNode()->at(path), std::forward<Args>(args)...); }

private:
    VE_DECLARE_SHARED_PRIVATE
};

namespace command {

VE_API Factory& factory();

// Register any callable: plain functions/lambdas are adapted to Proc via
// convert::parse(F, Proc&) above (Proc-shaped callables pass straight through).
template<typename F>
inline auto reg(Factory& f, const std::string& key, F&& fn)
{
    Proc p;
    convert::parse(std::forward<F>(fn), p);
    return f.reg(key, Var::callable(std::move(p)));
}
template<typename F>
inline auto reg(const std::string& key, F&& fn)
{
    return reg(factory(), key, std::forward<F>(fn));
}

inline Command create(const Factory& factory, const std::string& key, Node* ctx = nullptr, Node* in = nullptr, Node* out = nullptr, char sep = VE_FACTORY_KEY_SEP)
{ return Command(factory.node(key, sep), ctx, in, out); }
inline Command create(const std::string& key, Node* ctx = nullptr, Node* in = nullptr, Node* out = nullptr, char sep = VE_FACTORY_KEY_SEP)
{ return create(factory(), key, ctx, in, out, sep); }

} // namespace command

} // namespace ve
