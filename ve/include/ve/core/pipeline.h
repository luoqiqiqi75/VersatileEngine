// ----------------------------------------------------------------------------
// pipeline.h — Pipeline: ctx + wiring + scheduling
// ----------------------------------------------------------------------------
// Copyright (c) 2023-present Thilo and VersatileEngine contributors.
// Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
// See LICENSE file in the project root for full license information.
// ----------------------------------------------------------------------------
//
// Pipeline runs N Commands wired together against a shared ctx.
//
// ctx top-level layout (locked 2026-05-21):
//   /request   /reply   /code   /message       — 4 envelope fields (业务可常驻)
//   _pipe/{stage,cancel,event,trace,session}   — framework internal state (user禁碰)
//   <user keys>                                — free; no underscore prefix
//
// Wiring forms (4, method-name disambiguated — D6):
//   addCtxStep   in = out = ctx       (initial Procedure style)
//   addLinearStep in = prev out, out = _pipe/stage/#i   (chain)
//   addPathStep   in / out at explicit ctx paths
//   addDagStep    in = merge node of deps, out = _pipe/stage/#i (DAG fan-in)
//
// State machine:  IDLE → RUNNING → (DONE | ERRORED)
//

#pragma once

#include "command.h"

namespace ve {

class VE_API Pipeline : public Object
{
public:
    enum State : uint8_t { IDLE, RUNNING, DONE, ERRORED };

    enum Signal : SignalT {
        CMD_DONE  = 0xFFFF'0030,
        CMD_ERROR = 0xFFFF'0031,
    };

    // Handle returned by add*Step; used to wire DAG deps + query output node.
    struct Handle {
        int slot = -1;
        explicit operator bool() const { return slot >= 0; }
        bool operator==(Handle o) const { return slot == o.slot; }
    };


    // --- construction ---
    //
    // Default: own ctx (allocated as "_ctx" Node, deleted by dtor).
    // External ctx: Pipeline does NOT delete it; on call end, only `_pipe/` is removed
    // (the 4 envelope fields /request /reply /code /message remain for caller inspection).

    explicit Pipeline(const std::string& name = "");
    Pipeline(const std::string& name, Node* external_ctx);
    ~Pipeline();

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;


    // --- wiring (D6: 4 forms, method-name disambiguated) ---

    // in = out = ctx; for NodeAction-style procs.
    Handle addCtxStep(Command cmd, Loop loop = {});

    // in = previous stage's out (first stage: ctx/request);
    // out = ctx/_pipe/stage/#i  (last stage: ctx/reply on call completion).
    Handle addLinearStep(Command cmd, Loop loop = {});

    // Explicit ctx paths (ensure-exists). Any path — envelope, _pipe/, or user.
    Handle addPathStep(Command cmd,
                       const std::string& in_path,
                       const std::string& out_path,
                       Loop loop = {});

    // DAG fan-in: in node = merge of deps' outs (shadow link); out = ctx/_pipe/stage/#i.
    Handle addDagStep(Command cmd, std::initializer_list<Handle> deps, Loop loop = {});


    // --- start / call ---
    //
    // Full form (explicit CallProto tag):
    //   pipe.call<CallProto<tag::RequestReply>>(input)

    template<typename CallProtoT>
    typename CallProtoT::Output call(const typename CallProtoT::Input& input);

    // Convenience family (different names; no overload ambiguity).
    Result  callReply(const Var& input);
    Var     callVar  (const Var& input = {});
    Result  callList (const Var::ListV& input);
    void    callNode (Node* in, Node* out);


    // --- completion callback ---
    using Handler = std::function<void(const Result&)>;
    void onFinished(Handler h);


    // --- control ---
    void cancel();
    void keepAlive();   // Default: Pipeline self-deletes on completion. After keepAlive(),
                        // caller owns the lifetime (must `delete pipe` once finished).


    // --- async progress (proc returned Result with code == ACCEPT) ---
    //
    // External code that owns the async work calls this to push the pipeline forward.
    void finishStep(Handle h, Result r);


    // --- query ---
    Node*       context() const;
    State       state()   const;
    int         stepCount() const;
    Node*       outputOf(Handle h) const;
    const Result& lastResult() const;

private:
    VE_DECLARE_POOL_PRIVATE
};

} // namespace ve
