// command.cpp — Node-centric command execution engine
//
// Step functions are connected as slots to an internal CMD_IMPL signal.
// Shadow relationships between step Nodes determine execution order.
// CMD_DONE propagates completion to dependent steps.

#include "ve/core/command.h"
#include "ve/core/convert.h"

namespace ve {

// ============================================================================
// Result
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
// Internal signal for triggering step execution
// ============================================================================

static constexpr Object::SignalT CMD_IMPL = 0x0031;

// ============================================================================
// Helpers
// ============================================================================

static Object::ActionT wrapStep(Node* step, StepFn fn) {
    return [step, fn = std::move(fn)](const Var&) {
        Result r = fn(step);
        step->set(Var::custom(r));
        step->trigger<CMD_DONE>(Var::custom(r));
    };
}

// Collect step children in shadow-based topological order.
// Steps whose shadow is null or outside `cmd` are roots.
static Vector<Node*> topoSort(Node* cmd)
{
    auto children = cmd->children();
    int n = (int)children.size();
    if (n == 0) return {};

    Dict<int> indexOf;
    for (int i = 0; i < n; ++i)
        indexOf[children[i]->name().empty()
                    ? std::to_string(i) : children[i]->name()] = i;

    Vector<int> inDeg(n, 0);
    Vector<Vector<int>> adj(n);

    for (int i = 0; i < n; ++i) {
        auto* sh = children[i]->shadow();
        if (!sh || sh->parent() != cmd) continue;
        int j = cmd->indexOf(sh);
        if (j < 0 || j >= n) continue;
        adj[j].push_back(i);
        ++inDeg[i];
    }

    Vector<int> queue;
    for (int i = 0; i < n; ++i)
        if (inDeg[i] == 0) queue.push_back(i);

    Vector<Node*> sorted;
    sorted.reserve(n);
    int head = 0;
    while (head < (int)queue.size()) {
        int cur = queue[head++];
        sorted.push_back(children[cur]);
        for (int next : adj[cur]) {
            if (--inDeg[next] == 0)
                queue.push_back(next);
        }
    }
    return sorted;
}

// Sync execution of a single step node: trigger CMD_IMPL, return Result.
static Result execStep(Node* step)
{
    step->trigger<CMD_IMPL>();
    auto* r = step->value().customPtr<Result>();
    return r ? *r : Result::SUCCESS;
}

// ============================================================================
// command:: namespace implementation
// ============================================================================

namespace command {

Node* root()
{
    return ve::n("command");
}

// --- registration ---

Node* reg(const std::string& key, StepFn fn, LoopRef loop)
{
    auto* cmd = root()->ensure(key);
    cmd->clear();
    cmd->connect<CMD_IMPL>(cmd, wrapStep(cmd, std::move(fn)), loop);
    return cmd;
}

Node* reg(const std::string& key, std::initializer_list<StepInfo> steps)
{
    auto* cmd = root()->ensure(key);
    cmd->clear();

    Node* prev = nullptr;
    for (auto& info : steps) {
        auto* step = cmd->append(info.name);
        step->connect<CMD_IMPL>(step, wrapStep(step, info.fn), info.loop);
        if (prev) {
            step->setShadow(prev);
            prev->connect<CMD_DONE>(step, [step](const Var& result) {
                auto* r = result.customPtr<Result>();
                if (r && r->isError()) return;
                step->trigger<CMD_IMPL>(result);
            });
        }
        prev = step;
    }
    return cmd;
}

bool unreg(const std::string& key)
{
    auto* cmd = root()->resolve(key, false);
    if (!cmd || cmd == root()) return false;
    auto* p = cmd->parent();
    return p ? p->remove(cmd) : false;
}

// --- query ---

bool has(const std::string& key)
{
    return root()->resolve(key, false) != nullptr;
}

Strings keys()
{
    return root()->childNames();
}

// --- execution ---

Result call(const std::string& key, const Var& input)
{
    auto* cmd = root()->resolve(key, false);
    if (!cmd) return Result(Result::FAIL, "command not found: " + key);

    cmd->set(input);

    auto children = cmd->children();
    if (children.empty()) {
        return execStep(cmd);
    }

    auto sorted = topoSort(cmd);
    Result last = Result::SUCCESS;
    for (auto* step : sorted) {
        step->set(input);
        Result r = execStep(step);
        if (r.isError()) return r;
        last = r;
    }
    return last;
}

Result exec(const std::string& key, const Var& input)
{
    auto* cmd = root()->resolve(key, false);
    if (!cmd) return Result(Result::FAIL, "command not found: " + key);

    cmd->set(input);

    auto children = cmd->children();
    if (children.empty()) {
        return execStep(cmd);
    }

    // Set input on all step nodes
    for (auto* step : children)
        step->set(input);

    // Trigger root steps (those with no shadow dependency within this command).
    // For async steps (with LoopRef), CMD_DONE chain handles propagation.
    // For sync steps, trigger in topo order directly.
    bool hasAsync = false;
    auto sorted = topoSort(cmd);
    for (auto* step : sorted) {
        auto* sh = step->shadow();
        bool isRoot = !sh || sh->parent() != cmd;
        if (!isRoot && !hasAsync) continue;

        step->trigger<CMD_IMPL>(Var(input));
        auto* r = step->value().customPtr<Result>();
        if (r && r->isError()) return *r;
        if (r && r->isAccepted()) hasAsync = true;
    }

    if (hasAsync) return Result::ACCEPT;

    auto* last = sorted.empty() ? cmd : sorted.back();
    auto* r = last->value().customPtr<Result>();
    return r ? *r : Result::SUCCESS;
}

Result exec(const std::string& key, const Var& input,
            std::function<void(const Result&)> onDone)
{
    auto* cmd = root()->resolve(key, false);
    if (!cmd) return Result(Result::FAIL, "command not found: " + key);

    cmd->set(input);

    auto children = cmd->children();
    if (children.empty()) {
        Result r = execStep(cmd);
        if (onDone) onDone(r);
        return r;
    }

    for (auto* step : children)
        step->set(input);

    // Find the last step in topo order; connect onDone to its CMD_DONE
    auto sorted = topoSort(cmd);
    if (!sorted.empty() && onDone) {
        auto* last = sorted.back();
        auto obs = new Object("_cmd_done_obs");
        last->connect<CMD_DONE>(obs, [onDone, obs](const Var& result) {
            auto* r = result.customPtr<Result>();
            if (onDone) onDone(r ? *r : Result::SUCCESS);
            delete obs;
        });
    }

    // Trigger root steps
    for (auto* step : sorted) {
        auto* sh = step->shadow();
        if (!sh || sh->parent() != cmd) {
            step->trigger<CMD_IMPL>(Var(input));
            auto* r = step->value().customPtr<Result>();
            if (r && r->isError()) {
                if (onDone) onDone(*r);
                return *r;
            }
        }
    }

    return Result::ACCEPT;
}

} // namespace command

} // namespace ve
