// pipeline.h - transient command/proc execution graph
#pragma once

#include "command.h"

#include <functional>
#include <memory>

namespace ve {

// Pipeline is a copyable handle over a shared execution graph.
// It does NOT inherit Object (so it stays freely copyable/movable); completion
// is reported purely through the Callback. The shared Private keeps the graph
// alive across asynchronous (loop-bound) steps with no manual ownership.
class VE_API Pipeline
{
public:
    using Handle = Node*;
    using Callback = std::function<void(Pipeline&)>;

    enum StateSignal : int
    {
        IDLE        =   0x00,   // no emit
        PREPARE     =   0x10,
        STARTED     =   0x10,
        CANCELLED   =   0x11,
        FINISHED    =   0x20,
        ERRORED     =   0x40
    };

public:
    explicit Pipeline(const Node* ctx = nullptr);
    ~Pipeline();

    Node* contextNode() const;
    Node* inputNode() const;
    Node* outputNode() const;

    // command link
    Handle add(Command command);
    Handle addProc(Proc proc, Loop* loop = nullptr);

    // state control
    void cancel();

    template<StateSignal SS> void on(Object* observer, Callback cb, Loop* loop = nullptr);

    void onPrepare(Object* observer, Callback cb, Loop* loop = nullptr) { on<PREPARE>(observer, cb, loop); }
    void onStarted(Object* observer, Callback cb, Loop* loop = nullptr) { on<STARTED>(observer, cb, loop); }
    void onCanceled(Object* observer, Callback cb, Loop* loop = nullptr) { on<CANCELLED>(observer, cb, loop); }
    void onFinished(Object* observer, Callback cb, Loop* loop = nullptr) { on<FINISHED>(observer, cb, loop); }
    void onErrored(Object* observer, Callback cb, Loop* loop = nullptr) { on<ERRORED>(observer, cb, loop); }

    // exec
    void async();
    void sync(Loop* cur_l = nullptr);

    const Result& result() const;

private:
    VE_DECLARE_SHARED_PRIVATE
};

namespace pipeline {

// Take ownership of a pipeline and run it without blocking the caller: the first
// dispatch is posted to `driver` (defaults to loop::main()), and the shared graph
// keeps itself alive until completion, then frees automatically. If cb is given
// it becomes the completion callback; otherwise any onFinished callback is kept.
// VE_API void async(Pipeline&& p, Loop* driver = nullptr, Pipeline::Callback cb = {});

} // namespace pipeline

} // namespace ve
