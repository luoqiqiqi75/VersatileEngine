// pipeline.h - transient command/proc execution graph
#pragma once

#include "command.h"

#include <functional>
#include <memory>

namespace ve {

class Pipeline;

namespace pipeline {
// (declared again below with defaults; this form is what Pipeline friends)
VE_API void async(Pipeline&& p, Loop* driver, std::function<void(Pipeline&)> cb);
} // namespace pipeline

// Pipeline is a copyable handle over a shared execution graph.
// It does NOT inherit Object (so it stays freely copyable/movable); completion
// is reported purely through the Callback. The shared Private keeps the graph
// alive across asynchronous (loop-bound) steps with no manual ownership.
class VE_API Pipeline
{
public:
    enum State : uint8_t { IDLE, RUNNING, DONE, ERRORED };

    enum Error : int {
        ERR_NONE = 0,
        ERR_NO_CTX,
        ERR_NO_STEP_PROC,
        ERR_CANCELLED,
        ERR_EXCEPTION
    };

    using Callback = std::function<void(Pipeline&)>;

    explicit Pipeline(const Node* initialCtx = nullptr);
    ~Pipeline();

    Pipeline(const Pipeline&) = default;
    Pipeline& operator=(const Pipeline&) = default;
    Pipeline(Pipeline&&) = default;
    Pipeline& operator=(Pipeline&&) = default;

    void addProc(Proc proc, const std::string& inPath, const std::string& outPath,
                 Loop* loop = nullptr);
    void addCommand(Command command, Loop* loop = nullptr);
    void addCtxProc(Proc proc, Loop* loop = nullptr);
    void addLinearProc(Proc proc, Loop* loop = nullptr);

    void onFinished(Callback cb);

    // Blocking run on the calling thread. Loop-less steps complete inline; if a
    // step hops to another loop, the calling thread pumps `driver` (or yields)
    // until the graph finishes. Pass the loop driving the current thread.
    void sync(Loop* driver = nullptr);

    void cancel();

    Node* context() const;
    State state() const;
    const Result& lastResult() const;

private:
    friend void pipeline::async(Pipeline&&, Loop*, std::function<void(Pipeline&)>);

    void run(Loop* driver, bool deferFirst);
    void dispatch(int slot);
    void complete(State state);

    VE_DECLARE_SHARED_PRIVATE
};

namespace pipeline {

// Take ownership of a pipeline and run it without blocking the caller: the first
// dispatch is posted to `driver` (defaults to loop::main()), and the shared graph
// keeps itself alive until completion, then frees automatically. If cb is given
// it becomes the completion callback; otherwise any onFinished callback is kept.
VE_API void async(Pipeline&& p, Loop* driver = nullptr, Pipeline::Callback cb = {});

} // namespace pipeline

} // namespace ve
