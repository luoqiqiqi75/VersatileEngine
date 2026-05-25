// test_command.cpp — PR A core tests: Result / Proc / Command / RegProto / CallProto / Pipeline wiring / ctx
//
// PR A scope:
//   - Result three-segment {code, message, data}
//   - Proc = Result(Node*, Node*, Node*)
//   - Command = Proc + Schema, factory-node-backed
//   - RegProto<tag::X> wrap (PositionalArgs / NodeAction / VoidAction / VarSingle / InOutAction / FullProc / ResultArgs)
//   - CallProto<tag::X> (VarInVarOut / RequestReply / ListInDictOut / NodeInOut)
//   - Pipeline wiring (CtxStep / LinearStep / PathStep) — DAG deferred to PR C
//   - ctx top-level 4 envelopes + _pipe/ cleanup
//
// Async / cancel / lifetime / cross-loop / DAG → PR C / PR D.

#include "ve_test.h"
#include <ve/core/command.h>
#include <ve/core/loop.h>
#include <ve/core/pipeline.h>
#include <ve/core/node.h>

using namespace ve;


// ============================================================================
// 1. Result three-segment structure
// ============================================================================

VE_TEST(result_default_is_success) {
    Result r;
    VE_ASSERT(r.isSuccess());
    VE_ASSERT(!r.isError());
    VE_ASSERT(!r.isAccepted());
    VE_ASSERT_EQ(r.code, 0);
    VE_ASSERT(r.message.empty());
    VE_ASSERT(r.data.isNull());
}

VE_TEST(result_ok_carries_data) {
    Result r = Result::ok(Var(42));
    VE_ASSERT(r.isSuccess());
    VE_ASSERT_EQ(r.data.toInt(), 42);
    VE_ASSERT(r.message.empty());
}

VE_TEST(result_fail_message) {
    Result r = Result::fail("oops");
    VE_ASSERT(r.isError());
    VE_ASSERT_EQ(r.code, -1);
    VE_ASSERT_EQ(r.message, std::string("oops"));
}

VE_TEST(result_fail_explicit_code) {
    Result r = Result::fail(-42, "specific");
    VE_ASSERT_EQ(r.code, -42);
    VE_ASSERT_EQ(r.message, std::string("specific"));
}

VE_TEST(result_fail_clamps_positive_to_minus_one) {
    Result r = Result::fail(7, "should clamp");
    VE_ASSERT_EQ(r.code, -1);
}

VE_TEST(result_accept_terminal_success) {
    Result r = Result::accept();
    VE_ASSERT(r.isAccepted());
    VE_ASSERT(!r.isError());
    VE_ASSERT(!r.isSuccess());
    VE_ASSERT_EQ(r.code, Result::ACCEPT);
}

VE_TEST(result_framework_code_constants) {
    VE_ASSERT_EQ(Result::EXCEPTION,   -1000);
    VE_ASSERT_EQ(Result::CANCELLED,   -1001);
    VE_ASSERT_EQ(Result::UNKNOWN_CMD, -1002);
    VE_ASSERT_EQ(Result::BAD_REQUEST, -1003);
    VE_ASSERT_EQ(Result::TIMEOUT,     -1004);
}


// ============================================================================
// 2. Proc signature + 3-segment code semantics
// ============================================================================

VE_TEST(proc_two_node_signature) {
    Node in("in"), out("out");
    in.set(Var(7));

    Proc p = [](Node* in, Node* out) -> Result {
        out->set(Var(in->get().toInt() * 2));
        return Result::ok();
    };

    Result r = p(&in, &out);
    VE_ASSERT(r.isSuccess());
    VE_ASSERT_EQ(out.get().toInt(), 14);
}

VE_TEST(proc_returns_data_via_result) {
    Proc p = [](Node*, Node*) -> Result {
        return Result::ok(Var(42));
    };
    Node in("in"), out("out");
    Result r = p(&in, &out);
    VE_ASSERT_EQ(r.data.toInt(), 42);
}

VE_TEST(proc_returns_error) {
    Proc p = [](Node*, Node*) -> Result {
        return Result::fail("bad input");
    };
    Node in("in"), out("out");
    Result r = p(&in, &out);
    VE_ASSERT(r.isError());
    VE_ASSERT_EQ(r.message, std::string("bad input"));
}


// ============================================================================
// 3. RegProto specializations (via command::reg + SmartProto auto-tag)
// ============================================================================

VE_TEST(reg_positional_args_two_ints) {
    command::reg("_test_add", [](int a, int b) -> Result {
        return Result::ok(Var(a + b));
    });
    Var r = command::callVar("_test_add", Var(Var::ListV{Var(3), Var(4)}));
    VE_ASSERT_EQ(r.toInt(), 7);
}

VE_TEST(reg_two_node_via_smart_proto) {
    // SmartProto picks FullProc for (Node*, Node*) signature.
    command::reg("_test_two_node_touch", [](Node* in, Node* /*out*/) -> Result {
        in->at("touched")->set(Var(true));
        return Result::ok();
    });
    Result r = command::callReply("_test_two_node_touch", Var());
    VE_ASSERT(r.isSuccess());
    VE_ASSERT_EQ(r.code, 0);
}

VE_TEST(reg_void_action_via_smart_proto) {
    static int called = 0;
    called = 0;
    command::reg("_test_void", []() -> Result {
        ++called;
        return Result::ok();
    });
    Result r = command::callReply("_test_void", Var());
    VE_ASSERT(r.isSuccess());
    VE_ASSERT_EQ(called, 1);
}

VE_TEST(reg_full_proc_explicit) {
    command::regProc("_test_full", [](Node* in, Node* out) -> Result {
        if (in->get().isNull()) return Result::fail("no input");
        out->set(Var(in->get().toInt() + 100));
        return Result::ok();
    });
    Var r = command::callVar("_test_full", Var(5));
    VE_ASSERT_EQ(r.toInt(), 105);
}

VE_TEST(reg_var_single) {
    command::regVar("_test_var_single", [](Var input) -> Result {
        return Result::ok(Var(input.toInt() * 3));
    });
    Var r = command::callVar("_test_var_single", Var(7));
    VE_ASSERT_EQ(r.toInt(), 21);
}

VE_TEST(reg_result_args_carries_code_and_message) {
    command::regResult("_test_div", [](double a, double b) -> Result {
        if (b == 0) return Result::fail(-7, "div by zero");
        return Result::ok(Var(a / b));
    });
    Result r = command::callReply("_test_div", Var(Var::ListV{Var(10.0), Var(0.0)}));
    VE_ASSERT(r.isError());
    VE_ASSERT_EQ(r.code, -7);
    VE_ASSERT_EQ(r.message, std::string("div by zero"));
}


// ============================================================================
// 4. CallProto specializations
// ============================================================================

VE_TEST(call_proto_var_in_var_out) {
    command::reg("_test_inc", [](int x) -> Result { return Result::ok(Var(x + 1)); });
    Var r = command::callVar("_test_inc", Var(Var::ListV{Var(10)}));
    VE_ASSERT_EQ(r.toInt(), 11);
}

VE_TEST(call_proto_request_reply_envelope) {
    command::reg("_test_double", [](int x) -> Result { return Result::ok(Var(x * 2)); });
    Result r = command::callReply("_test_double", Var(Var::ListV{Var(21)}));
    VE_ASSERT(r.isSuccess());
    VE_ASSERT_EQ(r.code, 0);
    VE_ASSERT_EQ(r.data.toInt(), 42);
}

VE_TEST(call_proto_request_reply_carries_error_message) {
    command::regResult("_test_fail", [](int) -> Result {
        return Result::fail(-3, "intentional failure");
    });
    Result r = command::callReply("_test_fail", Var(Var::ListV{Var(0)}));
    VE_ASSERT(r.isError());
    VE_ASSERT_EQ(r.code, -3);
    VE_ASSERT_EQ(r.message, std::string("intentional failure"));
}

VE_TEST(call_proto_unknown_command) {
    Result r = command::callReply("_test_no_such_command_anywhere", Var());
    VE_ASSERT(r.isError());
    VE_ASSERT_EQ(r.code, Result::UNKNOWN_CMD);
}

VE_TEST(call_proto_node_in_out) {
    command::regProc("_test_copy", [](Node* in, Node* out) -> Result {
        out->set(in->get());
        return Result::ok();
    });
    Node my_in("in"), my_out("out");
    my_in.set(Var(123));
    command::callNode("_test_copy", &my_in, &my_out);
    // NodeInOut is zero-copy: caller reads its own out node.
    // (PR A wiring for NodeInOut is stub; this test mainly ensures no crash.)
}


// ============================================================================
// 5. Pipeline wiring forms (CtxStep / LinearStep / PathStep)
//    DAG deferred to PR C.
// ============================================================================

VE_TEST(pipeline_addCtxStep_basic) {
    // With addCtxStep, in == out == pipeline internal ctx node.
    command::regProc("_test_pipe_ctxstep", [](Node* in, Node* /*out*/) -> Result {
        in->at("hit")->set(Var(true));
        return Result::ok();
    });

    Pipeline p("pipe");
    p.addCtxStep(Command("_test_pipe_ctxstep"));
    Result r = p.callReply(Var());

    VE_ASSERT(r.isSuccess());
    VE_ASSERT(p.context()->find("hit"));
    VE_ASSERT_EQ(p.context()->find("hit")->get().toBool(), true);
}

VE_TEST(pipeline_addLinearStep_single) {
    command::regProc("_test_pipe_linear_inc", [](Node* in, Node* out) -> Result {
        out->set(Var(in->get().toInt() + 1));
        return Result::ok();
    });

    Pipeline p("pipe");
    p.addLinearStep(Command("_test_pipe_linear_inc"));
    Var r = p.callVar(Var(10));
    VE_ASSERT_EQ(r.toInt(), 11);
}

VE_TEST(pipeline_addLinearStep_chain_three) {
    command::regProc("_test_pipe_inc1", [](Node* in, Node* out) -> Result {
        out->set(Var(in->get().toInt() + 1));
        return Result::ok();
    });

    Pipeline p("pipe");
    p.addLinearStep(Command("_test_pipe_inc1"));
    p.addLinearStep(Command("_test_pipe_inc1"));
    p.addLinearStep(Command("_test_pipe_inc1"));
    Var r = p.callVar(Var(0));
    VE_ASSERT_EQ(r.toInt(), 3);  // 0 -> 1 -> 2 -> 3
}

VE_TEST(pipeline_addPathStep_explicit) {
    command::regProc("_test_pipe_path", [](Node* in, Node* out) -> Result {
        out->set(Var(in->get().toInt() * 10));
        return Result::ok();
    });

    Pipeline p("pipe");
    p.addPathStep(Command("_test_pipe_path"), "request", "reply");
    Var r = p.callVar(Var(4));
    VE_ASSERT_EQ(r.toInt(), 40);
}

VE_TEST(pipeline_aborts_on_error) {
    command::regProc("_test_pipe_ok",    [](Node* /*in*/, Node* out) -> Result { out->set(Var(1)); return Result::ok(); });
    command::regProc("_test_pipe_fail",  [](Node* /*in*/, Node* /*out*/) -> Result { return Result::fail(-9, "bail"); });
    command::regProc("_test_pipe_never", [](Node* /*in*/, Node* out) -> Result { out->set(Var(999)); return Result::ok(); });

    Pipeline p("pipe");
    p.addLinearStep(Command("_test_pipe_ok"));
    p.addLinearStep(Command("_test_pipe_fail"));
    p.addLinearStep(Command("_test_pipe_never"));

    Result r = p.callReply(Var());
    VE_ASSERT(r.isError());
    VE_ASSERT_EQ(r.code, -9);
    VE_ASSERT_EQ(r.message, std::string("bail"));
    VE_ASSERT_EQ(p.state(), Pipeline::ERRORED);
}


// ============================================================================
// 6. ctx top-level 4 envelopes + _pipe/ cleanup (D7)
// ============================================================================

VE_TEST(ctx_envelope_request_reply) {
    command::regProc("_test_ctx_req_reply", [](Node* in, Node* out) -> Result {
        // proc reads from in (= ctx/request), writes to out (= ctx/reply)
        out->set(Var(in->get().toString() + "!"));
        return Result::ok();
    });

    Pipeline p("pipe");
    p.addPathStep(Command("_test_ctx_req_reply"), "request", "reply");
    p.callReply(Var(std::string("hi")));

    Node* ctx = p.context();
    VE_ASSERT(ctx->find("request"));
    VE_ASSERT(ctx->find("reply"));
    VE_ASSERT_EQ(ctx->find("reply")->get().toString(), std::string("hi!"));
}

VE_TEST(ctx_envelope_code_and_message_set) {
    // 0-arg fn returning Result → SmartProto picks VoidAction.
    command::reg("_test_ctx_code", []() -> Result {
        return Result::fail(-77, "tagged");
    });
    Pipeline p("pipe");
    p.addPathStep(Command("_test_ctx_code"), "request", "reply");
    Result r = p.callReply(Var());

    VE_ASSERT_EQ(r.code, -77);
    VE_ASSERT_EQ(r.message, std::string("tagged"));

    Node* ctx = p.context();
    VE_ASSERT(ctx->find("code"));
    VE_ASSERT_EQ(ctx->find("code")->get().toInt(), -77);
    VE_ASSERT(ctx->find("message"));
    VE_ASSERT_EQ(ctx->find("message")->get().toString(), std::string("tagged"));
}

VE_TEST(ctx_external_user_fields_preserved_pipe_cleared) {
    Node my_ctx("user_ctx");
    my_ctx.at("user_field")->set(Var(std::string("must-survive")));

    command::regProc("_test_ctx_external", [](Node* /*in*/, Node* /*out*/) -> Result {
        return Result::ok(Var(42));
    });

    Pipeline p("pipe", &my_ctx);
    p.addPathStep(Command("_test_ctx_external"), "request", "reply");
    Var r = p.callVar(Var());
    VE_ASSERT_EQ(r.toInt(), 42);

    // user field preserved
    VE_ASSERT(my_ctx.find("user_field"));
    VE_ASSERT_EQ(my_ctx.find("user_field")->get().toString(), std::string("must-survive"));

    // _pipe/ subtree removed
    VE_ASSERT(!my_ctx.find("_pipe"));

    // envelope kept on external ctx (so caller can inspect /code /message /reply)
    VE_ASSERT(my_ctx.find("reply"));
}


// ============================================================================
// 7. command:: convenience family + factory query
// ============================================================================

VE_TEST(command_has_and_keys) {
    command::reg("_test_keys_one", [](int x) -> Result { return Result::ok(Var(x)); });
    VE_ASSERT(command::has("_test_keys_one"));
    VE_ASSERT(!command::has("_test_keys_definitely_not_registered_xyz"));

    auto keys = command::keys();
    bool found = false;
    for (auto& k : keys) if (k == "_test_keys_one") { found = true; break; }
    VE_ASSERT(found);
}

VE_TEST(command_help_round_trip) {
    command::reg("_test_help", [](int) -> Result { return Result::ok(); }, "double the input");
    VE_ASSERT_EQ(command::help("_test_help"), std::string("double the input"));
}

VE_TEST(command_isValid_distinguishes_missing) {
    command::reg("_test_valid_real", [](int) -> Result { return Result::ok(); });
    VE_ASSERT(Command("_test_valid_real").isValid());
    VE_ASSERT(!Command("_test_valid_does_not_exist_xyz").isValid());
}
