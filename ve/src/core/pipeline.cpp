// pipeline.cpp - transient command/proc execution graph

#include "ve/core/pipeline.h"

#include <algorithm>
#include <atomic>
#include <thread>

namespace ve {

namespace {

enum class Wiring : uint8_t { Path, Ctx, Linear };

struct Step
{
    Proc proc;
    Command command;
    Wiring wiring = Wiring::Path;
    std::string inPath;
    std::string outPath;
    Loop* loop = nullptr;
    Node* in = nullptr;
    Node* out = nullptr;
};

void setPipelineError(Node* ctx, Pipeline::Error code, const std::string& message)
{
    if (!ctx) return;
    Node* error = ctx->at("_pipe/error");
    error->set("code", static_cast<int64_t>(code));
    error->set("message", message);
}

Loop* resolveLoop(const Step& step)
{
    if (step.loop) return step.loop;
    return step.command.loop();
}

} // namespace

struct Pipeline::Private
{
    std::vector<Step> steps;
    State state = IDLE;
    Node* ctx = nullptr;
    std::atomic<bool> completed{false};
    Handler handler;
    Result last;

    ~Private()
    {
        delete ctx;
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
    }
}

Pipeline::~Pipeline() = default;

Pipeline::Handle Pipeline::addProc(Proc proc, const std::string& inPath,
                                   const std::string& outPath, Loop* loop)
{
    Step step;
    step.proc = std::move(proc);
    step.inPath = inPath;
    step.outPath = outPath;
    step.loop = loop;
    step.wiring = Wiring::Path;
    int slot = static_cast<int>(_p->steps.size());
    _p->steps.push_back(std::move(step));
    return {slot};
}

Pipeline::Handle Pipeline::addCommand(Command command, Loop* loop)
{
    Step step;
    step.command = std::move(command);
    step.loop = loop;
    step.wiring = Wiring::Path;
    int slot = static_cast<int>(_p->steps.size());
    _p->steps.push_back(std::move(step));
    return {slot};
}

Pipeline::Handle Pipeline::addCtxProc(Proc proc, Loop* loop)
{
    Step step;
    step.proc = std::move(proc);
    step.loop = loop;
    step.wiring = Wiring::Ctx;
    int slot = static_cast<int>(_p->steps.size());
    _p->steps.push_back(std::move(step));
    return {slot};
}

Pipeline::Handle Pipeline::addLinearProc(Proc proc, Loop* loop)
{
    Step step;
    step.proc = std::move(proc);
    step.loop = loop;
    step.wiring = Wiring::Linear;
    int slot = static_cast<int>(_p->steps.size());
    _p->steps.push_back(std::move(step));
    return {slot};
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

    dispatch(0);
}

void Pipeline::dispatch(int slot)
{
    if (slot >= static_cast<int>(_p->steps.size())) {
        complete(DONE);
        return;
    }

    Step& step = _p->steps[slot];
    if (step.proc) {
        switch (step.wiring) {
        case Wiring::Path:
            step.in = _p->ctx->at(step.inPath);
            step.out = _p->ctx->at(step.outPath);
            break;
        case Wiring::Ctx:
            step.in = _p->ctx;
            step.out = _p->ctx;
            break;
        case Wiring::Linear:
            step.in = slot == 0 ? _p->ctx->at("input") : _p->ctx->at("_pipe/stage")->at(slot - 1, false);
            step.out = slot + 1 == static_cast<int>(_p->steps.size())
                ? _p->ctx->at("output")
                : _p->ctx->at("_pipe/stage")->at(slot, false);
            break;
        }
    } else {
        step.in = step.command.input();
        step.out = step.command.output();
    }

    const bool hasProc = static_cast<bool>(step.proc);
    if (!hasProc && !step.command.isValid()) {
        const std::string message = "pipeline step has no proc";
        setPipelineError(_p->ctx, ERR_NO_STEP_PROC, message);
        _p->last = Result::fail(message);
        complete(ERRORED);
        return;
    }

    auto run = [this, slot, hasProc]() mutable {
        if (_p->state != RUNNING) {
            complete(_p->state == ERRORED ? ERRORED : DONE);
            return;
        }

        Step& step = _p->steps[slot];
        Result result;
        try {
            result = hasProc ? step.proc(_p->ctx, step.in, step.out) : step.command.call();
        } catch (const std::exception& e) {
            setPipelineError(_p->ctx, ERR_EXCEPTION, e.what());
            result = Result::fail(e.what());
        } catch (...) {
            setPipelineError(_p->ctx, ERR_EXCEPTION, "unknown exception");
            result = Result::fail("unknown exception");
        }

        if (_p->state != RUNNING) {
            complete(_p->state == ERRORED ? ERRORED : DONE);
            return;
        }

        _p->last = std::move(result);
        if (_p->last.isError()) {
            complete(ERRORED);
            return;
        }
        if (_p->last.isAccepted()) {
            complete(DONE);
            return;
        }

        dispatch(slot + 1);
    };

    if (Loop* loop = resolveLoop(step)) {
        loop->post(std::move(run));
    } else {
        run();
    }
}

void Pipeline::complete(State state)
{
    _p->state = state;
    Result snapshot = _p->last;
    Handler handler = std::move(_p->handler);

    if (_p->state == DONE) {
        trigger<DONE_SIGNAL>(Var::custom(snapshot));
    } else if (_p->state == ERRORED) {
        trigger<ERROR_SIGNAL>(Var::custom(snapshot));
    }

    _p->completed.store(true, std::memory_order_release);
    if (handler) {
        handler(snapshot);
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
        Loop* loop = resolveLoop(step);
        if (loop && std::find(loops.begin(), loops.end(), loop) == loops.end()) {
            loops.push_back(loop);
        }
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

void Pipeline::onFinished(Handler handler)
{
    _p->handler = std::move(handler);
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

} // namespace ve
