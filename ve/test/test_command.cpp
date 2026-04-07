// test_command.cpp — Tests for Step, Pipeline, Command, Factory, and Object::once

#include "ve_test.h"
#include <ve/core/command.h>
#include <ve/core/pipeline.h>
#include <ve/core/node.h>

using namespace ve;

// ============================================================================
// Step tests
// ============================================================================

VE_TEST(step_basic_exec) {
    Step s("add_one", [](const Var& v) -> Result {
        return Result(Result::SUCCESS, Var(v.toInt() + 1));
    });
    VE_ASSERT_EQ(s.name(), "add_one");
    VE_ASSERT(s);

    Result r = s.exec(Var(10));
    VE_ASSERT(r.isSuccess());
    VE_ASSERT_EQ(r.content().toInt(), 11);
}

VE_TEST(step_empty) {
    Step s;
    VE_ASSERT(!s);
    Result r = s.exec(Var(42));
    VE_ASSERT(r.isError());
}

VE_TEST(step_void_fn) {
    int called = 0;
    Step s("inc", [&called]() { ++called; });
    Result r = s.exec();
    VE_ASSERT(r.isSuccess());
    VE_ASSERT_EQ(called, 1);
}

VE_TEST(step_bool_fn) {
    Step s_ok("ok",  []() -> bool { return true; });
    Step s_fail("fail", []() -> bool { return false; });
    VE_ASSERT(s_ok.exec().isSuccess());
    VE_ASSERT(s_fail.exec().isError());
}

VE_TEST(step_typed_arg) {
    Step s("double_it", [](int x) -> Result {
        return Result(Result::SUCCESS, Var(x * 2));
    });
    Result r = s.exec(Var(7));
    VE_ASSERT(r.isSuccess());
    VE_ASSERT_EQ(r.content().toInt(), 14);
}

VE_TEST(step_clone) {
    int counter = 0;
    Step original("counter", [&counter](const Var&) -> Result {
        ++counter;
        return Result::SUCCESS;
    });

    Step copy = original.clone();
    VE_ASSERT_EQ(copy.name(), "counter");
    copy.exec();
    VE_ASSERT_EQ(counter, 1);
}

VE_TEST(step_metadata) {
    Step s("process", [](const Var&) -> Result { return Result::SUCCESS; });
    s.setInput("integer value").setOutput("doubled value");
    VE_ASSERT_EQ(s.inputDesc(), "integer value");
    VE_ASSERT_EQ(s.outputDesc(), "doubled value");
}

// ============================================================================
// Pipeline tests
// ============================================================================

VE_TEST(pipeline_single_step) {
    Pipeline pipe("test");
    pipe.add(Step("step1", [](const Var& v) -> Result {
        return Result(Result::SUCCESS, Var(v.toInt() + 100));
    }));

    VE_ASSERT_EQ(pipe.stepCount(), 1);
    VE_ASSERT_EQ(pipe.state(), Pipeline::IDLE);

    Result r = pipe.start(Var(5));
    VE_ASSERT_EQ(pipe.state(), Pipeline::DONE);
}

VE_TEST(pipeline_multi_step) {
    std::vector<int> order;
    Pipeline pipe("multi");
    pipe.add(Step("a", [&order](const Var&) -> Result { order.push_back(1); return Result::SUCCESS; }));
    pipe.add(Step("b", [&order](const Var&) -> Result { order.push_back(2); return Result::SUCCESS; }));
    pipe.add(Step("c", [&order](const Var&) -> Result { order.push_back(3); return Result::SUCCESS; }));

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
    pipe.add(Step("ok",   [&order](const Var&) -> Result { order.push_back(1); return Result::SUCCESS; }));
    pipe.add(Step("fail", [&order](const Var&) -> Result { order.push_back(2); return Result::FAIL; }));
    pipe.add(Step("skip", [&order](const Var&) -> Result { order.push_back(3); return Result::SUCCESS; }));

    pipe.start();
    VE_ASSERT_EQ(pipe.state(), Pipeline::ERRORED);
    VE_ASSERT_EQ((int)order.size(), 2);
}

VE_TEST(pipeline_pause_resume) {
    std::vector<int> order;
    Pipeline pipe("pausable");
    pipe.add(Step("a", [&](const Var&) -> Result {
        order.push_back(1);
        pipe.pause();
        return Result::SUCCESS;
    }));
    pipe.add(Step("b", [&order](const Var&) -> Result { order.push_back(2); return Result::SUCCESS; }));

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
    pipe.add(Step("a", [&](const Var&) -> Result {
        order.push_back(1);
        pipe.pause();
        return Result::SUCCESS;
    }));
    pipe.add(Step("b", [&order](const Var&) -> Result { order.push_back(2); return Result::SUCCESS; }));

    pipe.start();
    VE_ASSERT_EQ(pipe.state(), Pipeline::PAUSED);

    pipe.stop();
    VE_ASSERT_EQ(pipe.state(), Pipeline::IDLE);
    VE_ASSERT_EQ((int)order.size(), 1);
}

VE_TEST(pipeline_rerun) {
    int count = 0;
    Pipeline pipe("rerun");
    pipe.add(Step("inc", [&count](const Var&) -> Result { ++count; return Result::SUCCESS; }));

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
    pipe.add(Step("ok", [](const Var&) -> Result { return Result::SUCCESS; }));
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
    pipe.add(Step("ok", [](const Var&) -> Result { return Result::SUCCESS; }));
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
    pipe.add(Step("fail", [](const Var&) -> Result { return Result::FAIL; }));
    pipe.connect<Pipeline::CMD_ERROR>(&observer, [&got_error](const Var&) {
        got_error = true;
    });
    pipe.start();
    VE_ASSERT(got_error);
}

VE_TEST(pipeline_clone) {
    int count = 0;
    Pipeline pipe("orig");
    pipe.add(Step("inc", [&count](const Var&) -> Result { ++count; return Result::SUCCESS; }));

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
    cmd.addStep("validate", [](const Var&) -> Result { return Result::SUCCESS; });
    cmd.addStep("build",    [](const Var&) -> Result { return Result::SUCCESS; });

    VE_ASSERT_EQ(cmd.name(), "deploy");
    VE_ASSERT_EQ(cmd.stepCount(), 2);
}

VE_TEST(command_pipeline_creation) {
    int order_val = 0;
    Command cmd("test");
    cmd.addStep("a", [&order_val](const Var&) -> Result { order_val = 1; return Result::SUCCESS; });

    Pipeline* pipe = cmd.pipeline();
    VE_ASSERT(pipe != nullptr);
    VE_ASSERT_EQ(pipe->stepCount(), 1);

    pipe->start();
    VE_ASSERT_EQ(order_val, 1);
    VE_ASSERT_EQ(pipe->state(), Pipeline::DONE);
    delete pipe;
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
    command::reg("_test_greet", Step("_test_greet", [](const Var& v) -> Result {
        return Result(Result::SUCCESS, Var("hello " + v.toString()));
    }));
    VE_ASSERT(command::has("_test_greet"));

    auto r = command::call("_test_greet", Var("world"));
    VE_ASSERT(r.isSuccess());
    VE_ASSERT_EQ(r.content().toString(), "hello world");

    GlobalCommandFactory().erase("_test_greet");
}

VE_TEST(command_factory_register_and_exec) {
    GlobalCommandFactory().insertOne("_test_deploy", []() -> Command* {
        auto* cmd = new Command("_test_deploy");
        cmd->addStep("v", [](const Var&) -> Result { return Result::SUCCESS; });
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
    command::reg("_test_echo", Step("_test_echo", [](const Var& v) -> Result {
        return Result(Result::SUCCESS, v);
    }), "echo input");

    VE_ASSERT(command::has("_test_echo"));
    VE_ASSERT_EQ(command::help("_test_echo"), "echo input");

    Result r = command::call("_test_echo", Var(42));
    VE_ASSERT(r.isSuccess());

    GlobalCommandFactory().erase("_test_echo");
}

VE_TEST(command_ns_run) {
    GlobalCommandFactory().insertOne("_test_multi", []() -> Command* {
        auto* cmd = new Command("_test_multi");
        cmd->addStep("a", [](const Var&) -> Result { return Result::SUCCESS; });
        cmd->addStep("b", [](const Var&) -> Result { return Result::SUCCESS; });
        return cmd;
    });

    Pipeline* pipe = command::run("_test_multi", Var());
    VE_ASSERT(pipe != nullptr);
    VE_ASSERT_EQ(pipe->state(), Pipeline::DONE);
    delete pipe;

    GlobalCommandFactory().erase("_test_multi");
}

VE_TEST(command_ns_step) {
    command::reg("_test_inc", Step("_test_inc", [](const Var& v) -> Result {
        return Result(Result::SUCCESS, Var(v.toInt() + 1));
    }));

    Pipeline* pipe = command::run("_test_inc", Var(10));
    VE_ASSERT(pipe != nullptr);
    VE_ASSERT_EQ(pipe->state(), Pipeline::DONE);
    delete pipe;

    GlobalCommandFactory().erase("_test_inc");
}

VE_TEST(command_ns_not_found) {
    Result r = command::call("_test_nonexistent");
    VE_ASSERT(r.isError());

    Pipeline* pipe = command::run("_test_nonexistent");
    VE_ASSERT(pipe == nullptr);
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
