// pipeline.cpp — ve::Pipeline: state machine + Step chain execution

#include "ve/core/pipeline.h"
#include "ve/core/node.h"
#include "ve/core/factory.h"
#include "ve/core/schema.h"

namespace ve {

struct Pipeline::Private
{
    Vector<Step>    steps;
    List<Step>      queue;
    State           state = IDLE;
    int             stepIndex = -1;
    Node*           context = nullptr;
    bool            ownsContext = false;
    Result          lastResult = Result::ok();
    ResultHandler   handler;

    void clearContext()
    {
        if (ownsContext && context) {
            delete context;
        }
        context = nullptr;
        ownsContext = false;
    }
};

Pipeline::Pipeline(const std::string& name)
    : Object(name) {}

Pipeline::~Pipeline() { _p->clearContext(); }

// --- build ---

Pipeline& Pipeline::add(const Step& step)
{
    _p->steps.push_back(step);
    return *this;
}

int Pipeline::stepCount() const { return static_cast<int>(_p->steps.size()); }

// --- execution state machine ---

Result Pipeline::start(Node* ctx)
{
    _p->clearContext();
    _p->state = RUNNING;
    if (!ctx) {
        ctx = new Node("_ctx");
        _p->ownsContext = true;
    } else {
        _p->ownsContext = false;
    }
    _p->context = ctx;
    _p->stepIndex = 0;
    _p->lastResult = Result::ok();

    _p->queue.clear();
    for (const auto& s : _p->steps) {
        _p->queue.push_back(s);
    }

    if (_p->queue.empty()) {
        complete(DONE, CMD_DONE, Result::ok());
        return _p->lastResult;
    }

    runNext();

    if (_p->state == ERRORED) {
        return _p->lastResult;
    }
    if (_p->state == RUNNING || _p->state == PAUSED) {
        return Result::accept();
    }
    return _p->lastResult;
}

Result Pipeline::start(const Var& input)
{
    auto* ctx = new Node("_input");
    ctx->set(input);
    auto r = start(ctx);
    _p->ownsContext = true;
    return r;
}

void Pipeline::pause()
{
    if (_p->state == RUNNING) {
        _p->state = PAUSED;
    }
}

void Pipeline::resume()
{
    if (_p->state != PAUSED) {
        return;
    }
    _p->state = RUNNING;
    runNext();
}

void Pipeline::stop()
{
    if (_p->state == IDLE) {
        return;
    }
    _p->queue.clear();
    _p->state = IDLE;
    _p->stepIndex = -1;
    _p->clearContext();
}

void Pipeline::finish(const Result& result)
{
    handleResult(result);
}

Pipeline::State Pipeline::state() const { return _p->state; }

// --- progress ---

int Pipeline::currentStep() const { return _p->stepIndex; }
Node* Pipeline::context() const { return _p->context; }
const Result& Pipeline::lastResult() const { return _p->lastResult; }

// --- result handler ---

void Pipeline::setResultHandler(const ResultHandler& handler)
{
    _p->handler = handler;
}

// --- clone ---

Pipeline* Pipeline::clone() const
{
    auto* copy = new Pipeline(name());
    for (const auto& s : _p->steps) {
        copy->_p->steps.push_back(s);
    }
    return copy;
}

// --- internal ---

void Pipeline::runNext()
{
    while (_p->state == RUNNING && !_p->queue.empty()) {
        Step step = std::move(_p->queue.front());
        _p->queue.pop_front();

        const Var stepIn(Var(static_cast<void*>(_p->context)));

        if (step.second) {
            auto alive = Alive::create();
            auto self = this;
            auto ctx = _p->context;
            auto stepFn = step;
            step.second.post([self, alive, stepFn, ctx]() {
                if (alive.dead()) {
                    return;
                }
                if (!stepFn.first.isCallable()) {
                    self->handleResult(Result::fail(Var("step has no callable")));
                    return;
                }
                Var ret = stepFn.first.invoke(Var(static_cast<void*>(ctx)));
                self->handleResult(resultFromStepReturn(ret));
            });
            return;
        }

        if (!step.first.isCallable()) {
            complete(ERRORED, CMD_ERROR, Result::fail(Var("step has no callable")));
            return;
        }

        Var ret = step.first.invoke(stepIn);
        Result r = resultFromStepReturn(ret);
        if (r.isAccepted()) {
            return;
        }
        if (r.isError()) {
            complete(ERRORED, CMD_ERROR, r);
            return;
        }
        _p->lastResult = r;
        ++_p->stepIndex;
    }

    if (_p->state == RUNNING && _p->queue.empty()) {
        complete(DONE, CMD_DONE, _p->lastResult);
    }
}

void Pipeline::handleResult(const Result& result)
{
    if (_p->state != RUNNING && _p->state != PAUSED) {
        return;
    }

    if (result.isError()) {
        complete(ERRORED, CMD_ERROR, result);
        return;
    }

    _p->lastResult = result;
    ++_p->stepIndex;

    if (_p->queue.empty()) {
        complete(DONE, CMD_DONE, result);
        return;
    }

    if (_p->state == PAUSED) {
        return;
    }
    _p->state = RUNNING;
    runNext();
}

void Pipeline::complete(State finalState, SignalT signal, const Result& result)
{
    Result out = result;
    if (_p->context && finalState == DONE && signal == CMD_DONE) {
        if (auto* rn = _p->context->find("_result", false)) {
            out.setContent(schema::exportAs<schema::VarS>(rn));
        }
    }

    _p->state = finalState;
    _p->lastResult = std::move(out);
    _p->queue.clear();

    if (signal == CMD_DONE) {
        trigger<CMD_DONE>(Var::custom(_p->lastResult));
    }
    else if (signal == CMD_ERROR) {
        trigger<CMD_ERROR>(Var::custom(_p->lastResult));
    }

    if (_p->handler) {
        auto h = std::move(_p->handler);
        Result res = _p->lastResult;
        h(res);
    }
}

} // namespace ve
