// command.cpp - ve::Command, Step, global factory, and command:: namespace

#include "ve/core/command.h"
#include "ve/core/node.h"
#include "ve/core/log.h"
#include "ve/core/pipeline.h"

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
        pipe->add(s);
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
    return node::root()->find("ve/command/declare/" + key, false);
}

Node* context(const std::string& key, Node* currentNode)
{
    auto* ctx = new Node("_ctx");
    if (auto* decl = declareNode(key)) {
        ctx->setShadow(decl);
    }
    if (currentNode) {
        ctx->at("_current")->set(Var(static_cast<void*>(currentNode)));
    }
    return ctx;
}

// --- argument parsing ---

// Lightweight flag parser (self-contained, mirrors terminal_util.h logic)
namespace {

struct ParsedFlags {
    std::vector<std::pair<std::string, std::string>> named;
    std::vector<std::string> positional;
};

bool isInt(const std::string& s)
{
    if (s.empty()) return false;
    size_t start = (s[0] == '-' || s[0] == '+') ? 1 : 0;
    if (start >= s.size()) return false;
    for (size_t i = start; i < s.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    }
    return true;
}

bool isDouble(const std::string& s)
{
    if (s.empty()) return false;
    bool dot = false;
    size_t start = (s[0] == '-' || s[0] == '+') ? 1 : 0;
    if (start >= s.size()) return false;
    for (size_t i = start; i < s.size(); ++i) {
        if (s[i] == '.') { if (dot) return false; dot = true; }
        else if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    }
    return dot;
}

Var parseValue(const std::string& raw)
{
    if (raw == "null")  return Var();
    if (raw == "true")  return Var(true);
    if (raw == "false") return Var(false);
    if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"')
        return Var(raw.substr(1, raw.size() - 2));
    if (isInt(raw))    return Var(static_cast<std::int64_t>(std::stoll(raw)));
    if (isDouble(raw)) return Var(std::stod(raw));
    return Var(raw);
}

ParsedFlags parseFlags(const std::vector<std::string>& args, int startIdx)
{
    ParsedFlags f;
    bool endOfFlags = false;
    for (size_t i = startIdx; i < args.size(); ++i) {
        auto& a = args[i];
        if (endOfFlags) {
            f.positional.push_back(a);
            continue;
        }
        if (a == "--") {
            endOfFlags = true;
            continue;
        }
        if (a.size() > 2 && a[0] == '-' && a[1] == '-') {
            auto eq = a.find('=', 2);
            if (eq != std::string::npos) {
                f.named.push_back({a.substr(2, eq - 2), a.substr(eq + 1)});
            } else if (i + 1 < args.size() && !args[i + 1].empty() && args[i + 1][0] != '-') {
                f.named.push_back({a.substr(2), args[++i]});
            } else {
                f.named.push_back({a.substr(2), ""});
            }
        } else if (a.size() > 1 && a[0] == '-' && !isInt(a) && !isDouble(a)) {
            for (size_t j = 1; j < a.size(); ++j) {
                std::string key(1, a[j]);
                if (j == a.size() - 1 && i + 1 < args.size() && !args[i + 1].empty() && args[i + 1][0] != '-') {
                    f.named.push_back({key, args[++i]});
                } else {
                    f.named.push_back({key, ""});
                }
            }
        } else {
            f.positional.push_back(a);
        }
    }
    return f;
}

} // anonymous namespace

bool parseArgs(Node* ctx, const std::vector<std::string>& args, int startIdx)
{
    if (!ctx) return false;

    // Build maps from declare node metadata (via shadow)
    const Node* decl = ctx->shadow();

    std::map<int, std::string> posMap;         // _pos index -> param name
    std::map<std::string, std::string> shortMap; // short char -> param name

    if (decl) {
        for (auto* param : *decl) {
            const auto& nm = param->name();
            if (nm.empty() || nm[0] == '_') continue;

            if (auto* posNode = param->find("_pos", false)) {
                posMap[posNode->getInt(-1)] = nm;
            }
            if (auto* shortNode = param->find("_short", false)) {
                std::string s = shortNode->getString();
                if (s.size() == 1) shortMap[s] = nm;
            }
        }
    }

    // Parse flags
    auto flags = parseFlags(args, startIdx);

    // Map positional args to named params
    for (int i = 0; i < static_cast<int>(flags.positional.size()); ++i) {
        auto it = posMap.find(i);
        if (it != posMap.end()) {
            ctx->at(it->second)->set(parseValue(flags.positional[i]));
        } else {
            // No positional mapping, store as #N
            ctx->at(i)->set(parseValue(flags.positional[i]));
        }
    }

    // Map named flags to params
    for (auto& [flag, value] : flags.named) {
        // Resolve short flag to param name
        std::string paramName;
        if (flag.size() == 1) {
            auto it = shortMap.find(flag);
            if (it != shortMap.end()) paramName = it->second;
        }
        if (paramName.empty()) paramName = flag;

        if (value.empty()) {
            ctx->at(paramName)->set(true);  // boolean flag
        } else {
            ctx->at(paramName)->set(parseValue(value));
        }
    }

    return true;
}

// --- execution ---

Result call(const std::string& key, Node* ctx)
{
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

    Result r = pipe->start(ctx);
    Result lr = pipe->lastResult();
    delete pipe;
    if (ctxToDelete) {
        delete ctxToDelete;
    }

    if (r.isAccepted()) {
        return Result::fail(Var("sync call on async command: " + key));
    }
    return lr;
}

Result call(const std::string& key, const Var& input)
{
    if (!GlobalCommandFactory().has(key)) {
        return Result::fail(Var("not found: " + key));
    }

    Node* ctx = context(key);
    ctx->set(input);
    Result r = call(key, ctx);
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
