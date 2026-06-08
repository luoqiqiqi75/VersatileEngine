// pipeline.cpp - transient command/proc execution graph

#include "ve/core/pipeline.h"

#include <algorithm>
#include <atomic>
#include <thread>

namespace ve {

namespace {

struct Step
{
    Node* node = nullptr;
    Command command;
    Node* in = nullptr;
    Node* out = nullptr;

    Step(Node* n, Command c) : node(n), command(std::move(c)) {}
};

void setPipelineError(Node* ctx, Pipeline::Error code, const std::string& message)
{
    if (!ctx) return;
    Node* error = ctx->at("_pipe/error");
    error->set("code", static_cast<int64_t>(code));
    error->set("message", message);
}

void appendLoop(std::vector<Loop*>& loops, Loop* loop)
{
    if (loop && std::find(loops.begin(), loops.end(), loop) == loops.end()) {
        loops.push_back(loop);
    }
}

} // namespace

struct Pipeline::Private
{
    std::vector<Step> steps;
    State state = IDLE;
    Node* ctx = nullptr;
    std::atomic<bool> completed{false};
    Callback callback;
    Result last;

    ~Private()
    {
        delete ctx;
    }

    Node* commandRoot() const { return ctx->at("_pipe/commands"); }

    Node* stageRoot() const { return ctx->at("_pipe/stage"); }

    Node* appendCommandNode() const
    {
        return commandRoot()->append();
    }

    Node* factoryNode(Node* commandNode) const
    {
        return commandNode->at("factory");
    }

    void wireCommands()
    {
        Node* prevOut = nullptr;
        Node* stages = stageRoot();
        const int last = static_cast<int>(steps.size()) - 1;
        for (int i = 0; i <= last; ++i) {
            Step& step = steps[i];
            if (!step.in) {
                step.in = prevOut ? prevOut : ctx->at("input");
            }
            if (!step.out) {
                Node* nextIn = (i < last) ? steps[i + 1].in : nullptr;
                step.out = nextIn ? nextIn : ((i == last) ? ctx->at("output") : stages->at(i, false));
            }
            step.command.setContext(ctx);
            step.command.setInput(step.in);
            step.command.setOutput(step.out);
            if (step.node) {
                step.node->at("in")->set(Var::ptr(step.in));
                step.node->at("out")->set(Var::ptr(step.out));
            }
            prevOut = step.out;
        }
    }
};

Pipeline::Pipeline(const std::string& name)
    : Object(name)
{
    _p->ctx = new Node("_ctx");
}

Pipeline::Pipeline(const std::string& name, const Node* initialCtx)
    : Object(name)
{
    _p->ctx = new Node("_ctx");
    if (initialCtx) {
        _p->ctx->copy(initialCtx, true, true, true);
        _p->ctx->erase("_pipe");
    }
}

Pipeline::~Pipeline() = default;

Pipeline::Handle Pipeline::addProc(Proc proc, const std::string& inPath,
                                   const std::string& outPath, Loop* loop)
{
    Node* commandNode = _p->appendCommandNode();
    Node* factoryNode = _p->factoryNode(commandNode);
    factoryNode->set(Var::callable(std::move(proc)));
    if (loop) factoryNode->at("loop")->set(Var::ptr(loop));

    Step step(commandNode, Command(factoryNode, _p->ctx,
        inPath.empty() ? nullptr : _p->ctx->at(inPath),
        outPath.empty() ? nullptr : _p->ctx->at(outPath)));
    step.in = step.command.input();
    step.out = step.command.output();
    int slot = static_cast<int>(_p->steps.size());
    _p->steps.push_back(std::move(step));
    return {slot};
}

Pipeline::Handle Pipeline::addCommand(Command command, Loop* loop)
{
    Node* commandNode = _p->appendCommandNode();
    Node* factoryNode = _p->factoryNode(commandNode);
    if (command.node()) {
        factoryNode->copy(command.node(), true, true, true);
    }

    Loop* commandLoop = loop ? loop : command.loop();
    if (commandLoop) factoryNode->at("loop")->set(Var::ptr(commandLoop));

    Step step(commandNode, Command(factoryNode, _p->ctx, command.input(), command.output()));
    step.in = step.command.input();
    step.out = step.command.output();
    int slot = static_cast<int>(_p->steps.size());
    _p->steps.push_back(std::move(step));
    return {slot};
}

Pipeline::Handle Pipeline::addCtxProc(Proc proc, Loop* loop)
{
    Node* commandNode = _p->appendCommandNode();
    Node* factoryNode = _p->factoryNode(commandNode);
    factoryNode->set(Var::callable(std::move(proc)));
    if (loop) factoryNode->at("loop")->set(Var::ptr(loop));

    Step step(commandNode, Command(factoryNode, _p->ctx, _p->ctx, _p->ctx));
    step.in = step.command.input();
    step.out = step.command.output();
    int slot = static_cast<int>(_p->steps.size());
    _p->steps.push_back(std::move(step));
    return {slot};
}

Pipeline::Handle Pipeline::addLinearProc(Proc proc, Loop* loop)
{
    return addProc(std::move(proc), {}, {}, loop);
}

void Pipeline::start()
{
    if (!_p->ctx) {
        _p->last = Result::fail("pipeline ctx is null");
        complete(ERRORED);
        return;
    }

    _p->completed.store(false, std::memory_order_release);
    _p->state = RUNNING;
    _p->last = Result::ok();

    if (_p->steps.empty()) {
        complete(DONE);
        return;
    }

    _p->wireCommands();
    dispatch(0);
}

void Pipeline::dispatch(int slot)
{
    if (slot >= static_cast<int>(_p->steps.size())) {
        complete(DONE);
        return;
    }

    Step& step = _p->steps[slot];
    step.command.call([this, slot](Command& cmd) mutable {
        if (_p->state != RUNNING) {
            return;
        }

        _p->last = cmd.result();
        if (_p->last.isError()) {
            if (!cmd.valid()) {
                setPipelineError(_p->ctx, ERR_NO_STEP_PROC, _p->last.message);
            }
            complete(ERRORED);
            return;
        }
        if (_p->last.isAccepted()) {
            complete(DONE);
            return;
        }

        dispatch(slot + 1);
    });
}

void Pipeline::complete(State state)
{
    _p->state = state;

    if (_p->state == DONE) {
        trigger<DONE_SIGNAL>(Var::ptr(this));
    } else if (_p->state == ERRORED) {
        trigger<ERROR_SIGNAL>(Var::ptr(this));
    }

    _p->completed.store(true, std::memory_order_release);
    Callback callback = std::move(_p->callback);
    if (callback) {
        callback(*this);
    }
}

void Pipeline::cancel()
{
    if (_p->state != RUNNING) return;
    setPipelineError(_p->ctx, ERR_CANCELLED, "cancelled");
    _p->last = Result::fail("cancelled");
    complete(ERRORED);
}

void Pipeline::wait()
{
    std::vector<Loop*> loops;
    loops.reserve(_p->steps.size());
    for (const auto& step : _p->steps) {
        appendLoop(loops, step.command.loop());
    }

    while (!_p->completed.load(std::memory_order_acquire)) {
        size_t processed = 0;
        for (auto* loop : loops) {
            processed += loop->processEvents();
        }
        if (processed == 0) {
            std::this_thread::yield();
        }
    }
}

void Pipeline::onFinished(Callback cb)
{
    _p->callback = std::move(cb);
}

Node* Pipeline::context() const
{
    return _p->ctx;
}

Pipeline::State Pipeline::state() const
{
    return _p->state;
}

const Result& Pipeline::lastResult() const
{
    return _p->last;
}

Node* Pipeline::outputOf(Handle handle) const
{
    if (handle.slot < 0 || handle.slot >= static_cast<int>(_p->steps.size())) {
        return nullptr;
    }
    return _p->steps[handle.slot].out;
}

namespace pipeline {

void start(Pipeline& p, Pipeline::Callback cb)
{
    if (cb) {
        p.onFinished(std::move(cb));
    }
    p.start();
}

void start(Pipeline* p, Pipeline::Callback cb)
{
    if (!p) return;
    p->onFinished([cb = std::move(cb)](Pipeline& pipe) mutable {
        if (cb) {
            cb(pipe);
        }
        delete &pipe;
    });
    p->start();
}

} // namespace pipeline

} // namespace ve
