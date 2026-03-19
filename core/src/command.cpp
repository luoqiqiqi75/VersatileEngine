// command.cpp — ve::Command, global factories, and command:: namespace

#include "ve/core/command.h"
#include "ve/core/node.h"

namespace ve {

// ============================================================================
// Result::toString (moved from old command.cpp)
// ============================================================================

std::string Result::toString() const
{
    if (isSuccess()) return "success";
    if (isAccepted()) return "accepted";
    std::string s = "error(" + std::to_string(_code) + ")";
    if (!_content.isNull()) s += ": " + _content.toString();
    return s;
}

// ============================================================================
// Command
// ============================================================================

Command::Command(const std::string& name) : _name(name) {}

Command::~Command()
{
    if (_node) {
        auto* p = _node->parent();
        if (p) p->remove(_node);
        else delete _node;
    }
}

void Command::addStep(const Step& step)              { _steps.push_back(step); }
void Command::addStep(const std::string& name, Step::StepFn fn)
{ _steps.push_back(Step(name, std::move(fn))); }
void Command::addStep(const std::string& name, Step::StepFn fn, LoopRef loop)
{ _steps.push_back(Step(name, std::move(fn), std::move(loop))); }

const std::string& Command::name() const      { return _name; }
int Command::stepCount() const                 { return static_cast<int>(_steps.size()); }
const Vector<Step>& Command::steps() const     { return _steps; }
const std::string& Command::help() const       { return _help; }
void Command::setHelp(const std::string& h)    { _help = h; }

Pipeline* Command::pipeline() const
{
    auto* pipe = new Pipeline(_name);
    for (auto& s : _steps)
        pipe->add(s.clone());
    return pipe;
}

Node* Command::node() const
{
    if (!_node)
        _node = new Node(_name);
    return _node;
}

// ============================================================================
// Global factories
// ============================================================================

static std::unordered_map<std::string, std::string>& helpMap()
{
    static std::unordered_map<std::string, std::string> m;
    return m;
}

StepFactory& GlobalStepFactory()
{
    static StepFactory* s = new StepFactory("GlobalStepFactory");
    return *s;
}

CommandFactory& GlobalCommandFactory()
{
    static CommandFactory* s = new CommandFactory("GlobalCommandFactory");
    return *s;
}

// ============================================================================
// command:: namespace
// ============================================================================

namespace command {

void reg(const std::string& key, Step::StepFn fn, const std::string& help)
{
    GlobalStepFactory().insertOne(key, std::move(fn));
    if (!help.empty()) helpMap()[key] = help;
}

Pipeline* run(const std::string& key, const Var& input)
{
    if (GlobalCommandFactory().has(key)) {
        Command* cmd = GlobalCommandFactory().exec(key);
        if (!cmd) return nullptr;
        Pipeline* pipe = cmd->pipeline();
        delete cmd;
        pipe->start(input);
        return pipe;
    }

    if (GlobalStepFactory().has(key)) {
        return step(key, input);
    }

    return nullptr;
}

Pipeline* run(const std::string& key, Node* input)
{
    if (!input) return run(key, Var());
    return run(key, input->value());
}

Pipeline* step(const std::string& key, const Var& input)
{
    if (!GlobalStepFactory().has(key)) return nullptr;

    auto& fn = GlobalStepFactory()[key];
    auto* pipe = new Pipeline(key);
    pipe->add(Step(key, fn));
    pipe->start(input);
    return pipe;
}

Result call(const std::string& key, const Var& input)
{
    if (GlobalStepFactory().has(key)) {
        auto& fn = GlobalStepFactory()[key];
        return fn(input);
    }

    if (GlobalCommandFactory().has(key)) {
        Command* cmd = GlobalCommandFactory().exec(key);
        if (!cmd) return Result(Result::FAIL, "command factory returned null: " + key);
        Pipeline* pipe = cmd->pipeline();
        delete cmd;
        Result r = pipe->start(input);
        if (r.isAccepted()) {
            delete pipe;
            return Result(Result::FAIL, "sync call on async command: " + key);
        }
        Result final_result = (pipe->state() == Pipeline::DONE)
            ? Result::SUCCESS
            : Result(Result::FAIL, "command failed: " + key);
        delete pipe;
        return final_result;
    }

    return Result(Result::FAIL, "not found: " + key);
}

bool has(const std::string& key)
{
    return GlobalStepFactory().has(key) || GlobalCommandFactory().has(key);
}

Strings keys()
{
    auto sk = GlobalStepFactory().keys();
    auto ck = GlobalCommandFactory().keys();
    sk.insert(sk.end(), ck.begin(), ck.end());
    return sk;
}

std::string help(const std::string& key)
{
    auto& m = helpMap();
    auto it = m.find(key);
    return (it != m.end()) ? it->second : std::string();
}

} // namespace command

} // namespace ve
