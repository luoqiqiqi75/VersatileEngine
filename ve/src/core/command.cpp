// command.cpp - Command instance and command factory helpers

#include "ve/core/command.h"

#include <algorithm>
#include <exception>

namespace ve {

struct Command::Private
{
    Loop* l = nullptr;

    Node* ctx_n = nullptr;
    Node* in_n = nullptr;
    Node* out_n = nullptr;

    Node internal_ctx_n;

    Result r;
};

Command::Command(Node* factory_n, Node* ctx_n, Node* in_n, Node* out_n) : NodeRef(factory_n), _p(std::make_shared<Private>())
{
    _p->r = Result::fail(-0xff01, "command invalid");
    if (factory_n) {
        _p->l = factory_n->get("loop").as<Loop*>();
    }
    setContextNodes(ctx_n, in_n, out_n);
}

Command::~Command() = default;

Node* Command::contextNode() const { return _p->ctx_n; }
Node* Command::inputNode() const { return _p->in_n; }
Node* Command::outputNode() const { return _p->out_n; }

Command& Command::setContextNodes(Node* ctx_n, Node* in_n, Node* out_n)
{
    _p->ctx_n = ctx_n ? ctx_n : &_p->internal_ctx_n;
    _p->in_n = in_n ? in_n : _p->ctx_n->at("in");
    _p->out_n = out_n ? out_n : _p->ctx_n->at("out");
    return *this;
}

bool Command::valid() const
{
    return node() != nullptr && node()->get().isCallable()
        && _p->ctx_n != nullptr && _p->in_n != nullptr && _p->out_n != nullptr;
}

Command& Command::run()
{
    if (!valid()) { _p->r = Result::fail("command invalid"); return *this; }
    try {
        _p->r = node()->get().invoke(_p->ctx_n, _p->in_n, _p->out_n).as<Result>();
    } catch (const std::exception& e) {
        _p->r = Result::fail(e.what());
    } catch (...) {
        _p->r = Result::fail("unknown exception");
    }
    return *this;
}

Loop* Command::loop() const { return _p->l; }
void Command::setLoop(Loop* l) { _p->l = l; }

Result Command::result() const { return _p->r; }

void Command::call(Callback cb, Loop* cb_loop) const
{
    auto task = [c = *this, cb, cb_l = cb_loop]() mutable {
        c.run(); // proc exec in l
        if (cb_l) {
            cb_l->post([cb, c]() mutable { if (cb) cb(c); }); // callback exec in loop
        } else if (cb) {
            cb(c); // callback exec in l
        }
    };
    _p->l ? _p->l->post(task) : task();
}

namespace command {

Factory& factory()
{
    return factory::at("cmd");
}

} // namespace command

} // namespace ve
