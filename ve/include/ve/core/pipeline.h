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
    using Callback = std::function<void(Pipeline&)>;

    enum StateCode : int
    {
        CANCELED    =   0x10,
    };

    enum StateSignal : int
    {
        STARTED     =   0x10,
        FINISHED    =   0x20
    };

public:
    explicit Pipeline(const Node* ctx = nullptr);
    ~Pipeline();

    Node* contextNode() const;
    Node* inputNode() const;
    Node* outputNode() const;

    // command link
    Command* add(Command command);
    Command* addProc(Proc proc, Loop* loop = nullptr);

    // state control
    void cancel() const;

    template<StateSignal SS> void on(Object* observer, Callback cb, Loop* loop = nullptr)
    {
        auto action = [self = *this, cb = std::move(cb)] {
            Pipeline p = std::move(self);
            cb(p);
        };
        object()->connect<SS>(observer, action, loop);
    }

    void onStarted(Object* observer, Callback cb, Loop* loop = nullptr);
    void onFinished(Object* observer, Callback cb, Loop* loop = nullptr);

    // exec
    void async() const;
    void sync(Loop* cur_l = nullptr) const;

    const Result& result() const;

protected:
    Object* object() const;

private:
    VE_DECLARE_SHARED_PRIVATE
};

namespace pipeline {



} // namespace pipeline

} // namespace ve
