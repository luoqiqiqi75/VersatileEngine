// command.cpp - ve::Command, Step, global factory, and command:: namespace

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

void Command::addStep(Step step) { _steps.push_back(std::move(step)); }

const std::string& Command::name() const      { return _name; }
int Command::stepCount() const                 { return static_cast<int>(_steps.size()); }
const Vector<Step>& Command::steps() const     { return _steps; }
const std::string& Command::help() const       { return _help; }
void Command::setHelp(const std::string& h)    { _help = h; }

Pipeline* Command::pipeline() const
{
    auto* pipe = new Pipeline(_name);
    for (const auto& s : _steps) {
        Step step = s;
        if (!step.second) {
            step.second = LoopRef::from(loop::main());
        }
        pipe->add(std::move(step));
    }
    return pipe;
}

Node* Command::node() const
{
    if (!_node)
        _node = new Node(_name);
    return _node;
}

// ============================================================================
// Global factory
// ============================================================================

CommandFactory& GlobalCommandFactory()
{
    static CommandFactory* s = new CommandFactory("GlobalCommandFactory");
    return *s;
}

void registerStep(const std::string& key, Step step, const std::string& help)
{
    GlobalCommandFactory().insertOne(key, [key, st = std::move(step), help]() -> Command* {
        auto* cmd = new Command(key);
        cmd->addStep(std::move(st));
        if (!help.empty()) {
            cmd->setHelp(help);
        }
        return cmd;
    });
}

void registerCommand(const std::string& key, std::function<void(Command&)> builder,
                     const std::string& help)
{
    GlobalCommandFactory().insertOne(key, [key, builder = std::move(builder), help]() -> Command* {
        auto* cmd = new Command(key);
        builder(*cmd);
        if (!help.empty()) {
            cmd->setHelp(help);
        }
        return cmd;
    });
}

// ============================================================================
// command:: namespace
// ============================================================================

namespace command {

void build(const std::string& key, std::function<void(Command&)> builder,
           const std::string& help)
{
    registerCommand(key, std::move(builder), help);
}

// --- context ---

Node* declareNode(const std::string& key)
{
    return node::root()->at("ve/command/declare/" + key, false);
}

Node* context(const std::string& key, Node* currentNode)
{
    auto* ctx = new Node("_ctx");
    if (auto* decl = declareNode(key)) {
        ctx->setShadow(decl);
    }
    ctx->set(Var(static_cast<void*>(currentNode)));
    return ctx;
}

// --- argument parsing (state-machine, declare-driven) ---

// Build lookup tables from declare node.
// paramOrder: positional params (no _short), in declare child order.
// shortMap:   short-char -> param name (has _short).
// longNames:  all param names (for --name matching).
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
            // --name or --name=value
            auto eq = token.find('=', 2);
            std::string name = (eq != std::string::npos) ? token.substr(2, eq - 2) : token.substr(2);
            if (longNames.count(name)) return name;
            return {};
        }
        // -x (single char short flag)
        if (token.size() == 2) {
            std::string key(1, token[1]);
            auto it = shortMap.find(key);
            if (it != shortMap.end()) return it->second;
        }
        return {};
    };

    for (size_t i = static_cast<size_t>(startIdx); i < args.size(); ++i) {
        const auto& token = args[i];

        // Check --name=value form
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

        // Check if token is a keyword (-f or --file)
        std::string matched = isKeyword(token);
        if (!matched.empty()) {
            currentTarget = matched;
            continue;
        }

        // Token is a value
        if (!currentTarget.empty()) {
            // Assign to the current flag target
            ctx->at(currentTarget, false)->set(parse::parseValue(token));
            currentTarget.clear();
        } else if (posIndex < static_cast<int>(paramOrder.size())) {
            // Assign to next positional param
            ctx->at(paramOrder[posIndex], false)->set(parse::parseValue(token));
            ++posIndex;
        } else {
            // Extra positional: store as anonymous child
            ctx->at(ctx->count(), false)->set(parse::parseValue(token));
        }
    }

    // A trailing flag with no value becomes a boolean true
    if (!currentTarget.empty()) {
        ctx->at(currentTarget, false)->set(true);
    }

    return true;
}

// --- parseArgs(Var) overload ---

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

    // Scalar: store as first positional param
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

Args args(Node* ctx) { return Args(ctx); }

Var Args::var(const std::string& key, const Var& def) const
{
    if (!ctx || key.empty()) return def;
    // use_shadow=true: falls back to declare default values
    if (auto* n = ctx->find(key)) {
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
    if (!ctx || key.empty()) return false;
    // use_shadow=false: only check if user explicitly set this param
    auto* n = ctx->find(key, false);
    return n && !n->get().isNull();
}

// --- execution ---

Result call(const std::string& key, Node* ctx, bool wait, Pipeline** detachedOut)
{
    if (detachedOut) {
        *detachedOut = nullptr;
    }

    if (!GlobalCommandFactory().has(key)) {
        return Result::fail(Var("not found: " + key));
    }

    Command* cmd = GlobalCommandFactory().exec(key);
    if (!cmd) {
        return Result::fail(Var("command factory returned null: " + key));
    }

    Pipeline* pipe = cmd->pipeline();
    delete cmd;

    Node* ctxToDelete = nullptr;
    if (!ctx) {
        ctx = context(key);
        ctxToDelete = ctx;
    }

    auto cleanup = [&]() {
        delete pipe;
        if (ctxToDelete) {
            delete ctxToDelete;
        }
    };

    if (wait) {
        std::mutex              mtx;
        std::condition_variable cv;
        bool                    finished = false;

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
            "command::call(..., wait=false) requires non-null Pipeline** when command is asynchronous"));
    }

    *detachedOut = pipe;
    return Result::accept();
}

Result call(const std::string& key, const Var& input, bool wait)
{
    Node* ctx = context(key);
    parseArgs(ctx, input);
    Result r = call(key, ctx, wait, nullptr);
    delete ctx;
    return r;
}

Pipeline* run(const std::string& key, Node* ctx)
{
    if (!GlobalCommandFactory().has(key)) return nullptr;

    Command* cmd = GlobalCommandFactory().exec(key);
    if (!cmd) return nullptr;

    Pipeline* pipe = cmd->pipeline();
    delete cmd;

    pipe->start(ctx);
    return pipe;
}

Pipeline* run(const std::string& key, const Var& input)
{
    if (!GlobalCommandFactory().has(key)) return nullptr;

    Command* cmd = GlobalCommandFactory().exec(key);
    if (!cmd) return nullptr;

    Pipeline* pipe = cmd->pipeline();
    delete cmd;

    pipe->start(input);
    return pipe;
}

// --- query ---

bool has(const std::string& key)
{
    return GlobalCommandFactory().has(key);
}

Strings keys()
{
    return GlobalCommandFactory().keys();
}

std::string help(const std::string& key)
{
    // Try declare node first
    if (auto* decl = declareNode(key)) {
        if (auto* h = decl->find("_help", false)) {
            return h->getString();
        }
    }

    // Fall back to Command help string
    if (GlobalCommandFactory().has(key)) {
        Command* cmd = GlobalCommandFactory().exec(key);
        if (cmd) {
            std::string h = cmd->help();
            delete cmd;
            return h;
        }
    }

    return {};
}

} // namespace command

} // namespace ve
