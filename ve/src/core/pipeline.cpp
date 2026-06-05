// pipeline.cpp - Pipeline: ctx + wiring + scheduling

#include "ve/core/pipeline.h"
#include "ve/core/node.h"
#include "ve/core/schema.h"
#include "ve/core/factory.h"
#include "ve/core/log.h"

#include <algorithm>
#include <atomic>
#include <thread>

namespace ve {

namespace {

enum class Wiring : uint8_t { Ctx, Linear, Path, Dag };

struct StepRuntime
{
    Command          cmd;
    Wiring           wiring = Wiring::Linear;
    Loop*            loop = nullptr;
    Node*            in = nullptr;
    Node*            out = nullptr;
    std::string      in_path;
    std::string      out_path;
    std::vector<int> deps;
};

Loop* resolveLoop(const Command& cmd, Loop* explicit_loop)
{
    return explicit_loop ? explicit_loop : cmd.loop();
}

Loop* dispatchLoop(const StepRuntime& sr)
{
    return sr.loop;
}

void resolveWiring(StepRuntime& sr, int slot, int total, Node* ctx)
{
    if (Node* bind_in = ctx->find("_pipe/_bind_in", false)) {
        sr.in = static_cast<Node*>(bind_in->get().toPointer());
        sr.out = nullptr;
        if (Node* bind_out = ctx->find("_pipe/_bind_out", false)) {
            sr.out = static_cast<Node*>(bind_out->get().toPointer());
        }
        return;
    }

    switch (sr.wiring) {
    case Wiring::Ctx:
        sr.in = sr.out = ctx;
        break;

    case Wiring::Linear:
        sr.in = (slot == 0)
            ? ctx->at("request")
            : ctx->at("_pipe/stage")->at(slot - 1);
        sr.out = (slot == total - 1)
            ? ctx->at("reply")
            : ctx->at("_pipe/stage")->at(slot);
        break;

    case Wiring::Path:
        sr.in  = ctx->at(sr.in_path);
        sr.out = ctx->at(sr.out_path);
        break;

    case Wiring::Dag:
        // PR C: proper merge node with shadow links of dependency outputs.
        sr.in = ctx->at("_pipe/stage")->at(slot);
        sr.out = sr.in;
        break;
    }
}

} // namespace

struct Pipeline::Private
{
    std::vector<StepRuntime> steps;
    State                    state = IDLE;
    Node*                    ctx = nullptr;
    bool                     owns_ctx = false;
    std::atomic<bool>        completed{false};
    Handler                  handler;
    Result                   last;

    ~Private()
    {
        if (owns_ctx && ctx) delete ctx;
    }
};


// ----- construction / destruction -----------------------------------------

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


// ----- wiring ---------------------------------------------------------------

Pipeline::Handle Pipeline::addCtxStep(Command cmd, Loop* loop)
{
    StepRuntime sr;
    sr.loop = resolveLoop(cmd, loop);
    sr.cmd = std::move(cmd);
    sr.wiring = Wiring::Ctx;
    int slot = static_cast<int>(_p->steps.size());
    _p->steps.push_back(std::move(sr));
    return {slot};
}

Pipeline::Handle Pipeline::addLinearStep(Command cmd, Loop* loop)
{
    StepRuntime sr;
    sr.loop = resolveLoop(cmd, loop);
    sr.cmd = std::move(cmd);
    sr.wiring = Wiring::Linear;
    int slot = static_cast<int>(_p->steps.size());
    _p->steps.push_back(std::move(sr));
    return {slot};
}

Pipeline::Handle Pipeline::addPathStep(Command cmd,
                                       const std::string& in_path,
                                       const std::string& out_path,
                                       Loop* loop)
{
    StepRuntime sr;
    sr.loop = resolveLoop(cmd, loop);
    sr.cmd = std::move(cmd);
    sr.wiring = Wiring::Path;
    sr.in_path = in_path;
    sr.out_path = out_path;
    if (_p->ctx && !in_path.empty()) {
        if (Node* decl = sr.cmd.declare()) {
            _p->ctx->at(in_path)->setShadow(decl);
        }
    }
    int slot = static_cast<int>(_p->steps.size());
    _p->steps.push_back(std::move(sr));
    return {slot};
}

Pipeline::Handle Pipeline::addDagStep(Command cmd,
                                      std::initializer_list<Handle> deps,
                                      Loop* loop)
{
    StepRuntime sr;
    sr.loop = resolveLoop(cmd, loop);
    sr.cmd = std::move(cmd);
    sr.wiring = Wiring::Dag;
    sr.deps.reserve(deps.size());
    for (auto h : deps) sr.deps.push_back(h.slot);
    int slot = static_cast<int>(_p->steps.size());
    _p->steps.push_back(std::move(sr));
    return {slot};
}


// ----- start / dispatch -----------------------------------------------------

void Pipeline::startReply(const Var& input)
{
    if (!_p->ctx) {
        _p->last = Result::fail(Result::UNKNOWN_CMD, "pipeline ctx is null");
        complete(ERRORED);
        return;
    }

    CallProto<tag::RequestReply>::import(_p->ctx, input);
    startPrepared();
}

void Pipeline::start()
{
    if (!_p->ctx) {
        _p->last = Result::fail(Result::UNKNOWN_CMD, "pipeline ctx is null");
        complete(ERRORED);
        return;
    }

    startPrepared();
}

void Pipeline::startPrepared()
{
    _p->completed.store(false, std::memory_order_release);
    _p->state = RUNNING;
    _p->last = Result::ok();

    if (_p->steps.empty()) {
        complete(DONE);
        return;
    }

    dispatchStep(0);
}

void Pipeline::dispatchStep(int slot)
{
    const int total = static_cast<int>(_p->steps.size());
    if (slot >= total) {
        complete(DONE);
        return;
    }

    StepRuntime& sr = _p->steps[slot];
    resolveWiring(sr, slot, total, _p->ctx);

    Proc p = sr.cmd.proc();
    if (!p) {
        _p->last = Result::fail(Result::UNKNOWN_CMD,
            "step #" + std::to_string(slot) + ": no proc bound");
        complete(ERRORED);
        return;
    }

    auto run = [this, slot, p = std::move(p)]() mutable {
        if (_p->state != RUNNING) {
            complete(_p->state == ERRORED ? ERRORED : DONE);
            return;
        }

        StepRuntime& sr = _p->steps[slot];
        Result r;
        try {
            r = p(sr.in, sr.out);
        } catch (const std::exception& e) {
            r = Result::fail(Result::EXCEPTION, e.what());
        } catch (...) {
            r = Result::fail(Result::EXCEPTION, "unknown exception");
        }

        if (_p->state != RUNNING) {
            complete(_p->state == ERRORED ? ERRORED : DONE);
            return;
        }

        if (!r.data.isNull() && sr.out) {
            schema::importAs<schema::VarS>(sr.out, r.data);
        }

        _p->last = r;
        if (r.isError()) {
            complete(ERRORED);
            return;
        }
        if (r.isAccepted()) {
            complete(DONE);
            return;
        }

        dispatchStep(slot + 1);
    };

    if (Loop* loop = dispatchLoop(sr)) {
        loop->post(std::move(run));
    } else {
        run();
    }
}

void Pipeline::complete(State final_state)
{
    _p->state = final_state;

    if (_p->ctx) {
        _p->ctx->set("code", _p->last.code);
        if (!_p->last.message.empty()) {
            _p->ctx->set("message", _p->last.message);
        }
        if (_p->last.data.isNull()) {
            Var reply = schema::exportAs<schema::VarS>(_p->ctx->at("reply"));
            if (!reply.isNull()) {
                _p->last.data = std::move(reply);
            }
        }
        if (!_p->owns_ctx) {
            _p->ctx->remove("_pipe");
        }
    }

    Result snapshot = _p->last;
    Handler h = std::move(_p->handler);

    if (_p->state == DONE) {
        trigger<CMD_DONE>(Var::custom(snapshot));
    } else if (_p->state == ERRORED) {
        trigger<CMD_ERROR>(Var::custom(snapshot));
    }

    _p->completed.store(true, std::memory_order_release);
    if (h) h(snapshot);
}


// ----- synchronous call -----------------------------------------------------

template<typename CallProtoT>
typename CallProtoT::Output Pipeline::call(const typename CallProtoT::Input& input)
{
    if (!_p->ctx) {
        return CallProtoT::makeFailure(Result::UNKNOWN_CMD, "pipeline ctx is null");
    }

    CallProtoT::import(_p->ctx, input);
    startPrepared();

    std::vector<Loop*> loops;
    loops.reserve(_p->steps.size());
    for (auto& sr : _p->steps) {
        Loop* loop = dispatchLoop(sr);
        if (loop && std::find(loops.begin(), loops.end(), loop) == loops.end()) {
            loops.push_back(loop);
        }
    }

    while (!_p->completed.load(std::memory_order_acquire)) {
        size_t processed = 0;
        for (auto* loop : loops) {
            processed += loop->processEvents();
        }
        if (processed == 0) {
            std::this_thread::yield();
        }
    }

    if constexpr (std::is_void_v<typename CallProtoT::Output>) {
        CallProtoT::exportOut(_p->ctx);
        return;
    } else {
        return CallProtoT::exportOut(_p->ctx);
    }
}

template Var    Pipeline::call<CallProto<tag::VarInVarOut>>  (const Var&);
template Result Pipeline::call<CallProto<tag::RequestReply>> (const Var&);
template Result Pipeline::call<CallProto<tag::ListInDictOut>>(const Var::ListV&);
template void   Pipeline::call<CallProto<tag::NodeInOut>>    (const CallProto<tag::NodeInOut>::Input&);


// ----- convenience family ---------------------------------------------------

Result Pipeline::callReply(const Var& input)        { return call<CallProto<tag::RequestReply>> (input); }
Var    Pipeline::callVar  (const Var& input)        { return call<CallProto<tag::VarInVarOut>>  (input); }
Result Pipeline::callList (const Var::ListV& input) { return call<CallProto<tag::ListInDictOut>>(input); }
void   Pipeline::callNode (Node* in, Node* out)     {        call<CallProto<tag::NodeInOut>>    ({in, out}); }


// ----- control --------------------------------------------------------------

void Pipeline::onFinished(Handler h) { _p->handler = std::move(h); }

void Pipeline::cancel()
{
    if (_p->state != RUNNING) return;
    if (_p->ctx) _p->ctx->set("_pipe/cancel", true);
    _p->state = ERRORED;
    _p->last = Result::fail(Result::CANCELLED, "cancelled");
}

void Pipeline::keepAlive() {}

void Pipeline::finishStep(Handle h, Result r)
{
    (void)h;
    _p->last = std::move(r);
}


// ----- query ----------------------------------------------------------------

Node* Pipeline::context() const { return _p->ctx; }
Pipeline::State Pipeline::state() const { return _p->state; }
int Pipeline::stepCount() const { return static_cast<int>(_p->steps.size()); }

Node* Pipeline::outputOf(Handle h) const
{
    if (h.slot < 0 || h.slot >= static_cast<int>(_p->steps.size())) return nullptr;
    return _p->steps[h.slot].out;
}

const Result& Pipeline::lastResult() const { return _p->last; }

} // namespace ve
