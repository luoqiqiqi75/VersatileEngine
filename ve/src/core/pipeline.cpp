// pipeline.cpp — Pipeline: ctx + wiring + scheduling
//
// PR A scope:
//   - ctx top-level envelope: /request /reply /code /message
//   - _pipe/ subtree for framework internal state (cleared on completion for external ctx)
//   - 4 wiring forms (addCtxStep / addLinearStep / addPathStep / addDagStep)
//   - Single-pass synchronous scheduling (linear chain + sync inline)
//   - Alive bug fixed: _alive is a member, allocated once at ctor
//
// Deferred to PR C:
//   - Proper round-based + atomic-pending scheduling
//   - DAG fan-in with shadow-link merge nodes
//   - Async ACCEPT (Result.code > 0) finishStep flow
//   - Cancel during running step (cooperative via _pipe/cancel flag)

#include "ve/core/pipeline.h"
#include "ve/core/node.h"
#include "ve/core/schema.h"
#include "ve/core/factory.h"
#include "ve/core/log.h"

#include <atomic>

namespace ve {

namespace {

enum class Wiring : uint8_t { Ctx, Linear, Path, Dag };

struct StepRuntime
{
    Command         cmd;
    Wiring          wiring = Wiring::Linear;
    LoopRef         loop;
    Node*           in  = nullptr;
    Node*           out = nullptr;
    std::string     in_path;
    std::string     out_path;
    std::vector<int> deps;   // for Dag
};

} // anonymous

struct Pipeline::Private
{
    std::vector<StepRuntime> steps;
    State                    state    = IDLE;
    Node*                    ctx      = nullptr;
    bool                     owns_ctx = false;
    Alive                    alive    = Alive::create();   // fixed: member, not per-post
    bool                     owns_self = true;
    Handler                  handler;
    Result                   last;

    ~Private()
    {
        alive.kill();
        if (owns_ctx && ctx) delete ctx;
    }
};


// ----- construction / destruction --------------------------------------------

Pipeline::Pipeline(const std::string& name)
    : Object(name)
{
    _p->ctx = new Node("_ctx");
    _p->owns_ctx = true;
}

Pipeline::Pipeline(const std::string& name, Node* external_ctx)
    : Object(name)
{
    if (external_ctx) {
        _p->ctx = external_ctx;
        _p->owns_ctx = false;
    } else {
        _p->ctx = new Node("_ctx");
        _p->owns_ctx = true;
    }
}

Pipeline::~Pipeline() = default;


// ----- wiring ----------------------------------------------------------------

Pipeline::Handle Pipeline::addCtxStep(Command cmd, LoopRef loop)
{
    StepRuntime sr;
    sr.cmd     = std::move(cmd);
    sr.wiring  = Wiring::Ctx;
    sr.loop    = std::move(loop);
    int slot = static_cast<int>(_p->steps.size());
    _p->steps.push_back(std::move(sr));
    return {slot};
}

Pipeline::Handle Pipeline::addLinearStep(Command cmd, LoopRef loop)
{
    StepRuntime sr;
    sr.cmd     = std::move(cmd);
    sr.wiring  = Wiring::Linear;
    sr.loop    = std::move(loop);
    int slot = static_cast<int>(_p->steps.size());
    _p->steps.push_back(std::move(sr));
    return {slot};
}

Pipeline::Handle Pipeline::addPathStep(Command cmd,
                                       const std::string& in_path,
                                       const std::string& out_path,
                                       LoopRef loop)
{
    StepRuntime sr;
    sr.cmd      = std::move(cmd);
    sr.wiring   = Wiring::Path;
    sr.in_path  = in_path;
    sr.out_path = out_path;
    sr.loop     = std::move(loop);
    int slot = static_cast<int>(_p->steps.size());
    _p->steps.push_back(std::move(sr));
    return {slot};
}

Pipeline::Handle Pipeline::addDagStep(Command cmd,
                                      std::initializer_list<Handle> deps,
                                      LoopRef loop)
{
    StepRuntime sr;
    sr.cmd     = std::move(cmd);
    sr.wiring  = Wiring::Dag;
    sr.loop    = std::move(loop);
    sr.deps.reserve(deps.size());
    for (auto h : deps) sr.deps.push_back(h.slot);
    int slot = static_cast<int>(_p->steps.size());
    _p->steps.push_back(std::move(sr));
    return {slot};
}


// ----- internal: resolve in/out nodes for a step at run time -----------------

namespace {

void resolveWiring(StepRuntime& sr, int slot, int total, Node* ctx)
{
    switch (sr.wiring) {
    case Wiring::Ctx:
        sr.in = sr.out = ctx;
        break;

    case Wiring::Linear:
        // first step in chain reads ctx/request; otherwise reads previous stage's out
        sr.in = (slot == 0)
            ? ctx->atPath("request", true)
            : ctx->atPath("_pipe/stage", true)->at(slot - 1);
        // last step writes to ctx/reply; otherwise to _pipe/stage/#slot
        sr.out = (slot == total - 1)
            ? ctx->atPath("reply", true)
            : ctx->atPath("_pipe/stage", true)->at(slot);
        break;

    case Wiring::Path:
        sr.in  = ctx->atPath(sr.in_path,  true);
        sr.out = ctx->atPath(sr.out_path, true);
        break;

    case Wiring::Dag:
        // PR C: proper merge node with shadow-link of dep outs.
        // Stub: alias in = out = _pipe/stage/#slot so user proc can still run.
        sr.in  = ctx->atPath("_pipe/stage", true)->at(slot);
        sr.out = ctx->atPath("_pipe/stage", true)->at(slot);
        break;
    }
}

} // anonymous


// ----- start / call ----------------------------------------------------------
//
// PR A: single-pass synchronous execution.
// - resolve each step's wiring
// - run proc inline (Step.post not yet used for async dispatch — PR C)
// - pack Result.data → out via importAs<VarS> (D14)
// - write ctx/code + ctx/message
// - cleanup _pipe/ subtree on external ctx
// - signal CMD_DONE / CMD_ERROR + invoke handler

template<typename CallProtoT>
typename CallProtoT::Output Pipeline::call(const typename CallProtoT::Input& input)
{
    if (!_p->ctx) {
        return CallProtoT::makeFailure(Result::UNKNOWN_CMD, "pipeline ctx is null");
    }

    // 1. import input → ctx envelope
    CallProtoT::import(_p->ctx, input);

    // 2. run steps (sync linear pass for PR A)
    _p->state = RUNNING;
    _p->last  = Result::ok();

    const int total = static_cast<int>(_p->steps.size());
    for (int i = 0; i < total; ++i) {
        if (_p->state != RUNNING) break;

        StepRuntime& sr = _p->steps[i];
        resolveWiring(sr, i, total, _p->ctx);

        Proc p = sr.cmd.proc();
        if (!p) {
            _p->last  = Result::fail(Result::UNKNOWN_CMD,
                "step #" + std::to_string(i) + ": no proc bound");
            _p->state = ERRORED;
            break;
        }

        // synchronous inline execution (PR C: dispatch via Step.post(_alive))
        Result r = p(sr.in, sr.out);

        // D14: Result.data non-null → importAs<VarS> overwrites out
        if (!r.data.isNull() && sr.out) {
            schema::importAs<schema::VarS>(sr.out, r.data);
        }

        _p->last = r;

        if (r.isError()) {
            _p->state = ERRORED;
            break;
        }
        if (r.isAccepted()) {
            // PR C: proper async accept path. For now treat as terminal-success.
            break;
        }
    }

    if (_p->state == RUNNING) _p->state = DONE;

    // 3. pack envelope (code / message; reply is already on /reply via wiring)
    if (_p->ctx) {
        _p->ctx->atPath("code", true)->set(Var(_p->last.code));
        if (!_p->last.message.empty()) {
            _p->ctx->atPath("message", true)->set(Var(_p->last.message));
        }
    }

    // 4. signal + handler
    if (_p->state == DONE) {
        trigger<CMD_DONE>(Var::custom(_p->last));
    } else if (_p->state == ERRORED) {
        trigger<CMD_ERROR>(Var::custom(_p->last));
    }
    if (_p->handler) {
        auto h = std::move(_p->handler);
        Result snapshot = _p->last;
        h(snapshot);
    }

    // 5. export output + 6. cleanup _pipe/ for external ctx (envelope kept).
    //    Split on void output (NodeInOut) — can't store/return a void value.
    if constexpr (std::is_void_v<typename CallProtoT::Output>) {
        CallProtoT::exportOut(_p->ctx);
        if (!_p->owns_ctx && _p->ctx) {
            _p->ctx->remove("_pipe");
        }
        return;
    } else {
        auto out = CallProtoT::exportOut(_p->ctx);
        if (!_p->owns_ctx && _p->ctx) {
            _p->ctx->remove("_pipe");
        }
        return out;
    }
}

// Explicit instantiations for the standard 4 CallProto specs.
template Var    Pipeline::call<CallProto<tag::VarInVarOut>>  (const Var&);
template Result Pipeline::call<CallProto<tag::RequestReply>> (const Var&);
template Result Pipeline::call<CallProto<tag::ListInDictOut>>(const Var::ListV&);
template void   Pipeline::call<CallProto<tag::NodeInOut>>    (const CallProto<tag::NodeInOut>::Input&);


// ----- convenience family ----------------------------------------------------

Result Pipeline::callReply(const Var& input)        { return call<CallProto<tag::RequestReply>> (input); }
Var    Pipeline::callVar  (const Var& input)        { return call<CallProto<tag::VarInVarOut>>  (input); }
Result Pipeline::callList (const Var::ListV& input) { return call<CallProto<tag::ListInDictOut>>(input); }
void   Pipeline::callNode (Node* in, Node* out)     {        call<CallProto<tag::NodeInOut>>    ({in, out}); }


// ----- control ---------------------------------------------------------------

void Pipeline::onFinished(Handler h) { _p->handler = std::move(h); }

void Pipeline::cancel()
{
    if (_p->state != RUNNING) return;
    if (_p->ctx) _p->ctx->atPath("_pipe/cancel", true)->set(Var(true));
    _p->state = ERRORED;
    _p->last  = Result::fail(Result::CANCELLED, "cancelled");
}

void Pipeline::keepAlive() { _p->owns_self = false; }

void Pipeline::finishStep(Handle h, Result r)
{
    // PR C: proper async finishStep (resume pending dispatch via atomic counters).
    // PR A stub: just record the result.
    (void)h;
    _p->last = std::move(r);
}


// ----- query -----------------------------------------------------------------

Node*           Pipeline::context()  const { return _p->ctx; }
Pipeline::State Pipeline::state()    const { return _p->state; }
int             Pipeline::stepCount()const { return static_cast<int>(_p->steps.size()); }

Node* Pipeline::outputOf(Handle h) const
{
    if (h.slot < 0 || h.slot >= static_cast<int>(_p->steps.size())) return nullptr;
    return _p->steps[h.slot].out;
}

const Result& Pipeline::lastResult() const { return _p->last; }

} // namespace ve
