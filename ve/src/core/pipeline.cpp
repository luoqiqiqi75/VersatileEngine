// pipeline.cpp - transient command/proc execution graph

#include "ve/core/pipeline.h"

#include <atomic>
#include <thread>

#include "ve/core/log.h"

namespace ve {

struct Pipeline::Private
{
    Node* ctx_n = nullptr;
    Node* in_n = nullptr;
    Node* out_n = nullptr;

    Node* pipe_n = nullptr;

    std::atomic_bool running = false;

    List<Command> commands;

    Result result;

public:
    Private() { ctx_n = new Node; }
    ~Private() { delete ctx_n; }

    void execute(std::shared_ptr<Private> self);
};

void Pipeline::Private::execute(std::shared_ptr<Private> self)
{
    if (running && result.isSuccess() && !commands.empty()) { // continue
        Command exec_c = commands.front();
        exec_c.setContextNodes(ctx_n, in_n, out_n);
        commands.pop_front();
        if (!commands.empty()) std::swap(in_n, out_n);
        exec_c.call([self] (Command& c) {
            Result r = c.result();
            if (self->running && !r.isSuccess()) {
                self->result = std::move(r); // save result if still running
                self->running = false;
            }
            self->execute(self);
        });
    } else { // stop
        pipe_n->trigger<FINISHED>();
        running = false;
        pipe_n->disconnectAll(); // must reconnect if restart
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
}

Pipeline::~Pipeline() = default;

Node* Pipeline::contextNode() const { return _p->ctx_n; }
Node* Pipeline::inputNode() const { return _p->in_n; }
Node* Pipeline::outputNode() const { return _p->out_n; }

Object* Pipeline::object() const { return _p->pipe_n; }

const Result& Pipeline::result() const { return _p->result; }

Pipeline::Handle Pipeline::add(Command command)
{
    if (_p->running) {
        veLogW << "<ve::pipeline> commands cannot change while running";
        return nullptr;
    }
    _p->commands.push_back(command);
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

void Pipeline::onStarted(Object* observer, Callback cb, Loop* loop) { on<STARTED>(observer, std::move(cb), loop); }
void Pipeline::onFinished(Object* observer, Callback cb, Loop* loop) { on<FINISHED>(observer, std::move(cb), loop); }

void Pipeline::cancel() const
{
    _p->result.setCode(CANCELED);
    _p->running = false;
}

void Pipeline::async()
{
    if (_p->running) {
        veLogE << "<ve::pipeline> already running";
        return;
    }
    _p->running = true;
    _p->result = Result::ok();
    _p->pipe_n->trigger<STARTED>();
    _p->execute(_p);
}

void Pipeline::sync(Loop* cur_l)
{
    if (!cur_l) cur_l = loop::current();   // the loop driving this thread, if any
    async();
    while (_p->running) {
        if (cur_l) cur_l->processEvents(); // keep pumping so loop-bound steps can hop back
        std::this_thread::yield();
    }
}

namespace pipeline {


} // namespace pipeline

} // namespace ve
