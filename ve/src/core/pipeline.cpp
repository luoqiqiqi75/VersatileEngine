// pipeline.cpp - transient command/proc execution graph

#include "ve/core/pipeline.h"

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

} // namespace

struct Pipeline::Private
{
    std::vector<Step> steps;
    std::atomic<State> state{IDLE};   // written on the finishing thread, read by sync()
    Node* ctx = nullptr;
    Callback callback;
    Result last;
    Loop* driver = nullptr;   // loop pumped by sync() / posted to by async()

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

Pipeline::Pipeline(const Node* initialCtx)
    : _p(std::make_shared<Private>())
{
    _p->ctx = new Node("_ctx");
    if (initialCtx) {
        _p->ctx->copy(initialCtx, true, true, true);
        _p->ctx->erase("_pipe");
    }
}

Pipeline::~Pipeline() = default;

void Pipeline::addProc(Proc proc, const std::string& inPath,
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
    _p->steps.push_back(std::move(step));
}

void Pipeline::addCommand(Command command, Loop* loop)
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
    _p->steps.push_back(std::move(step));
}

void Pipeline::addCtxProc(Proc proc, Loop* loop)
{
    Node* commandNode = _p->appendCommandNode();
    Node* factoryNode = _p->factoryNode(commandNode);
    factoryNode->set(Var::callable(std::move(proc)));
    if (loop) factoryNode->at("loop")->set(Var::ptr(loop));

    Step step(commandNode, Command(factoryNode, _p->ctx, _p->ctx, _p->ctx));
    step.in = step.command.input();
    step.out = step.command.output();
    _p->steps.push_back(std::move(step));
}

void Pipeline::addLinearProc(Proc proc, Loop* loop)
{
    addProc(std::move(proc), {}, {}, loop);
}

void Pipeline::onFinished(Callback cb)
{
    _p->callback = std::move(cb);
}

void Pipeline::run(Loop* driver, bool deferFirst)
{
    _p->driver = driver;

    if (!_p->ctx) {
        _p->last = Result::fail("pipeline ctx is null");
        complete(ERRORED);
        return;
    }

    _p->state.store(RUNNING);
    _p->last = Result::ok();

    if (_p->steps.empty()) {
        complete(DONE);
        return;
    }

    _p->wireCommands();

    if (deferFirst && driver) {
        Pipeline self = *this;                       // keep the graph alive on the loop
        driver->post([self]() mutable { self.dispatch(0); });
    } else {
        dispatch(0);
    }
}

void Pipeline::sync(Loop* driver)
{
    if (!driver) driver = loop::current();   // default: the loop driving this thread
    run(driver, /*deferFirst=*/false);

    // Loop-less graphs are already terminal here. Otherwise, pump the driver
    // (keeping the current loop responsive) until an off-thread step finishes.
    while (_p->state.load() == RUNNING) {
        if (driver) {
            if (driver->processEvents() == 0) std::this_thread::yield();
        } else {
            std::this_thread::yield();
        }
    }
}

void Pipeline::dispatch(int slot)
{
    if (slot >= static_cast<int>(_p->steps.size())) {
        complete(DONE);
        return;
    }

    Step& step = _p->steps[slot];
    // Share the graph into the continuation so it outlives the calling handle
    // across loop-bound (asynchronous) steps.
    Pipeline self = *this;
    step.command.call([self, slot](Command& cmd) mutable {
        if (self._p->state.load() != RUNNING) {
            return;
        }

        self._p->last = cmd.result();
        if (self._p->last.isError()) {
            if (!cmd.valid()) {
                setPipelineError(self._p->ctx, ERR_NO_STEP_PROC, self._p->last.message);
            }
            self.complete(ERRORED);
            return;
        }
        if (self._p->last.isAccepted()) {
            self.complete(DONE);
            return;
        }

        self.dispatch(slot + 1);
    });
}

void Pipeline::complete(State state)
{
    _p->state.store(state);
    Callback callback = std::move(_p->callback);
    _p->callback = {};
    if (callback) {
        callback(*this);
    }
    // Wake a sync() pump that may be waiting on the driver loop.
    if (_p->driver) {
        _p->driver->post([] {});
    }
}

void Pipeline::cancel()
{
    if (_p->state.load() != RUNNING) return;
    setPipelineError(_p->ctx, ERR_CANCELLED, "cancelled");
    _p->last = Result::fail("cancelled");
    complete(ERRORED);
}

Node* Pipeline::context() const
{
    return _p->ctx;
}

Pipeline::State Pipeline::state() const
{
    return _p->state.load();
}

const Result& Pipeline::lastResult() const
{
    return _p->last;
}

namespace pipeline {

void async(Pipeline&& p, Loop* driver, Pipeline::Callback cb)
{
    Pipeline pipe = std::move(p);
    if (cb) {
        pipe.onFinished(std::move(cb));
    }
    // Default driver: the loop driving this thread, else the main loop (async must
    // post somewhere that will actually run the first dispatch).
    if (!driver) driver = loop::current();
    if (!driver) driver = loop::main();
    pipe.run(driver, /*deferFirst=*/true);
}

} // namespace pipeline

} // namespace ve
