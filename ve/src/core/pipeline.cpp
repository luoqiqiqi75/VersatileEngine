// pipeline.cpp — ve::Pipeline: state machine + Step chain execution

#include "ve/core/pipeline.h"
#include "ve/core/node.h"
#include "ve/core/factory.h"

namespace ve {

struct Pipeline::Private
{
    Vector<Step>    steps;       // definition (template)
    List<Step>      queue;       // runtime queue (copied from steps on start)
    State           state = IDLE;
    int             stepIndex = -1;
    Node*           context = nullptr;
    bool            ownsContext = false;
    Result          lastResult = Result::SUCCESS;
    ResultHandler   handler;

    void clearContext()
    {
        if (ownsContext && context) { delete context; }
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

Pipeline& Pipeline::add(const std::string& name, Step::StepFn fn)
{
    _p->steps.push_back(Step(name, std::move(fn)));
    return *this;
}

Pipeline& Pipeline::add(const std::string& name, Step::StepFn fn, LoopRef loop)
{
    _p->steps.push_back(Step(name, std::move(fn), std::move(loop)));
    return *this;
}

int Pipeline::stepCount() const { return static_cast<int>(_p->steps.size()); }

// --- execution state machine ---

Result Pipeline::start(Node* ctx)
{
    _p->clearContext();
    _p->state = RUNNING;
    _p->context = ctx;
    _p->stepIndex = 0;
    _p->lastResult = Result::SUCCESS;

    _p->queue.clear();
    for (auto& s : _p->steps)
        _p->queue.push_back(s.clone());

    if (_p->queue.empty()) {
        complete(DONE, CMD_DONE, Result::SUCCESS);
        return _p->lastResult;
    }

    runNext();

    if (_p->state == ERRORED) {
        return _p->lastResult;
    }
    if (_p->state == RUNNING || _p->state == PAUSED)
        return Result::ACCEPT;
    return _p->lastResult;
}

Result Pipeline::start(const Var& input)
{
    // Backward compat: create a temp context node with input as value
    auto* ctx = new Node("_input");
    ctx->set(input);
    auto r = start(ctx);
    _p->ownsContext = true;  // Pipeline owns this temp node
    return r;
}

void Pipeline::pause()
{
    if (_p->state == RUNNING)
        _p->state = PAUSED;
}

void Pipeline::resume()
{
    if (_p->state != PAUSED) return;
    _p->state = RUNNING;
    runNext();
}

void Pipeline::stop()
{
    if (_p->state == IDLE) return;
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
    for (auto& s : _p->steps)
        copy->_p->steps.push_back(s.clone());
    return copy;
}

// --- internal ---

void Pipeline::runNext()
{
    while (_p->state == RUNNING && !_p->queue.empty()) {
        Step step = std::move(_p->queue.front());
        _p->queue.pop_front();

        if (step.loop()) {
            auto alive = Alive::create();
            auto self = this;
            auto ctx = _p->context;
            auto stepFn = step;
            step.loop().post([self, alive, stepFn, ctx]() {
                if (alive.dead()) return;
                Result r = stepFn.exec(ctx);
                self->handleResult(r);
            });
            return;
        }

        Result r = step.exec(_p->context);
        if (r.isAccepted()) return;  // async - wait for external finish()
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
    if (_p->state != RUNNING && _p->state != PAUSED) return;

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

    if (_p->state == PAUSED) return;  // will continue on resume()
    _p->state = RUNNING;
    runNext();
}

void Pipeline::complete(State finalState, SignalT signal, const Result& result)
{
    _p->state = finalState;
    _p->lastResult = result;
    _p->queue.clear();

    if (signal == CMD_DONE)
        trigger<CMD_DONE>(Var::custom(result));
    else if (signal == CMD_ERROR)
        trigger<CMD_ERROR>(Var::custom(result));

    if (_p->handler)
        _p->handler(result);
}

} // namespace ve
