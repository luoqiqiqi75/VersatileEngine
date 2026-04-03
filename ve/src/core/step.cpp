// step.cpp — ve::Step implementation

#include "ve/core/step.h"
#include "ve/core/node.h"

namespace ve {

Step::Step(StepFn fn)
    : _fn(std::move(fn)) {}

Step::Step(const std::string& name, StepFn fn)
    : _name(name), _fn(std::move(fn)) {}

Step::Step(const std::string& name, StepFn fn, LoopRef loop)
    : _name(name), _fn(std::move(fn)), _loop(std::move(loop)) {}

Result Step::exec(Node* ctx) const
{
    if (!_fn) return Result(Result::FAIL, std::string("step has no function"));
    return _fn(ctx);
}

Result Step::exec(const Var& input) const
{
    if (!_fn) return Result(Result::FAIL, std::string("step has no function"));
    Node tmp("_input");
    tmp.set(input);
    return _fn(&tmp);
}

Step Step::clone() const
{
    Step copy;
    copy._name = _name;
    copy._fn = _fn;
    copy._loop = _loop;
    copy._inputDesc = _inputDesc;
    copy._outputDesc = _outputDesc;
    return copy;
}

} // namespace ve
