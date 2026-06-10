// pipeline.cpp - transient command/proc execution graph

#include "ve/core/pipeline.h"

#include <atomic>
#include <thread>

#include "ve/core/log.h"

namespace ve {

struct Pipeline::Private
{
    Object* obj = nullptr;

    Node* ctx_n = new Node;
    Node* in_n = nullptr;
    Node* out_n = nullptr;

    Node* pipe_n = nullptr;

    std::atomic_bool running = false;

    List<Command> commands;

    Result result = Result::ok();

public:
    void execute(std::shared_ptr<Private> self);
};

void Pipeline::Private::execute(std::shared_ptr<Private> self)
{
    if (result.isSuccess()) { // will continue
        if (commands.empty()) { // finished
            obj->trigger<FINISHED>();
            running = false;
        } else if (!running) { // canceled
            obj->trigger<CANCELLED>();
            commands.clear();
        } else { // execute
            commands.front().call([self] (Command& c) {
                self->result = c.result();
                self->commands.pop_front();
                std::swap(self->in_n, self->out_n);
                self->execute(self);
            });
        }
    } else { // will stop
        if (result.isError()) obj->trigger<ERRORED>(); // errored, self handle accepted
        running = false;
        commands.clear();
    }
}

Pipeline::Pipeline(const Node* ctx) : _p(std::make_shared<Private>())
{
    _p->ctx_n->copy(ctx);

    _p->ctx_n->remove("_pipe");
    _p->pipe_n = _p->ctx_n->at("_pipe");

    _p->in_n = _p->pipe_n->at("i");
    _p->out_n = _p->pipe_n->at("o");

    _p->obj = _p->pipe_n;
}

Pipeline::~Pipeline()
{
    delete _p->ctx_n;
}

Node* Pipeline::contextNode() const { return _p->ctx_n; }
Node* Pipeline::inputNode() const { return _p->in_n; }
Node* Pipeline::outputNode() const { return _p->out_n; }

const Result& Pipeline::result() const { return _p->result; }

Pipeline::Handle Pipeline::add(Command command)
{
    if (_p->running) {
        veLogW << "<ve::pipeline> commands cannot change while running";
        return nullptr;
    }
    _p->commands.push_back(std::move(command));
    return command.node();
}

Pipeline::Handle Pipeline::addProc(Proc proc, Loop* loop)
{
    if (_p->running) {
        veLogW << "<ve::pipeline> commands cannot change while running";
        return nullptr;
    }
    Node* fac_n = _p->pipe_n->at("proc")->append();
    fac_n->set(Var::callable(std::move(proc)));
    if (loop) fac_n->set("loop", Var::ptr(loop));
    return add(Command(fac_n));
}

template<Pipeline::StateSignal SS> void Pipeline::on(Object* observer, Callback cb, Loop* loop)
{
    _p->obj->connect<SS>(observer, [self = *this, cb] {
        Pipeline p = std::move(self);
        cb(p);
    }, loop);
}

void Pipeline::cancel()
{
    _p->running = false;
}

void Pipeline::async()
{
    if (_p->running) {
        veLogE << "<ve::pipeline> already running";
        return;
    }
    _p->running = true;
    _p->obj->trigger<PREPARE>();
    _p->obj->trigger<STARTED>();
    _p->execute(_p);
}

void Pipeline::sync(Loop* cur_l)
{
    async();
    while (_p->running) {
        cur_l->processEvents();
    }
}

namespace pipeline {

// void async(Pipeline&& p, Loop* driver, Pipeline::Callback cb)
// {
//     Pipeline pipe = std::move(p);
//     if (cb) {
//         pipe.onFinished(std::move(cb));
//     }
//     // Default driver: the loop driving this thread, else the main loop (async must
//     // post somewhere that will actually run the first dispatch).
//     if (!driver) driver = loop::current();
//     if (!driver) driver = loop::main();
//     pipe.run(driver, /*deferFirst=*/true);
// }

} // namespace pipeline

} // namespace ve
