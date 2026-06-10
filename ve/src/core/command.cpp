// command.cpp - Command instance and command factory helpers

#include "ve/core/command.h"

#include <algorithm>
#include <exception>

namespace ve {

struct Command::Private
{
    Loop* l = nullptr;

    Node* declare_n = nullptr;

    Node* ctx_n = nullptr;
    Node* in_n = nullptr;
    Node* out_n = nullptr;

    Node internal_ctx_n;

    Result r;
};

Command::Command(Node* factory_n, Node* ctx, Node* in, Node* out) : NodeRef(factory_n), _p(std::make_shared<Private>())
{
    _p->l = factory_n->get("loop").as<Loop*>();
    _p->declare_n = factory_n->find("declare");
    _p->r = Result::fail(-0x10, "command invalid");

    setContext(ctx);
    setInput(in);
    setOutput(out);
}

Command::~Command() = default;

Node* Command::context() const { return _p->ctx_n; }
void Command::setContext(Node* ctx_n) { _p->ctx_n = ctx_n ? ctx_n : &_p->internal_ctx_n; }

Node* Command::input() const { return _p->in_n; }
void Command::setInput(Node* in_n) { _p->in_n = in_n ? in_n : _p->ctx_n->at("in"); _p->in_n->setShadow(_p->declare_n); }

Node* Command::output() const { return _p->out_n; }
void Command::setOutput(Node* out_n) { _p->out_n = out_n ? out_n : _p->ctx_n->at("out"); }

bool Command::valid() const
{
    return node() != nullptr && node()->get().isCallable()
        && _p->ctx_n != nullptr && _p->in_n != nullptr && _p->out_n != nullptr;
}

Command& Command::run()
{
    if (!valid()) _p->r = Result::fail("command invalid");
    _p->r = node()->get().invoke(_p->ctx_n, _p->in_n, _p->out_n).as<Result>();
    return *this;
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
