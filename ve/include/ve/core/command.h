// command.h - Proc, Command instance and command factory helpers
#pragma once

#include "factory.h"
#include "loop.h"
#include "node.h"
#include "var.h"

#include <cstdint>
#include <functional>
#include <string>
#include <utility>

namespace ve {

struct Result
{
    enum Code : int {
        SUCCESS         = 0,
        FAILED          = -1,
        ACCEPTED        = 1
    };

    int         code = FAILED;
    std::string message;

    bool isSuccess() const { return code == SUCCESS; }
    bool isError() const { return code < 0; }
    bool isAccepted() const { return code > 0; }
    explicit operator bool() const { return isSuccess(); }

    static Result ok() { return {SUCCESS}; }
    static Result fail(std::string message = {}) { return {FAILED, std::move(message)}; }
    static Result fail(int c, std::string message = {}) { return {c, std::move(message)}; }
    static Result accept(std::string message = {}) { return {ACCEPTED, std::move(message)}; }
    static Result accept(int c, std::string message = {}) { return {c < 0 ? -c : c, std::move(message)}; }

    template<typename E> static std::enable_if_t<std::is_enum_v<E>, Result> fail(E ec, std::string message = {})
    { return fail(static_cast<int>(ec), std::move(message)); }
};

using Proc = std::function<Result(Node* ctx, Node* in, Node* out)>;

namespace convert
{

// always ok

template<> inline bool parse(std::function<void()> f, Proc& p)
{ p = [f = std::move(f)] (Node*, Node* in, Node* out) -> Result { f(); return Result::ok(); }; return true; }

template<> inline bool parse(std::function<Var()> f, Proc& p)
{ p = [f = std::move(f)] (Node*, Node* in, Node* out) -> Result { out->set(f()); return Result::ok(); }; return true; }

template<> inline bool parse(std::function<void(const Var& v)> f, Proc& p)
{ p = [f = std::move(f)] (Node*, Node* in, Node* out) -> Result { f(in->get()); return Result::ok(); }; return true; }

template<> inline bool parse(std::function<void(const Var& v1, const Var& v2)> f, Proc& p)
{ p = [f = std::move(f)] (Node*, Node* in, Node* out) -> Result { f(in->get(0), in->get(1)); return Result::ok(); }; return true; }

template<> inline bool parse(std::function<Var(const Var& v)> f, Proc& p)
{ p = [f = std::move(f)] (Node*, Node* in, Node* out) -> Result { out->set(f(in->get())); return Result::ok(); }; return true; }

template<> inline bool parse(std::function<Var(const Var& v1, const Var& v2)> f, Proc& p)
{ p = [f = std::move(f)] (Node*, Node* in, Node* out) -> Result { out->set(f(in->get(0), in->get(1))); return Result::ok(); }; return true; }

// use result

template<> inline bool parse(std::function<Result(const Var& v_in)> f, Proc& p)
{ p = [f = std::move(f)] (Node*, Node* in, Node* out) -> Result {
    return f(schema::exportAs<schema::VarS>(in));
}; return true; }

template<> inline bool parse(std::function<Result(const Var& v_in, Var& v_out)> f, Proc& p)
{ p = [f = std::move(f)] (Node*, Node* in, Node* out) -> Result {
    Var v;
    Result r = f(schema::exportAs<schema::VarS>(in), v);
    schema::importAs<schema::VarS>(out, v);
    return r;
}; return true; }

}

class VE_API Command : public NodeRef
{
public:
    using Callback = std::function<void(Command&)>;

public:
    explicit Command(Node* factory_n, Node* ctx = nullptr, Node* in = nullptr, Node* out = nullptr);
    ~Command();

    std::string help() const { return node()->get("help").toString(); } // global

    Node* context() const;
    void setContext(Node* ctx_n);
    Node* input() const;
    void setInput(Node* in_n);
    Node* output() const;
    void setOutput(Node* out_n);

    bool valid() const;

    Command& run();

    Loop* loop() const;
    void setLoop(Loop* l);

    Result result() const;

    void call(Callback cb, Loop* loop = nullptr) const;

private:
    VE_DECLARE_SHARED_PRIVATE
};

namespace command {

VE_API Factory& factory();

inline auto reg(Factory& f, const std::string& key, Proc proc)
{
    return f.reg(key, Var::callable(std::move(proc)));
}

inline Command create(const Factory& factory, const std::string& key, Node* ctx, Node* in, Node* out, char sep = VE_FACTORY_KEY_SEP)
{ return Command(factory.node(key, sep), ctx, in, out); }
inline Command create(const std::string& key, Node* ctx, Node* in, Node* out, char sep = VE_FACTORY_KEY_SEP)
{ return create(factory(), key, ctx, in, out, sep); }

} // namespace command

} // namespace ve
