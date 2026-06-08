// command.cpp - Command instance and command factory helpers

#include "ve/core/command.h"

#include <algorithm>
#include <exception>

namespace ve {

struct Command::Private
{
    Loop* l = nullptr;

    Node* ctx = nullptr;
    Node* in = nullptr;
    Node* out = nullptr;

    Result r;
};

Command::Command(Node* factory_n) : NodeRef(factory_n), _p(std::make_shared<Private>())
{
    _p->l = factory_n->get("loop").as<Loop*>();
    _p->r = Result::fail(-0x10, "command invalid");
}

Command::Command(Node* factory_n, Node* ctx, Node* in, Node* out) : Command(factory_n)
{
    _p->ctx = ctx;
    _p->in = in;
    _p->out = out;
}

Command::Command(Node* factory_n, Node* ctx) : Command(factory_n)
{
    _p->ctx = ctx;
    _p->in = ctx->at("in");
    _p->out = ctx->at("out");
}

Command::~Command() = default;

Node* Command::context() const { return _p->ctx; }
void Command::setContext(Node* ctx_n) { _p->ctx = ctx_n; }

Node* Command::input() const { return _p->in; }
void Command::setInput(Node* in_n) { _p->in = in_n; }

Node* Command::output() const { return _p->out; }
void Command::setOutput(Node* out_n) { _p->out = out_n; }

bool Command::valid() const
{
    return node() != nullptr && node()->get().isCallable()
        && _p->ctx != nullptr && _p->in != nullptr && _p->out != nullptr;
}

Result Command::run() const
{
    if (!valid()) return Result::fail("command invalid");
    return node()->get().invoke(_p->ctx, _p->in, _p->out).as<Result>();
}

Loop* Command::loop() const { return _p->l; }
void Command::setLoop(Loop* l) { _p->l = l; }

Result Command::result() const { return _p->r; }

void Command::call(Callback cb, Loop* loop) const
{
    auto task = [c = *this, cb, cb_l = loop]() mutable {
        try {
            c._p->r = c.run(); // proc exec in l
        } catch (const std::exception& e) {
            c._p->r = Result::fail(e.what());
        } catch (...) {
            c._p->r = Result::fail("unknown exception");
        }
        if (cb_l) {
            cb_l->post([cb, c]() mutable { if (cb) cb(c); }); // callback exec in loop
        } else if (cb) {
            cb(c); // callback exec in l
        }
    };
    if (_p->l) {
        _p->l->post(task);
    } else {
        task();
    }
}

namespace command {

Factory& factory()
{
    return factory::at("cmd");
}

} // namespace command

} // namespace ve
