// pipeline.cpp - transient command/proc execution graph

#include "ve/core/pipeline.h"

#include <atomic>
#include <chrono>
#include <future>

#include "ve/core/log.h"

namespace ve {

struct Pipeline::Private
{
    Node* ctx_n = nullptr;
    Node* in_n = nullptr;
    Node* out_n = nullptr;

    Node* pipe_n = nullptr;

    // IDLE -> RUNNING (async) -> STOPPING (error/cancel) -> IDLE (after cleanup)
    enum State : int { IDLE, RUNNING, STOPPING };
    std::atomic<int> state = IDLE;

    // per run; fulfilled after the state returns to IDLE
    std::promise<void> finish;
    std::shared_future<void> finished;

    List<Command> commands;

    Result result;

public:
    Private() { ctx_n = new Node; }
    ~Private() { delete ctx_n; }

    void execute(std::shared_ptr<Private> self);
};

void Pipeline::Private::execute(std::shared_ptr<Private> self)
{
    if (state == RUNNING && !commands.empty()) { // continue
        const Command exec_c = commands.front();
        commands.pop_front();
        exec_c.call([self] (Command& c) {
            Result r = c.result();
            int s = RUNNING;
            if (!r.isSuccess() && self->state.compare_exchange_strong(s, STOPPING))
                self->result = std::move(r); // first stop reason wins
            self->execute(self);
        });
    } else { // stop
        pipe_n->trigger<FINISHED>();
        pipe_n->disconnectAll(); // must reconnect if restart
        commands.clear();
        auto done = std::move(finish);
        state = IDLE;
        done.set_value();
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

Command* Pipeline::add(Command command)
{
    if (_p->state != Private::IDLE) {
        veLogW << "<ve::pipeline> commands cannot change while running";
        return nullptr;
    }
    command.setContextNodes(contextNode(), command.inputNode(), command.outputNode());
    _p->commands.push_back(command);
    return &_p->commands.back();
}

Command* Pipeline::addProc(Proc proc, Loop* loop)
{
    if (_p->state != Private::IDLE) {
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
    int s = Private::RUNNING;
    if (_p->state.compare_exchange_strong(s, Private::STOPPING))
        _p->result.setCode(CANCELED);
}

void Pipeline::async() const
{
    int s = Private::IDLE;
    if (!_p->state.compare_exchange_strong(s, Private::RUNNING)) {
        veLogE << "<ve::pipeline> already running";
        return;
    }
    _p->finish = std::promise<void>();
    _p->finished = _p->finish.get_future().share();
    _p->result = Result::ok();
    _p->pipe_n->trigger<STARTED>();
    _p->execute(_p);
}

void Pipeline::sync(Loop* cur_l) const
{
    if (!cur_l) cur_l = loop::current();   // the loop driving this thread, if any
    async();
    const auto finished = _p->finished;
    if (!cur_l) return finished.wait();
    // keep pumping so steps bound to this loop can run; sleep only when idle
    while (finished.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        if (!cur_l->processEvents()) finished.wait_for(std::chrono::milliseconds(1));
    }
}

namespace pipeline {


} // namespace pipeline

} // namespace ve
