// ----------------------------------------------------------------------------
// command.h — Proc, Command, RegProto / CallProto, namespace ve::command
// ----------------------------------------------------------------------------
// Copyright (c) 2023-present Thilo and VersatileEngine contributors.
// Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
// See LICENSE file in the project root for full license information.
// ----------------------------------------------------------------------------
//
// Four-layer abstraction (read 00-overview / 02-proc-command for full design):
//
//   Step       (loop.h)        — function<void()>; loop scheduling primitive
//   Proc                       — function<Result(Node* in, Node* out)>; processing unit
//   Command  = Proc + Schema   — factory-backed, independently callable
//   Pipeline = N Commands + Wiring (pipeline.h)
//
//   RegProto<tag::X>  — user callable -> Proc wrap (compile-time signature check)
//   CallProto<tag::Y> — external Input/Output <-> ctx envelope conversion
//
// ctx top-level layout (locked 2026-05-21):
//   /request   /reply   /code   /message       — 4 envelope fields (business-visible, can persist)
//   _pipe/{stage,cancel,event,trace,session}   — framework-internal state (user禁碰; clean on call end)
//   <user keys>                                — free; no underscore prefix, no collision with envelope
//

#pragma once

#include "loop.h"
#include "node.h"
#include "var.h"
#include "schema.h"
#include "factory.h"

namespace ve {

class Pipeline;
class Command;


// ============================================================================
// Proc — core processing primitive (2026-05-25 simplified to 2-node)
// ============================================================================
//
// Signature: Result(Node* in, Node* out)
//
//   in    Single-call input node;  wired by Pipeline (ctx/request, _pipe/stage/#i, ...)
//         Holds ALL inputs: positional args, named args, session current pointer, etc.
//         Service / caller is responsible for injecting whatever the command needs
//         (e.g. session injects `current` field for cd / save / load).
//
//   out   Single-call output node; wired by Pipeline (ctx/reply, _pipe/stage/#i, ...)
//         Proc writes complex / multi-field output here.
//
// Result.data: convenience payload; when non-null, framework importAs<VarS> over out
// (D14). When null, out is left as proc wrote it.
//
// Result.code semantics (D9):
//   ==0  SUCCESS  → pipeline continues
//    <0  ERROR    → pipeline aborts (framework reserves <= -1000, see Result::Code)
//    >0  ACCEPT   → terminal-but-success (DAG case-edge / async accept)
//
// Framework state (cancel / event / trace) is hidden inside Pipeline; user proc
// queries it via thread-local helpers (e.g. ve::pipeline::cancelled()) or — for
// advanced cases — directly via Pipeline::context() after construction.
//
using Proc = std::function<Result(Node* in, Node* out)>;


// ============================================================================
// Schema — input/output shape declaration (filled by RegProto at register time)
// ============================================================================
//
// Used by CallProto for input packing + tooling (help, introspection).
// Runtime validation is LOOSE by default (locked Q-arch-1).

struct InSchema
{
    enum Kind : uint8_t { Empty, Positional, Named, FreeForm };
    Kind                     kind = Empty;
    std::vector<std::string> names;   // Named: param names in order
    std::vector<uint8_t>     types;   // optional Var::Type per param; empty = loose
};

struct OutSchema
{
    enum Kind : uint8_t { Empty, Single, Dict, FreeForm };
    Kind                     kind = Empty;
    uint8_t                  single_type = 0;   // Var::Type for Single
    std::vector<std::string> field_names;       // Dict: field names
};


// ============================================================================
// Proto tags — disjoint namespaces for CallProto / RegProto specialization
// ============================================================================
namespace tag {

// Call tags (external Input/Output <-> ctx envelope)
struct VarInVarOut;        // Var → /request,   Var ← /reply           (most common: just the data)
struct RequestReply;       // Var → /request,   Result ← envelope      (full code/message/data)
struct ListInDictOut;      // ListV → /request positional, Result out  (CLI-style)
struct NodeInOut;          // caller in/out nodes (zero-copy)

// Reg tags (user callable -> Proc wrap; 2-node Proc signature)
struct PositionalArgs;     // R fn(A1,…,An)              — in->at(i)
struct VarSingle;          // R fn(Var)                  — in->exportAs<VarS>
struct FullProc;           // Result fn(Node* in, Node* out)  — raw 2-node Proc
struct VoidAction;         // void fn() / int fn()       — side-effect only
struct ResultArgs;         // Result fn(A1,…,An)         — positional + explicit Result

} // namespace tag


// ============================================================================
// CallProto<Tag> — external data <-> ctx envelope conversion
// ============================================================================
//
// Each specialization provides:
//   using Input  = ...;
//   using Output = ...;
//   static void   import   (Node* ctx, const Input& v);     // external -> ctx
//   static Output exportOut(Node* ctx);                     // ctx -> external
//   static Output makeFailure(int code, const std::string& msg);   // for unknown-cmd path
//
// import() writes to /request (and possibly other top-level fields).
// exportOut() reads from /code, /message, /reply.

template<typename Tag>
struct CallProto;   // primary undefined

// ----- VarInVarOut (most common) ----------------------------------------------
// Input:  Var → ctx/request
// Output: Var ← ctx/reply (raw data; caller doesn't see code/message)
template<>
struct CallProto<tag::VarInVarOut>
{
    using Input  = Var;
    using Output = Var;
    static VE_API void import   (Node* ctx, const Var& v);
    static VE_API Var  exportOut(Node* ctx);
    static VE_API Var  makeFailure(int code, const std::string& msg);
};

// ----- RequestReply (full envelope) -------------------------------------------
// Input:  Var → ctx/request
// Output: Result {code, message, data} — full envelope (transport-friendly)
template<>
struct CallProto<tag::RequestReply>
{
    using Input  = Var;
    using Output = Result;
    static VE_API void   import   (Node* ctx, const Var& v);
    static VE_API Result exportOut(Node* ctx);
    static VE_API Result makeFailure(int code, const std::string& msg);
};

// ----- ListInDictOut (CLI-style) ----------------------------------------------
// Input:  Var::ListV → ctx/request positional (int-indexed children)
// Output: Result envelope
template<>
struct CallProto<tag::ListInDictOut>
{
    using Input  = Var::ListV;
    using Output = Result;
    static VE_API void   import   (Node* ctx, const Var::ListV& v);
    static VE_API Result exportOut(Node* ctx);
    static VE_API Result makeFailure(int code, const std::string& msg);
};

// ----- NodeInOut (zero-copy) --------------------------------------------------
// Input:  caller-owned {in, out} nodes (Pipeline wires them as in/out directly)
// Output: void (caller reads its own out node)
template<>
struct CallProto<tag::NodeInOut>
{
    struct Input { Node* in = nullptr; Node* out = nullptr; };
    using Output = void;
    static VE_API void import   (Node* ctx, const Input& v);
    static VE_API void exportOut(Node* ctx);
    static VE_API void makeFailure(int code, const std::string& msg);
};


// ============================================================================
// RegProto<Tag> — user callable -> Proc wrap (compile-time signature check)
// ============================================================================
//
// Each specialization provides:
//   template<typename F> static Proc       wrap        (F&& fn);
//   template<typename F> static InSchema   inSchemaOf  ();
//   template<typename F> static OutSchema  outSchemaOf ();
//
// wrap() uses static_assert to catch tag/signature mismatches (Q-pc-2).
// All specializations include try/catch in wrap() that maps std::exception
// to Result::fail(Result::EXCEPTION, e.what()) (Q-reg-3).
//
// Implementations are inline; see impl/command_proto_inl.h (included below).

template<typename Tag>
struct RegProto;

} // namespace ve

#include "impl/command_proto_inl.h"   // RegProto<tag::X> specializations

namespace ve {


// ============================================================================
// Command — Proc + Schema, factory-node-backed, independently callable
// ============================================================================
//
// Factory node layout for a registered command:
//
//   ve/factory/cmd/{key}/
//     help                    — help string
//     loop                    — optional bound Loop
//     declare/                — parameter declarations (NamedArgs / docs / parseArgs)
//     _proc                   — Var::custom<Proc>   (framework-internal)
//     _in_schema              — Var::custom<InSchema>
//     _out_schema             — Var::custom<OutSchema>

class VE_API Command : public NodeRef
{
public:
    // Default: unbound (isValid() == false). Allows containers like
    // std::vector<StepRuntime> with default-constructed slots.
    Command() = default;

    // Resolve key via the global command factory. If absent, _n is bound to a
    // singleton "unknown command" sentinel; every method stays safe to call;
    // dispatch returns Result::fail(UNKNOWN_CMD, ...) instead of segfaulting.
    // Use isValid() to distinguish from a real bind.
    explicit Command(const std::string& key, char sep = VE_FACTORY_KEY_SEP);

    // Bind directly to an existing factory node.
    Command(Node* factory_node) : NodeRef(factory_node) {}

    bool isValid() const;

    // --- accessors (read from factory node) ---
    Proc        proc()      const;
    InSchema    inSchema()  const;
    OutSchema   outSchema() const;

    std::string help() const               { return _n ? _n->get("help").toString() : std::string{}; }
    void        setHelp(const std::string& h) { if (_n) _n->set("help", h); }
    Node*       declare() const            { return _n ? _n->find("declare", false) : nullptr; }

    // --- independent call ---
    //
    // Spins up a transient Pipeline (own ctx) + addPathStep(this, /request, /reply),
    // runs synchronously, returns CallProtoT::Output.
    //
    //   Full form: cmd.call<CallProto<tag::RequestReply>>(input);
    //
    template<typename CallProtoT>
    typename CallProtoT::Output call(const typename CallProtoT::Input& input);

    // Convenience family (different names; no overload ambiguity).
    Result  callReply(const Var& input)             { return call<CallProto<tag::RequestReply>> (input); }
    Var     callVar  (const Var& input = {})        { return call<CallProto<tag::VarInVarOut>>  (input); }
    Result  callList (const Var::ListV& input)      { return call<CallProto<tag::ListInDictOut>>(input); }
    void    callNode (Node* in, Node* out)          {        call<CallProto<tag::NodeInOut>>   ({in, out}); }
};


// ============================================================================
// ve::command — namespace-level helpers
// ============================================================================

namespace command {

VE_API Factory& factory();

// --- registration ---------------------------------------------------------
//
// Full form (explicit tag, no magic):
//   command::reg<RegProto<tag::PositionalArgs>>("func.add",
//       [](double a, double b) { return a + b; });
//
// Convenience family (different names — no overload ambiguity, no magic):
//
//   reg       — RegProto<tag::PositionalArgs>  (default; positional args)
//   regProc   — RegProto<tag::FullProc>        (raw 2-node Proc: Node* in, Node* out)
//   regVar    — RegProto<tag::VarSingle>       (single Var in/out)
//   regAction — RegProto<tag::VoidAction>      (side-effect only)
//   regResult — RegProto<tag::ResultArgs>      (positional + user returns Result)

template<typename RegProtoT, typename F>
inline void reg(const std::string& key, F&& fn,
                const std::string& help = "", Loop lr = {},
                char sep = VE_FACTORY_KEY_SEP);

template<typename F>
inline void reg       (const std::string& key, F&& fn, const std::string& help = "", Loop lr = {});

// Three-arg form: reg(key, fn, Loop) — help defaults to empty.
template<typename F>
inline void reg       (const std::string& key, F&& fn, Loop lr);

// Four-arg form: reg(key, fn, Loop, help) — legacy "loop-before-help" order.
template<typename F>
inline void reg       (const std::string& key, F&& fn, Loop lr, const std::string& help);

template<typename F>
inline void regProc   (const std::string& key, F&& fn, const std::string& help = "", Loop lr = {});

template<typename F>
inline void regVar    (const std::string& key, F&& fn, const std::string& help = "", Loop lr = {});

template<typename F>
inline void regAction (const std::string& key, F&& fn, const std::string& help = "", Loop lr = {});

template<typename F>
inline void regResult (const std::string& key, F&& fn, const std::string& help = "", Loop lr = {});


// --- query ---------------------------------------------------------------
VE_API bool        has  (const std::string& key, char sep = VE_FACTORY_KEY_SEP);
inline std::string help (const std::string& key) { return Command(key).help(); }
inline Strings     keys ()                       { return factory().keys(); }


// --- dispatch ------------------------------------------------------------
//
// Full form:
//   auto r = command::call<CallProto<tag::RequestReply>>("func.add", input);
//
// Convenience family (most callers want one of these):

template<typename CallProtoT>
inline typename CallProtoT::Output call(const std::string& key,
                                        const typename CallProtoT::Input& input)
{
    return Command(key).call<CallProtoT>(input);
}

// callSync — explicitly synchronous (blocks until the command completes).
//
// PR A: Pipeline::call is already single-pass synchronous, so this is a thin
// alias of call<CallProtoT>. PR C will make Pipeline async-capable, at which
// point this wrapper, when invoked on its target loop's own thread
// (target.isCurrentThread()), drains it via target.processEvents() instead of
// CV-waiting (so a nested call from a loop's worker thread doesn't self-starve).
template<typename CallProtoT>
inline typename CallProtoT::Output callSync(const std::string& key,
                                            const typename CallProtoT::Input& input)
{
    return call<CallProtoT>(key, input);
}

inline Result callReply(const std::string& key, const Var& input = {})
{ return Command(key).callReply(input); }

inline Var callVar(const std::string& key, const Var& input = {})
{ return Command(key).callVar(input); }

inline Result callList(const std::string& key, const Var::ListV& input)
{ return Command(key).callList(input); }

inline void callNode(const std::string& key, Node* in, Node* out)
{ Command(key).callNode(in, out); }


// --- ctx-side helpers (for proc bodies reading params from /request) ----
//
// Args API stays compatible with declare/-driven access; PR B will deepen this
// with RegProto<NamedArgs> schema integration.

struct VE_API Args : NodeRef
{
    using NodeRef::NodeRef;

    std::string string (const std::string& key, const std::string& def = "") const;
    int64_t     integer(const std::string& key, int64_t def = 0) const;
    double      number (const std::string& key, double def = 0.0) const;
    bool        flag   (const std::string& key, bool def = false) const;
    Var         var    (const std::string& key, const Var& def = {}) const;
    bool        has    (const std::string& key) const;
};

VE_API Args args(Node* in);

// parseArgs: writes parsed values into ctx/request (positional under int-index, named under string-key).
// Reads declare/ shadow if present (for short-name expansion, defaults, name lookup).
VE_API bool parseArgs(Node* ctx, const std::vector<std::string>& args, int startIdx = 0);
VE_API bool parseArgs(Node* ctx, const Var& input);

// declareNode: ensure key exists and expose its declare/ subtree for parameter declarations.
// Does NOT register a proc; pair with subsequent command::reg / regProc / etc to make it runnable.
inline Node* declareNode(const std::string& key, char sep = VE_FACTORY_KEY_SEP)
{
    return factory().node(key, sep)->at("declare");
}

// current(in) — extract the "current node" pointer from in/current field.
// Conventional protocol: callers inject session.current as the `current` field
// of the input dict; command body retrieves it via command::current(in), with
// fall-back to ve::n("/") (root) when absent.
inline Node* current(Node* in)
{
    if (!in) return ve::n("/");
    if (auto* cn = in->find("current"))
        if (auto* p = cn->get().toPointer())
            return static_cast<Node*>(p);
    return ve::n("/");
}

} // namespace command

} // namespace ve

#include "impl/command_reg_inl.h"   // template impl for command::reg<...> family
