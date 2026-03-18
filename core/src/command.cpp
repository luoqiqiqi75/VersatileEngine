// command.cpp — ve::Result, ve::Command, ve::command:: implementation
#include "ve/core/command.h"
#include "ve/core/node.h"

#include <chrono>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

namespace ve {

// ============================================================================
// Result
// ============================================================================

std::string Result::toString() const
{
    if (isSuccess()) return "success";
    if (isAccepted()) return "accepted";
    std::string s = "error(" + std::to_string(_code) + ")";
    if (!_text.empty()) s += ": " + _text;
    return s;
}

// ============================================================================
// CommandFactory singleton
// ============================================================================

static CommandFactory& factory()
{
    static auto* s = new CommandFactory("command");
    return *s;
}

CommandFactory& commandFactory() { return factory(); }

// ============================================================================
// Command — lifecycle
// ============================================================================

Command::Command(const std::string& key) : _key(key)
{
    _data = Var(Var::DictV{});
}

Command::~Command() = default;

// ============================================================================
// Command — data storage
// ============================================================================

Var Command::data(const std::string& name) const
{
    if (!_data.isDict()) return Var();
    auto& d = _data.toDict();
    return d.has(name) ? d[name] : Var();
}

void Command::setData(const std::string& name, const Var& v)
{
    if (!_data.isDict()) _data = Var(Var::DictV{});
    _data.toDict()[name] = v;
}

bool Command::hasData(const std::string& name) const
{
    return _data.isDict() && _data.toDict().has(name);
}

// ============================================================================
// Command — step chain
// ============================================================================

void Command::addStep(const Step& step)  { _steps.push_back(step); }
void Command::addStep(Step&& step)       { _steps.push_back(std::move(step)); }
void Command::prependStep(const Step& step) { _steps.insert(_steps.begin(), step); }

std::string Command::currentStepName() const
{
    if (_step_idx >= 0 && _step_idx < (int)_steps.size())
        return _steps[_step_idx].name();
    return "";
}

int64_t Command::elapsedUs() const
{
    if (_start_time == 0) return 0;
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(now).count() - _start_time;
}

// ============================================================================
// Command — execution
// ============================================================================

Result Command::start()
{
    if (_running) return Result(Result::FAIL, "already running");
    _running  = true;
    _finished = false;
    _step_idx = 0;
    _start_time = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    if (_steps.empty()) {
        finish(Result::SUCCESS);
        return Result::SUCCESS;
    }
    return runCurrentStep();
}

Result Command::runCurrentStep()
{
    if (_step_idx >= (int)_steps.size()) {
        finish(Result::SUCCESS);
        return Result::SUCCESS;
    }

    auto& step = _steps[_step_idx];
    if (step.loop()) {
        step.loop().post(Alive::create(), [this, &step]() {
            Result r = step(*this);
            advance(r);
        });
        return Result::ACCEPT;
    }

    Result r = step(*this);
    if (r.isAccepted()) return r;
    advance(r);
    return r;
}

void Command::advance(const Result& res)
{
    if (res.isError()) {
        finish(res);
        return;
    }
    ++_step_idx;
    if (_step_idx >= (int)_steps.size()) {
        finish(res);
    } else {
        runCurrentStep();
    }
}

void Command::finish(const Result& result)
{
    _running  = false;
    _finished = true;
    if (_result_handler) _result_handler(result);
}

// ============================================================================
// Node integration helpers
// ============================================================================

static Node* registryNode()
{
    return ve::n("command/registry");
}

static Node* runningNode()
{
    return ve::n("command/running");
}

static std::atomic<int64_t> s_cmdId{0};

static void syncRegistryNode(const std::string& key, bool add)
{
    auto* reg = registryNode();
    if (add) {
        if (!reg->child(key)) reg->append(key)->set(Var(true));
    } else {
        reg->erase(key);
    }
}

static Node* addRunningEntry(const std::string& cmdKey)
{
    int64_t id = s_cmdId.fetch_add(1, std::memory_order_relaxed);
    std::string idStr = std::to_string(id);
    auto* entry = runningNode()->append(idStr);
    entry->set("key", Var(cmdKey));
    entry->set("step", Var(0));
    return entry;
}

static void removeRunningEntry(Node* entry)
{
    if (!entry) return;
    auto* p = entry->parent();
    if (p) p->remove(entry);
}

// ============================================================================
// command:: namespace — registration (delegates to CommandFactory)
// ============================================================================

namespace command {

static std::function<Result(Command&)> stepsToFn(Vector<Step> steps)
{
    return [s = std::move(steps)](Command& cmd) -> Result {
        for (auto& step : s) cmd.addStep(step);
        return cmd.start();
    };
}

void reg(const std::string& key, const Vector<Step>& steps)
{
    factory().insertOne(key, stepsToFn(steps));
    syncRegistryNode(key, true);
}

void reg(const std::string& key, std::initializer_list<Step> steps)
{
    reg(key, Vector<Step>(steps));
}

bool unreg(const std::string& key)
{
    bool ok = factory().erase(key);
    if (ok) syncRegistryNode(key, false);
    return ok;
}

// ============================================================================
// command:: namespace — query
// ============================================================================

bool has(const std::string& key)
{
    return factory().has(key);
}

Strings keys()
{
    Strings out;
    for (auto& kv : factory())
        out.push_back(kv.key);
    return out;
}

// ============================================================================
// command:: namespace — simple call (zero Command allocation)
// ============================================================================

Result call(const std::string& key, const Var& input)
{
    if (!factory().has(key))
        return Result(Result::FAIL, "command not found: " + key);

    auto cmd = std::make_unique<Command>(key);
    if (!input.isNull()) cmd->setInput(input);

    auto& fn = factory()[key];
    return fn(*cmd);
}

// ============================================================================
// command:: namespace — full execution
// ============================================================================

Command* create(const std::string& key)
{
    if (!factory().has(key)) return nullptr;
    auto* cmd = new Command(key);
    auto& fn = factory()[key];
    fn(*cmd);
    return cmd;
}

Command* create(const std::string& key, const Command::ResultHandler& handler)
{
    auto* cmd = create(key);
    if (cmd) cmd->setResultHandler(handler);
    return cmd;
}

Result exec(const std::string& key)
{
    return call(key);
}

Result exec(const std::string& key, const Var& input)
{
    return call(key, input);
}

Result exec(const std::string& key, const Var& input,
            const Command::ResultHandler& handler)
{
    if (!factory().has(key))
        return Result(Result::FAIL, "command not found: " + key);

    auto* cmd = new Command(key);
    cmd->setInput(input);
    cmd->setResultHandler([handler, cmd](const Result& r) {
        if (handler) handler(r);
        delete cmd;
    });

    auto& fn = factory()[key];
    return fn(*cmd);
}

} // namespace command

// ============================================================================
// CommandGraph — DAG execution
// ============================================================================

void CommandGraph::addNode(const std::string& id, Step step)
{
    _nodes.push_back({id, std::move(step), {}});
}

void CommandGraph::addEdge(const std::string& from, const std::string& to)
{
    _edges.push_back({from, to});
    for (auto& n : _nodes) {
        if (n.id == to) {
            n.deps.push_back(from);
            break;
        }
    }
}

Result CommandGraph::execute(LoopRef loop)
{
    if (_nodes.empty()) return Result::SUCCESS;

    // Build in-degree map and adjacency list
    Dict<int> inDegree;
    Dict<Strings> adj;
    for (auto& n : _nodes) inDegree[n.id] = 0;

    for (auto& e : _edges) {
        inDegree[e.second] = inDegree.has(e.second) ? inDegree[e.second] + 1 : 1;
        if (!adj.has(e.first)) adj[e.first] = {};
        adj[e.first].push_back(e.second);
    }

    // Find nodes by id for quick lookup
    Dict<Step*> stepMap;
    for (auto& n : _nodes) stepMap[n.id] = &n.step;

    // Topological sort + execution
    Strings ready;
    for (auto& kv : inDegree) {
        if (kv.value == 0) ready.push_back(kv.key);
    }

    int completed = 0;
    int total = (int)_nodes.size();
    Result finalResult = Result::SUCCESS;

    auto graphCmd = std::make_unique<Command>("_graph");

    while (!ready.empty() && finalResult.isSuccess()) {
        // Execute all ready nodes
        Strings nextReady;

        if (loop && ready.size() > 1) {
            // Parallel dispatch
            std::atomic<int> pending(static_cast<int>(ready.size()));
            std::atomic<bool> failed(false);
            Result firstError;
            std::mutex errMtx;

            for (auto& id : ready) {
                auto* step = stepMap[id];
                loop.post(Alive::create(), [&, step, id]() {
                    if (failed.load(std::memory_order_acquire)) {
                        pending.fetch_sub(1, std::memory_order_release);
                        return;
                    }
                    Result r = (*step)(*graphCmd);
                    if (r.isError()) {
                        std::lock_guard<std::mutex> lk(errMtx);
                        if (!failed.exchange(true, std::memory_order_release))
                            firstError = r;
                    }
                    pending.fetch_sub(1, std::memory_order_release);
                });
            }

            // Spin-wait for completion (simplistic; sufficient for most use cases)
            while (pending.load(std::memory_order_acquire) > 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }

            if (failed.load()) {
                finalResult = firstError;
                break;
            }
        } else {
            // Sequential execution
            for (auto& id : ready) {
                auto* step = stepMap[id];
                Result r = (*step)(*graphCmd);
                if (r.isError()) {
                    finalResult = r;
                    break;
                }
            }
            if (finalResult.isError()) break;
        }

        completed += (int)ready.size();

        // Update in-degrees
        for (auto& id : ready) {
            if (adj.has(id)) {
                for (auto& next : adj[id]) {
                    inDegree[next]--;
                    if (inDegree[next] == 0)
                        nextReady.push_back(next);
                }
            }
        }
        ready = std::move(nextReady);
    }

    if (finalResult.isSuccess() && completed < total) {
        return Result(Result::FAIL, "cycle detected in command graph");
    }

    return finalResult;
}

} // namespace ve
