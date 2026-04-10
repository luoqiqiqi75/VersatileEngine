// test_command.cpp — Tests for Step, Pipeline, Command, Factory, and Object::once

#include "ve_test.h"
#include <ve/core/command.h>
#include <ve/core/loop.h>
#include <ve/core/pipeline.h>
#include <ve/core/node.h>

#include <chrono>
#include <thread>

using namespace ve;

static void wait_pipeline_terminal(Pipeline* p)
{
    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + std::chrono::seconds(2);
    while (clock::now() < deadline) {
        const auto s = p->state();
        if (s != Pipeline::RUNNING && s != Pipeline::PAUSED) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

// ============================================================================
// Step tests
// ============================================================================

VE_TEST(step_basic_exec) {
    Step s([](const Var& v) -> Result {
        return Result::ok(Var(v.toInt() + 1));
    });
    VE_ASSERT(s);

    Result r = resultFromStepReturn(s.exec(Var(10)));
    VE_ASSERT(r.isSuccess());
    VE_ASSERT_EQ(r.content().toInt(), 11);
}

VE_TEST(step_empty) {
    Step s;
    VE_ASSERT(!s);
    Result r = resultFromStepReturn(s.exec(Var(42)));
    VE_ASSERT(r.isError());
}

VE_TEST(step_void_fn) {
    int called = 0;
    Step s([&called]() { ++called; });
    Result r = resultFromStepReturn(s.exec());
    VE_ASSERT(r.isSuccess());
    VE_ASSERT_EQ(called, 1);
}

VE_TEST(step_typed_arg) {
    Step s([](int x) -> Result {
        return Result::ok(Var(x * 2));
    });
    Result r = resultFromStepReturn(s.exec(Var(7)));
    VE_ASSERT(r.isSuccess());
    VE_ASSERT_EQ(r.content().toInt(), 14);
}

VE_TEST(step_copy) {
    int counter = 0;
    Step original([&counter](const Var&) -> Result {
        ++counter;
        return Result::ok();
    });

    Step copy = original;
    resultFromStepReturn(copy.exec());
    VE_ASSERT_EQ(counter, 1);
}

VE_TEST(step_multi_arg_list) {
    Step s([](int a, int b) -> Result {
        return Result::ok(Var(a + b));
    });
    Var::ListV args;
    args.push_back(Var(3));
    args.push_back(Var(4));
    Result r = resultFromStepReturn(s.exec(Var(std::move(args))));
    VE_ASSERT(r.isSuccess());
    VE_ASSERT_EQ(r.content().toInt(), 7);
}

// ============================================================================
// Pipeline tests
// ============================================================================

VE_TEST(pipeline_single_step) {
    Pipeline pipe("test");
    pipe.add([](Node* n) -> Result {
        Var v = n ? n->get() : Var();
        return Result::ok(Var(v.toInt() + 100));
    });

    VE_ASSERT_EQ(pipe.stepCount(), 1);
    VE_ASSERT_EQ(pipe.state(), Pipeline::IDLE);

    pipe.start(Var(5));
    VE_ASSERT_EQ(pipe.state(), Pipeline::DONE);
    VE_ASSERT_EQ(pipe.lastResult().content().toInt(), 105);
}

VE_TEST(pipeline_multi_step) {
    std::vector<int> order;
    Pipeline pipe("multi");
    pipe.add([&order](const Var&) -> Result { order.push_back(1); return Result::ok(); });
    pipe.add([&order](const Var&) -> Result { order.push_back(2); return Result::ok(); });
    pipe.add([&order](const Var&) -> Result { order.push_back(3); return Result::ok(); });

    pipe.start();
    VE_ASSERT_EQ(pipe.state(), Pipeline::DONE);
    VE_ASSERT_EQ((int)order.size(), 3);
    VE_ASSERT_EQ(order[0], 1);
    VE_ASSERT_EQ(order[1], 2);
    VE_ASSERT_EQ(order[2], 3);
}

VE_TEST(pipeline_error_stops) {
    std::vector<int> order;
    Pipeline pipe("err");
    pipe.add([&order](const Var&) -> Result { order.push_back(1); return Result::ok(); });
    pipe.add([&order](const Var&) -> Result { order.push_back(2); return Result::fail(); });
    pipe.add([&order](const Var&) -> Result { order.push_back(3); return Result::ok(); });

    pipe.start();
    VE_ASSERT_EQ(pipe.state(), Pipeline::ERRORED);
    VE_ASSERT_EQ((int)order.size(), 2);
}

VE_TEST(pipeline_pause_resume) {
    std::vector<int> order;
    Pipeline pipe("pausable");
    pipe.add([&](const Var&) -> Result {
        order.push_back(1);
        pipe.pause();
        return Result::ok();
    });
    pipe.add([&order](const Var&) -> Result { order.push_back(2); return Result::ok(); });

    pipe.start();
    VE_ASSERT_EQ(pipe.state(), Pipeline::PAUSED);
    VE_ASSERT_EQ((int)order.size(), 1);

    pipe.resume();
    VE_ASSERT_EQ(pipe.state(), Pipeline::DONE);
    VE_ASSERT_EQ((int)order.size(), 2);
}

VE_TEST(pipeline_stop) {
    std::vector<int> order;
    Pipeline pipe("stoppable");
    pipe.add([&](const Var&) -> Result {
        order.push_back(1);
        pipe.pause();
        return Result::ok();
    });
    pipe.add([&order](const Var&) -> Result { order.push_back(2); return Result::ok(); });

    pipe.start();
    VE_ASSERT_EQ(pipe.state(), Pipeline::PAUSED);

    pipe.stop();
    VE_ASSERT_EQ(pipe.state(), Pipeline::IDLE);
    VE_ASSERT_EQ((int)order.size(), 1);
}

VE_TEST(pipeline_rerun) {
    int count = 0;
    Pipeline pipe("rerun");
    pipe.add([&count](const Var&) -> Result { ++count; return Result::ok(); });

    pipe.start();
    VE_ASSERT_EQ(count, 1);
    VE_ASSERT_EQ(pipe.state(), Pipeline::DONE);

    pipe.start();
    VE_ASSERT_EQ(count, 2);
    VE_ASSERT_EQ(pipe.state(), Pipeline::DONE);
}

VE_TEST(pipeline_result_handler) {
    bool handler_called = false;
    Pipeline pipe("rh");
    pipe.add([](const Var&) -> Result { return Result::ok(); });
    pipe.setResultHandler([&handler_called](const Result& r) {
        handler_called = true;
        VE_ASSERT(r.isSuccess());
    });
    pipe.start();
    VE_ASSERT(handler_called);
}

VE_TEST(pipeline_signal_done) {
    Object observer("obs");
    bool got_done = false;
    Pipeline pipe("sig");
    pipe.add([](const Var&) -> Result { return Result::ok(); });
    pipe.connect<Pipeline::CMD_DONE>(&observer, [&got_done](const Var&) {
        got_done = true;
    });
    pipe.start();
    VE_ASSERT(got_done);
}

VE_TEST(pipeline_signal_error) {
    Object observer("obs");
    bool got_error = false;
    Pipeline pipe("sig_err");
    pipe.add([](const Var&) -> Result { return Result::fail(); });
    pipe.connect<Pipeline::CMD_ERROR>(&observer, [&got_error](const Var&) {
        got_error = true;
    });
    pipe.start();
    VE_ASSERT(got_error);
}

VE_TEST(pipeline_clone) {
    int count = 0;
    Pipeline pipe("orig");
    pipe.add([&count](const Var&) -> Result { ++count; return Result::ok(); });

    Pipeline* copy = pipe.clone();
    VE_ASSERT_EQ(copy->name(), "orig");
    VE_ASSERT_EQ(copy->stepCount(), 1);

    copy->start();
    VE_ASSERT_EQ(count, 1);
    delete copy;
}

// ============================================================================
// Command tests
// ============================================================================

VE_TEST(command_basic) {
    Command cmd("deploy");
    cmd.addStep([](const Var&) -> Result { return Result::ok(); });
    cmd.addStep([](const Var&) -> Result { return Result::ok(); });

    VE_ASSERT_EQ(cmd.name(), "deploy");
    VE_ASSERT_EQ(cmd.stepCount(), 2);
}

VE_TEST(command_pipeline_creation) {
    int order_val = 0;

    // Build pipeline directly (no LoopRef) so the step runs synchronously.
    Pipeline pipe("test");
    pipe.add(Step([&order_val](const Var&) -> Result {
        order_val = 1;
        return Result::ok();
    }));

    VE_ASSERT_EQ(pipe.stepCount(), 1);

    pipe.start();
    VE_ASSERT_EQ(order_val, 1);
    VE_ASSERT_EQ(pipe.state(), Pipeline::DONE);
}

VE_TEST(command_help_metadata) {
    Command cmd("greet");
    cmd.setHelp("say hello");
    VE_ASSERT_EQ(cmd.help(), "say hello");
}

// ============================================================================
// Factory tests
// ============================================================================

VE_TEST(step_factory_register_and_exec) {
    command::reg("_test_greet", [](const std::string& v) -> Result {
        return Result::ok(Var("hello " + v));
    });
    VE_ASSERT(command::has("_test_greet"));

    auto r = command::call("_test_greet", Var("world"));
    VE_ASSERT(r.isSuccess());
    VE_ASSERT_EQ(r.content().toString(), "hello world");

    GlobalCommandFactory().erase("_test_greet");
}

VE_TEST(command_factory_register_and_exec) {
    GlobalCommandFactory().insertOne("_test_deploy", []() -> Command* {
        auto* cmd = new Command("_test_deploy");
        cmd->addStep([](const Var&) -> Result { return Result::ok(); });
        return cmd;
    });
    VE_ASSERT(GlobalCommandFactory().has("_test_deploy"));

    Command* cmd = GlobalCommandFactory().exec("_test_deploy");
    VE_ASSERT(cmd != nullptr);
    VE_ASSERT_EQ(cmd->stepCount(), 1);
    delete cmd;

    GlobalCommandFactory().erase("_test_deploy");
}

// ============================================================================
// command:: namespace tests
// ============================================================================

VE_TEST(command_ns_reg_and_call) {
    command::reg("_test_echo", [](int value) -> Result {
        return Result::ok(Var(value));
    }, "echo input");

    VE_ASSERT(command::has("_test_echo"));
    VE_ASSERT_EQ(command::help("_test_echo"), "echo input");

    Result r = command::call("_test_echo", Var(42));
    VE_ASSERT(r.isSuccess());

    GlobalCommandFactory().erase("_test_echo");
}

VE_TEST(command_ns_run) {
    GlobalCommandFactory().insertOne("_test_multi", []() -> Command* {
        auto* cmd = new Command("_test_multi");
        cmd->addStep([](const Var&) -> Result { return Result::ok(); });
        cmd->addStep([](const Var&) -> Result { return Result::ok(); });
        return cmd;
    });

    Pipeline* pipe = command::run("_test_multi", Var());
    VE_ASSERT(pipe != nullptr);
    wait_pipeline_terminal(pipe);
    VE_ASSERT_EQ(pipe->state(), Pipeline::DONE);
    delete pipe;

    GlobalCommandFactory().erase("_test_multi");
}

VE_TEST(command_ns_step) {
    command::reg("_test_inc", [](int value) -> Result {
        return Result::ok(Var(value + 1));
    });

    Pipeline* pipe = command::run("_test_inc", Var(10));
    VE_ASSERT(pipe != nullptr);
    wait_pipeline_terminal(pipe);
    VE_ASSERT_EQ(pipe->state(), Pipeline::DONE);
    delete pipe;

    GlobalCommandFactory().erase("_test_inc");
}

VE_TEST(register_step_with_loopref) {
    EventLoop loop("regstep_lr");
    loop.start();
    registerStep("_test_regstep_lr", [](Node*) -> Result { return Result::ok(Var(1)); },
                 LoopRef::from(loop), "lr help");
    VE_ASSERT(command::has("_test_regstep_lr"));
    VE_ASSERT_EQ(command::help("_test_regstep_lr"), "lr help");

    Command* cmd = GlobalCommandFactory().exec("_test_regstep_lr");
    VE_ASSERT(cmd != nullptr);
    VE_ASSERT_EQ(cmd->stepCount(), 1);
    VE_ASSERT(static_cast<bool>(cmd->steps()[0].second));
    delete cmd;

    loop.stop();
    GlobalCommandFactory().erase("_test_regstep_lr");
}

VE_TEST(command_reg_with_loopref) {
    EventLoop loop("reg_ns_lr");
    loop.start();
    command::reg("_test_reg_ns_lr", [](Node*) -> Result { return Result::ok(); },
                 LoopRef::from(loop), "with loop");
    VE_ASSERT(command::has("_test_reg_ns_lr"));
    VE_ASSERT_EQ(command::help("_test_reg_ns_lr"), "with loop");

    Command* cmd = GlobalCommandFactory().exec("_test_reg_ns_lr");
    VE_ASSERT(cmd != nullptr);
    VE_ASSERT(static_cast<bool>(cmd->steps()[0].second));
    delete cmd;

    loop.stop();
    GlobalCommandFactory().erase("_test_reg_ns_lr");
}

VE_TEST(command_ns_not_found) {
    Result r = command::call("_test_nonexistent");
    VE_ASSERT(r.isError());

    Pipeline* pipe = command::run("_test_nonexistent");
    VE_ASSERT(pipe == nullptr);
}

VE_TEST(command_context_keeps_current_out_of_children) {
    Node current("current");
    Node* ctx = command::context("_test_ctx_meta", &current);

    VE_ASSERT_EQ(command::current(ctx), &current);
    VE_ASSERT_EQ(ctx->count(), 0);

    delete ctx;
}

VE_TEST(command_parse_args_with_current_keeps_positional_index_zero) {
    Node current("current");
    Node* ctx = command::context("_test_ctx_parse", &current);

    VE_ASSERT(command::parseArgs(ctx, std::vector<std::string>{"hello"}));
    VE_ASSERT_EQ(ctx->get(0).toString(), "hello");
    VE_ASSERT_EQ(ctx->count(), 1);
    VE_ASSERT_EQ(command::current(ctx), &current);

    delete ctx;
}

VE_TEST(command_parse_args_maps_declared_positional_and_named_params) {
    auto* decl = node::root()->at("ve/command/declare/_test_declared_args");
    decl->at("topic");
    decl->at("target_node");

    Node* positionalCtx = command::context("_test_declared_args");
    VE_ASSERT(command::parseArgs(positionalCtx, std::vector<std::string>{"/robot_states", "/target"}));
    auto positionalArgs = command::args(positionalCtx);
    VE_ASSERT_EQ(positionalArgs.string("topic"), "/robot_states");
    VE_ASSERT_EQ(positionalArgs.string("target_node"), "/target");
    delete positionalCtx;

    Node* namedCtx = command::context("_test_declared_args");
    VE_ASSERT(command::parseArgs(namedCtx, std::vector<std::string>{"--topic", "/robot_states", "--target_node", "/target"}));
    auto namedArgs = command::args(namedCtx);
    VE_ASSERT_EQ(namedArgs.string("topic"), "/robot_states");
    VE_ASSERT_EQ(namedArgs.string("target_node"), "/target");
    delete namedCtx;
}

// ============================================================================
// wrapStepCallable — Form 2 tests (plain callable, not Node* ctx)
// ============================================================================

VE_TEST(step_form2_multi_arg_int) {
    command::reg("_test_add", [](int a, int b) { return a + b; });
    auto r = command::call("_test_add", Var(Var::ListV{Var(3), Var(4)}));
    VE_ASSERT(r.isSuccess());
    VE_ASSERT_EQ(r.content().toInt(), 7);
    GlobalCommandFactory().erase("_test_add");
}

VE_TEST(step_form2_multi_arg_result) {
    command::reg("_test_div", [](int a, int b) -> Result {
        if (b == 0) return Result::fail(Var("div by zero"));
        return Result::ok(Var(a / b));
    });
    auto r1 = command::call("_test_div", Var(Var::ListV{Var(10), Var(2)}));
    VE_ASSERT(r1.isSuccess());
    VE_ASSERT_EQ(r1.content().toInt(), 5);

    auto r2 = command::call("_test_div", Var(Var::ListV{Var(1), Var(0)}));
    VE_ASSERT(r2.isError());
    GlobalCommandFactory().erase("_test_div");
}

VE_TEST(step_form2_single_string) {
    command::reg("_test_hi", [](const std::string& name) {
        return std::string("hi ") + name;
    });
    auto r = command::call("_test_hi", Var("alice"));
    VE_ASSERT(r.isSuccess());
    VE_ASSERT_EQ(r.content().toString(), "hi alice");
    GlobalCommandFactory().erase("_test_hi");
}

VE_TEST(step_form2_void_no_args) {
    int called = 0;
    command::reg("_test_noop", [&called]() { ++called; });
    auto r = command::call("_test_noop");
    VE_ASSERT(r.isSuccess());
    VE_ASSERT_EQ(called, 1);
    GlobalCommandFactory().erase("_test_noop");
}

VE_TEST(step_form2_var_return) {
    command::reg("_test_ping", []() -> Var { return Var("pong"); });
    auto r = command::call("_test_ping");
    VE_ASSERT(r.isSuccess());
    VE_ASSERT_EQ(r.content().toString(), "pong");
    GlobalCommandFactory().erase("_test_ping");
}

VE_TEST(step_form2_double_args) {
    command::reg("_test_sum", [](double a, double b) { return a + b; });
    auto r = command::call("_test_sum", Var(Var::ListV{Var(1.5), Var(2.5)}));
    VE_ASSERT(r.isSuccess());
    VE_ASSERT_EQ(r.content().toDouble(), 4.0);
    GlobalCommandFactory().erase("_test_sum");
}

VE_TEST(step_form2_single_arg_via_call) {
    // single-arg via command::call(key, Var) — parseArgs puts value in child
    command::reg("_test_double_it", [](int x) { return x * 2; });
    auto r = command::call("_test_double_it", Var(21));
    VE_ASSERT(r.isSuccess());
    VE_ASSERT_EQ(r.content().toInt(), 42);
    GlobalCommandFactory().erase("_test_double_it");
}

VE_TEST(step_form2_single_arg_ctx_child) {
    // single-arg: value in ctx child at index 0
    command::reg("_test_negate", [](int x) { return -x; });
    Node* ctx = command::context("_test_negate");
    ctx->at(0, false)->set(Var(7));
    auto r = command::call("_test_negate", ctx);
    VE_ASSERT(r.isSuccess());
    VE_ASSERT_EQ(r.content().toInt(), -7);
    GlobalCommandFactory().erase("_test_negate");
}

// ============================================================================
// Object::once tests
// ============================================================================

VE_TEST(object_once_fires_once) {
    enum : Object::SignalT { SIG = 0x1000 };

    Object sender("sender");
    Object observer("observer");
    int count = 0;

    sender.once<SIG>(&observer, [&count](const Var&) { ++count; });

    sender.trigger<SIG>();
    VE_ASSERT_EQ(count, 1);

    sender.trigger<SIG>();
    VE_ASSERT_EQ(count, 1);
}

VE_TEST(object_once_typed) {
    enum : Object::SignalT { SIG = 0x1001 };

    Object sender("sender");
    Object observer("observer");
    int received = 0;

    sender.once<SIG>(&observer, [&received](int v) { received = v; });

    sender.trigger<SIG>(42);
    VE_ASSERT_EQ(received, 42);

    sender.trigger<SIG>(99);
    VE_ASSERT_EQ(received, 42);
}

VE_TEST(object_once_does_not_affect_connect) {
    enum : Object::SignalT { SIG = 0x1002 };

    Object sender("sender");
    Object observer("observer");
    int once_count = 0;
    int connect_count = 0;

    sender.connect<SIG>(&observer, [&connect_count](const Var&) { ++connect_count; });
    sender.once<SIG>(&observer, [&once_count](const Var&) { ++once_count; });

    sender.trigger<SIG>();
    VE_ASSERT_EQ(once_count, 1);
    VE_ASSERT_EQ(connect_count, 1);

    sender.trigger<SIG>();
    VE_ASSERT_EQ(once_count, 1);
    VE_ASSERT_EQ(connect_count, 2);
}

VE_TEST(object_once_multiple) {
    enum : Object::SignalT { SIG = 0x1003 };

    Object sender("sender");
    Object obs1("obs1");
    Object obs2("obs2");
    int count1 = 0, count2 = 0;

    sender.once<SIG>(&obs1, [&count1](const Var&) { ++count1; });
    sender.once<SIG>(&obs2, [&count2](const Var&) { ++count2; });

    sender.trigger<SIG>();
    VE_ASSERT_EQ(count1, 1);
    VE_ASSERT_EQ(count2, 1);

    sender.trigger<SIG>();
    VE_ASSERT_EQ(count1, 1);
    VE_ASSERT_EQ(count2, 1);
}
