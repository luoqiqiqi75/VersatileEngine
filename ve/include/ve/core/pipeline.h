// pipeline.h - transient command/proc execution graph
#pragma once

#include "command.h"

namespace ve {

class VE_API Pipeline : public Object
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

    enum Signal : SignalT {
        DONE_SIGNAL  = 0xFFFF'0030,
        ERROR_SIGNAL = 0xFFFF'0031,
    };

    struct Handle {
        int slot = -1;
        explicit operator bool() const { return slot >= 0; }
    };

    explicit Pipeline(const std::string& name = {});
    Pipeline(const std::string& name, const Node* initialCtx);
    ~Pipeline();

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    Handle addProc(Proc proc, const std::string& inPath, const std::string& outPath,
                   Loop* loop = nullptr);
    Handle addCommand(Command command, Loop* loop = nullptr);
    Handle addCtxProc(Proc proc, Loop* loop = nullptr);
    Handle addLinearProc(Proc proc, Loop* loop = nullptr);

    void start();
    void cancel();
    void wait();

    using Callback = std::function<void(Pipeline&)>;
    void onFinished(Callback cb);

    Node* context() const;
    State state() const;
    const Result& lastResult() const;
    Node* outputOf(Handle handle) const;

private:
    void dispatch(int slot);
    void complete(State state);

private:
    VE_DECLARE_POOL_PRIVATE
};

namespace pipeline {

VE_API void start(Pipeline& p, Pipeline::Callback cb = {});
VE_API void start(Pipeline* p, Pipeline::Callback cb = {});

} // namespace pipeline

} // namespace ve
