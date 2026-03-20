// pipeline.cpp — ve::Pipeline: state machine + Step chain execution

#include "ve/core/pipeline.h"
#include "ve/core/factory.h"

namespace ve {

struct Pipeline::Private
{
    Vector<Step>    steps;       // definition (template)
    List<Step>      queue;       // runtime queue (copied from steps on start)
    State           state = IDLE;
    int             stepIndex = -1;
    Var             input;
    ResultHandler   handler;
};

Pipeline::Pipeline(const std::string& name)
    : Object(name) {}

Pipeline::~Pipeline() = default;

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

Result Pipeline::start(const Var& input)
{
    _p->state = RUNNING;
    _p->input = input;
    _p->stepIndex = 0;

    _p->queue.clear();
    for (auto& s : _p->steps)
        _p->queue.push_back(s.clone());

    if (_p->queue.empty()) {
        complete(DONE, CMD_DONE, Result::SUCCESS);
        return Result::SUCCESS;
    }

    runNext();

    if (_p->state == ERRORED) {
        auto* r = _p->input.customPtr<Result>();
        return r ? *r : Result::FAIL;
    }
    if (_p->state == RUNNING || _p->state == PAUSED)
        return Result::ACCEPT;
    return Result::SUCCESS;
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
}

void Pipeline::finish(const Result& result)
{
    handleResult(result);
}

Pipeline::State Pipeline::state() const { return _p->state; }

// --- progress ---

int Pipeline::currentStep() const { return _p->stepIndex; }
const Var& Pipeline::input() const { return _p->input; }

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
            auto input = _p->input;
            auto stepFn = step;
            step.loop().post([self, alive, stepFn, input]() {
                if (alive.dead()) return;
                Result r = stepFn.exec(input);
                self->handleResult(r);
            });
            return;
        }

        Result r = step.exec(_p->input);
        if (r.isAccepted()) return;  // async — wait for external finish()
        if (r.isError()) {
            complete(ERRORED, CMD_ERROR, r);
            return;
        }
        ++_p->stepIndex;
    }

    if (_p->state == RUNNING && _p->queue.empty()) {
        complete(DONE, CMD_DONE, Result::SUCCESS);
    }
}

void Pipeline::handleResult(const Result& result)
{
    if (_p->state != RUNNING && _p->state != PAUSED) return;

    if (result.isError()) {
        complete(ERRORED, CMD_ERROR, result);
        return;
    }

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
    _p->queue.clear();

    trigger<CMD_DONE>(Var::custom(result));
    if (signal == CMD_ERROR)
        trigger<CMD_ERROR>(Var::custom(result));

    if (_p->handler)
        _p->handler(result);
}

} // namespace ve
