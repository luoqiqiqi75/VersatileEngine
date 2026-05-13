// command.cpp - ve::Step, ve::Command, command:: namespace

#include "ve/core/command.h"
#include "ve/core/loop.h"
#include "ve/core/node.h"
#include "ve/core/log.h"
#include "ve/core/pipeline.h"
#include "parse_util.h"

namespace ve {

Result resultFromStepReturn(const Var& ret)
{
    if (ret.customIs<Result>()) {
        return ret.as<Result>();
    }
    return Result::ok(ret);
}

// ============================================================================
// Step  (order mirrors command.h: writeTo, addToPipeline, reg)
// ============================================================================

void Step::writeTo(Node* nd) const
{
    if (!nd) return;
    nd->set(first);
    if (second) nd->at("loop")->set(Var::custom(second));
}

void Step::addToPipeline(Node* nd, Pipeline& pipe)
{
    if (!nd || !nd->get().isCallable()) return;
    LoopRef lr;
    if (auto* ln = nd->find("loop", false))
        lr = ln->get().as<LoopRef>();
    // No default to loop::main() — empty LoopRef means synchronous inline execution.
    // Pipeline::runNext() dispatches async only when step.second is set.
    pipe.add(Step(nd->get(), std::move(lr)));
}

void Step::reg(const std::string& key, Step step, const std::string& help, char sep)
{
    Command::factory().reg(key, step.first, help, step.second, sep);
}

// ============================================================================
// Command  (order mirrors command.h):
//   ctor → stepCount → declare → addStep → build →
//   context → call(ctx) → call(input) → run(ctx) → run(input) →
//   factory → reg → declareNode
// ============================================================================

Command::Command(const std::string& key, char sep)
{
    const Factory& f = factory();
    _n = f.node(key, sep);
}

int Command::stepCount() const
{
    if (_n->get().isCallable()) return 1;
    if (auto* s = _n->find("steps", false)) return s->count();
    return 0;
}

Node* Command::declare()
{
    return _n->at("declare");
}

void Command::addStep(Step step)
{
    auto* steps_nd = _n->at("steps");
    int idx = steps_nd->count();
    auto* sn = steps_nd->at(idx);
    step.writeTo(sn);
}

Pipeline* Command::build() const
{
    auto* pipe = new Pipeline(_n->name());

    auto addStep = [&](Node* sn) {
        if (!sn || !sn->get().isCallable()) return;
        LoopRef lr;
        if (auto* ln = sn->find("loop", false))
            lr = ln->get().as<LoopRef>();
        // Default to loop::main() so commands dispatched from services
        // (HTTP/WS/TCP) run on the main event loop.
        if (!lr) lr = LoopRef::from(loop::main());
        pipe->add(Step(sn->get(), std::move(lr)));
    };

    if (_n->get().isCallable()) {
        addStep(_n);
    } else if (auto* steps_nd = _n->find("steps", false)) {
        for (auto* sn : *steps_nd)
            addStep(sn);
    }

    if (pipe->stepCount() == 0) {
        delete pipe;
        return nullptr;
    }
    return pipe;
}

Node* Command::context(Node* currentNode)
{
    auto* ctx = new Node("_ctx");
    if (auto* decl = _n->find("declare", false))
        ctx->setShadow(decl);
    ctx->set(Var(static_cast<void*>(currentNode)));
    return ctx;
}

Result Command::call(Node* ctx, bool wait, Pipeline** detachedOut)
{
    if (detachedOut) *detachedOut = nullptr;

    Pipeline* pipe = build();
    if (!pipe) return Result::fail(Var("command has no steps: " + _n->name()));

    Node* ctxToDelete = nullptr;
    if (!ctx) {
        ctx = context(nullptr);
        ctxToDelete = ctx;
    }

    auto cleanup = [&]() {
        delete pipe;
        if (ctxToDelete) delete ctxToDelete;
    };

    if (wait) {
        std::mutex mtx;
        std::condition_variable cv;
        bool finished = false;

        pipe->setResultHandler([&](const Result&) {
            std::lock_guard<std::mutex> lk(mtx);
            finished = true;
            cv.notify_all();
        });

        Result r = pipe->start(ctx);

        if (r.isAccepted()) {
            std::unique_lock<std::mutex> lk(mtx);
            while (!finished) {
                cv.wait_for(lk, std::chrono::milliseconds(10));
                const Pipeline::State st = pipe->state();
                if (st == Pipeline::DONE || st == Pipeline::ERRORED || st == Pipeline::IDLE) {
                    break;
                }
            }
        }

        Result lr = pipe->lastResult();
        cleanup();
        return lr;
    }

    Result r = pipe->start(ctx);

    if (!r.isAccepted()) {
        Result lr = pipe->lastResult();
        cleanup();
        return lr;
    }

    if (!detachedOut) {
        pipe->stop();
        cleanup();
        return Result::fail(Var(
            "Command::call(..., wait=false) requires non-null Pipeline** when command is asynchronous"));
    }

    *detachedOut = pipe;
    return Result::accept();
}

Result Command::call(const Var& input, bool wait)
{
    Node* ctx = context(nullptr);
    command::parseArgs(ctx, input);
    Result r = call(ctx, wait, nullptr);
    delete ctx;
    return r;
}

Pipeline* Command::run(Node* ctx)
{
    auto* pipe = build();
    if (!pipe) return nullptr;
    pipe->start(ctx);
    return pipe;
}

Pipeline* Command::run(const Var& input)
{
    auto* pipe = build();
    if (!pipe) return nullptr;
    pipe->start(input);
    return pipe;
}

Factory& Command::factory()
{
    return factory::at("cmd");
}

void Command::reg(const std::string& key, std::function<void(Command&)> builder,
                  const std::string& help, char sep)
{
    auto& f = factory();

    // Track in keys list first
    auto keys = f.keys();
    if (std::find(keys.begin(), keys.end(), key) == keys.end()) {
        // Add to keys by registering empty callable
        f.reg(key, Var(), "", {}, sep);
    }

    auto* nd = f.node(key, sep);
    Command cmd(nd);
    builder(cmd);
    if (!help.empty())
        cmd.setHelp(help);
}

Node* Command::declareNode(const std::string& key, char sep)
{
    auto& f = factory();

    // Track in keys list if not already present
    auto keys = f.keys();
    if (std::find(keys.begin(), keys.end(), key) == keys.end()) {
        f.reg(key, Var(), "", {}, sep);
    }

    return f.node(key, sep)->at("declare");
}

// ============================================================================
// command:: namespace — ctx-operating helpers (parseArgs / Args / args)
// ============================================================================

namespace command {

// --- argument parsing (state-machine, declare-driven) ---

static void buildDeclInfo(const Node* decl,
                          std::vector<std::string>& paramOrder,
                          std::map<std::string, std::string>& shortMap,
                          std::set<std::string>& longNames)
{
    if (!decl) return;
    for (auto* param : *decl) {
        const auto& nm = param->name();
        if (nm.empty() || nm[0] == '_') continue;
        longNames.insert(nm);
        if (auto* shortNode = param->find("_short", false)) {
            std::string s = shortNode->getString();
            if (!s.empty()) shortMap[s] = nm;
        } else {
            paramOrder.push_back(nm);
        }
    }
}

bool parseArgs(Node* ctx, const std::vector<std::string>& args, int startIdx)
{
    if (!ctx) return false;

    ctx->clear();

    const Node* decl = ctx->shadow();

    std::vector<std::string> paramOrder;
    std::map<std::string, std::string> shortMap;
    std::set<std::string> longNames;
    buildDeclInfo(decl, paramOrder, shortMap, longNames);

    std::string currentTarget;
    int posIndex = 0;

    auto isKeyword = [&](const std::string& token) -> std::string {
        if (token.size() < 2 || token[0] != '-') return {};
        if (parse::isInt(token) || parse::isDouble(token)) return {};
        if (token[1] == '-') {
            auto eq = token.find('=', 2);
            std::string name = (eq != std::string::npos) ? token.substr(2, eq - 2) : token.substr(2);
            if (longNames.count(name)) return name;
            return {};
        }
        if (token.size() == 2) {
            std::string key(1, token[1]);
            auto it = shortMap.find(key);
            if (it != shortMap.end()) return it->second;
        }
        return {};
    };

    for (size_t i = static_cast<size_t>(startIdx); i < args.size(); ++i) {
        const auto& token = args[i];

        // --name=value inline form
        if (token.size() > 2 && token[0] == '-' && token[1] == '-') {
            auto eq = token.find('=', 2);
            if (eq != std::string::npos) {
                std::string name = token.substr(2, eq - 2);
                if (longNames.count(name)) {
                    currentTarget.clear();
                    ctx->at(name, false)->set(parse::parseValue(token.substr(eq + 1)));
                    continue;
                }
            }
        }

        std::string matched = isKeyword(token);
        if (!matched.empty()) {
            currentTarget = matched;
            continue;
        }

        if (!currentTarget.empty()) {
            ctx->at(currentTarget, false)->set(parse::parseValue(token));
            currentTarget.clear();
        } else if (posIndex < static_cast<int>(paramOrder.size())) {
            ctx->at(paramOrder[posIndex], false)->set(parse::parseValue(token));
            ++posIndex;
        } else {
            ctx->at(ctx->count(), false)->set(parse::parseValue(token));
        }
    }

    if (!currentTarget.empty()) {
        ctx->at(currentTarget, false)->set(true);
    }

    return true;
}

bool parseArgs(Node* ctx, const Var& input)
{
    if (!ctx) return false;

    ctx->clear();
    if (input.isNull()) return true;

    if (input.isDict()) {
        for (auto& [key, val] : input.toDict()) {
            if (!key.empty() && key[0] != '_')
                ctx->at(key, false)->set(val);
        }
        return true;
    }

    if (input.isList()) {
        std::vector<std::string> strs;
        strs.reserve(input.toList().size());
        for (auto& item : input.toList())
            strs.push_back(item.toString());
        return parseArgs(ctx, strs, 0);
    }

    // scalar: store as first positional param
    const Node* decl = ctx->shadow();
    if (decl) {
        for (auto* param : *decl) {
            const auto& nm = param->name();
            if (nm.empty() || nm[0] == '_') continue;
            if (!param->find("_short", false)) {
                ctx->at(nm, false)->set(input);
                return true;
            }
        }
    }
    ctx->at(0, false)->set(input);
    return true;
}

// --- Args accessor ---

Var Args::var(const std::string& key, const Var& def) const
{
    if (!_n || key.empty()) return def;
    // use_shadow=true: falls back to declare default values
    if (auto* n = _n->find(key)) {
        Var v = n->get();
        if (!v.isNull()) return v;
    }
    return def;
}

std::string Args::string(const std::string& key, const std::string& def) const
{
    Var v = var(key);
    return v.isNull() ? def : v.toString(def);
}

int64_t Args::integer(const std::string& key, int64_t def) const
{
    Var v = var(key);
    return v.isNull() ? def : v.toInt64(def);
}

double Args::number(const std::string& key, double def) const
{
    Var v = var(key);
    return v.isNull() ? def : v.toDouble(def);
}

bool Args::flag(const std::string& key, bool def) const
{
    Var v = var(key);
    return v.isNull() ? def : v.toBool(def);
}

bool Args::has(const std::string& key) const
{
    if (!_n || key.empty()) return false;
    // use_shadow=false: only check if user explicitly set this param
    auto* n = _n->find(key, false);
    return n && !n->get().isNull();
}

Args args(Node* ctx) { return Args(ctx); }

} // namespace command

} // namespace ve
