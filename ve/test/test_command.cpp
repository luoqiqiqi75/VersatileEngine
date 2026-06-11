// test_command.cpp - current command / pipeline refactor tests

#include "ve_test.h"

#include <ve/core/command.h>
#include <ve/core/loop.h>
#include <ve/core/pipeline.h>
#include <ve/core/node.h>

#include <atomic>
#include <chrono>
#include <thread>

using namespace ve;

VE_TEST(result_code_segments)
{
    Result ok = Result::ok();
    VE_ASSERT(ok.isSuccess());
    VE_ASSERT(!ok.isError());
    VE_ASSERT(!ok.isAccepted());
    VE_ASSERT_EQ(ok.code(), 0);

    Result fail = Result::fail(-7, "bad");
    VE_ASSERT(fail.isError());
    VE_ASSERT_EQ(fail.code(), -7);
    VE_ASSERT_EQ(fail.message(), std::string("bad"));

    Result accepted = Result::accept(9, "queued");
    VE_ASSERT(accepted.isAccepted());
    VE_ASSERT_EQ(accepted.code(), 9);
    VE_ASSERT_EQ(accepted.message(), std::string("queued"));
}

VE_TEST(factory_reg_proc_and_command_run)
{
    command::factory().reg("_test_cmd_add",
        [](Node*, Node* in, Node* out) -> Result {
            out->set(in->get().toInt() + 5);
            return Result::ok();
        },
        "add five");

    Node ctx("ctx");
    Node* in = ctx.at("in");
    Node* out = ctx.at("out");
    in->set(7);

    Command cmd = command::create("_test_cmd_add", &ctx, in, out);
    VE_ASSERT(cmd.valid());
    VE_ASSERT_EQ(cmd.help(), std::string("add five"));

    Result r = cmd.run().result();   // run() is chainable, result() reads back
    VE_ASSERT(r.isSuccess());
    VE_ASSERT_EQ(out->get().toInt(), 12);
}

VE_TEST(command_single_step_run_with_internal_context)
{
    // The /health idiom: a single-step sync command needs no pipeline and no
    // external ctx — Command(fn) defaults ctx to its internal node, in/out to
    // ctx/in and ctx/out, and run() chains straight into output().
    auto* fn = command::reg(command::factory(), "_test_single_inc",
        [](Node*, Node* in, Node* out) -> Result {
            out->set(in->get().toInt(0) + 1);
            return Result::ok();
        });

    Command cmd(fn);
    VE_ASSERT(cmd.valid());
    VE_ASSERT(cmd.context() != nullptr);
    VE_ASSERT_EQ(cmd.input(), cmd.context()->at("in"));
    VE_ASSERT_EQ(cmd.output(), cmd.context()->at("out"));

    cmd.input()->set(41);
    VE_ASSERT_EQ(cmd.run().output()->getInt(), 42);
    VE_ASSERT(cmd.result().isSuccess());
}

VE_TEST(convert_parse_selects_callable_overload)
{
    // The FnTraits overload must actually be selected: an unmatched
    // convert::parse falls back to the primary template and leaves the Proc empty.
    Proc p0;
    VE_ASSERT(convert::parse([] {}, p0));
    VE_ASSERT(p0 != nullptr);

    Proc p1;
    VE_ASSERT(convert::parse([]() -> int64_t { return 1; }, p1));   // any Var-able return
    VE_ASSERT(p1 != nullptr);

    Proc p2;
    VE_ASSERT(convert::parse([](const Var& v) -> Var { return v; }, p2));
    VE_ASSERT(p2 != nullptr);
}

VE_TEST(command_reg_wraps_plain_callables)
{
    auto& f = command::factory();

    // void(): pure side effect, always ok
    int hits = 0;
    Command cVoid(command::reg(f, "_test_wrap_void", [&] { ++hits; }));
    VE_ASSERT(cVoid.run().result().isSuccess());
    VE_ASSERT_EQ(hits, 1);

    // R(): produced value lands on the out node (the /health timestamp shape)
    Command cVal(command::reg(f, "_test_wrap_val", [] { return int64_t(7); }));
    VE_ASSERT_EQ(cVal.run().output()->getInt(), 7);

    // plain typed args: in is the argument list, arg I = in/I
    Command cSum(command::reg(f, "_test_wrap_sum", [](int a, int b) { return a + b; }));
    cSum.input()->append()->set(40);
    cSum.input()->append()->set(2);
    VE_ASSERT_EQ(cSum.run().output()->getInt(), 42);

    // void(const Var&): consumes args[0]
    Var seen;
    Command cSink(command::reg(f, "_test_wrap_sink", [&](const Var& v) { seen = v; }));
    cSink.input()->append()->set(5);
    VE_ASSERT(cSink.run().result().isSuccess());
    VE_ASSERT_EQ(seen.toInt(), 5);

    // Var in, Var out: the echo shape — return value replaces out
    Command cEcho(command::reg(f, "_test_wrap_echo", [](const Var& v) { return v; }));
    cEcho.input()->append()->set(7);
    VE_ASSERT(cEcho.run().result().isSuccess());
    VE_ASSERT_EQ(cEcho.output()->getInt(), 7);
}

VE_TEST(command_reg_result_returns_pass_through)
{
    auto& f = command::factory();

    // Result return passes through verbatim — nothing is written to out
    Command cCheck(command::reg(f, "_test_wrap_check",
        [](int v) -> Result {
            return v == 1 ? Result::ok() : Result::fail(-42, "refused");
        }));
    Node* arg = cCheck.input()->append();
    arg->set(1);
    VE_ASSERT(cCheck.run().result().isSuccess());
    arg->set(2);
    VE_ASSERT_EQ(cCheck.run().result().code(), -42);
    VE_ASSERT_EQ(cCheck.result().message(), std::string("refused"));

    // Result(Node* in, Node* out): Proc without ctx, the ros-module shape
    Command cNode(command::reg(f, "_test_wrap_node",
        [](Node* in, Node* out) -> Result {
            out->set(in->get(0).toInt() + 1);
            return Result::ok();
        }));
    cNode.input()->append()->set(41);
    VE_ASSERT_EQ(cNode.run().output()->getInt(), 42);
    VE_ASSERT(cNode.result().isSuccess());
}

VE_TEST(command_call_uses_bound_loop)
{
    Loop loop("cmd.loop");
    std::atomic<int> called{0};

    command::factory().reg("_test_cmd_loop",
        [&](Node*, Node* in, Node* out) -> Result {
            called.fetch_add(1);
            out->set(in->get().toInt() * 2);
            return Result::ok();
        },
        {},
        &loop);

    Node ctx("ctx");
    Node* in = ctx.at("in");
    Node* out = ctx.at("out");
    in->set(11);

    Command cmd = command::create("_test_cmd_loop", &ctx, in, out);
    bool finished = false;
    cmd.call([&](Command& done) {
        finished = true;
        VE_ASSERT(done.result().isSuccess());
    });

    VE_ASSERT(finished);
    VE_ASSERT_EQ(called.load(), 1);
    VE_ASSERT_EQ(out->get().toInt(), 22);
}

VE_TEST(var_invoke_packs_pointer_args)
{
    Var callable = Var::callable([](Node* ctx, Node* in, Node* out) -> Result {
        VE_ASSERT(ctx);
        out->set(in->get().toInt() + 1);
        return Result::ok();
    });

    Node ctx("ctx");
    Node* in = ctx.at("in");
    Node* out = ctx.at("out");
    in->set(3);

    Result r = callable.invoke(&ctx, in, out).as<Result>();
    VE_ASSERT(r.isSuccess());
    VE_ASSERT_EQ(out->get().toInt(), 4);
}

VE_TEST(pipeline_linear_proc_chain)
{
    Pipeline p;
    p.context()->at("input")->set(1);

    p.addLinearProc([](Node*, Node* in, Node* out) -> Result {
        out->set(in->get().toInt() + 2);
        return Result::ok();
    });
    p.addLinearProc([](Node*, Node* in, Node* out) -> Result {
        out->set(in->get().toInt() * 3);
        return Result::ok();
    });

    bool finished = false;
    p.onFinished([&](Pipeline& pipe) {
        finished = true;
        VE_ASSERT_EQ(pipe.context()->get("output").toInt(), 9);
    });
    p.sync();

    VE_ASSERT(finished);
    VE_ASSERT_EQ(p.state(), Pipeline::DONE);
    VE_ASSERT(p.lastResult().isSuccess());
    VE_ASSERT_EQ(p.context()->get("output").toInt(), 9);

    Node* commands = p.context()->find("_pipe/commands");
    VE_ASSERT(commands);
    VE_ASSERT_EQ(commands->count(), 2);
    VE_ASSERT(commands->child(0)->find("factory"));
    VE_ASSERT(commands->child(1)->find("factory"));
}

VE_TEST(pipeline_connects_command_inputs_and_outputs)
{
    command::factory().reg("_test_pipe_double",
        [](Node*, Node* in, Node* out) -> Result {
            out->set(in->get().toInt() * 2);
            return Result::ok();
        });

    Pipeline p;
    p.addProc([](Node*, Node* in, Node* out) -> Result {
        out->set(in->get().toInt() + 4);
        return Result::ok();
    }, "request", {});

    Command cmd = command::create("_test_pipe_double", p.context(), nullptr, nullptr);
    p.addCommand(cmd);

    p.addProc([](Node*, Node* in, Node* out) -> Result {
        out->set(in->get().toInt() + 1);
        return Result::ok();
    }, {}, "reply");

    p.context()->at("request")->set(6);
    p.sync();

    VE_ASSERT_EQ(p.state(), Pipeline::DONE);
    VE_ASSERT_EQ(p.context()->get("reply").toInt(), 21);
}

VE_TEST(pipeline_aborts_on_error)
{
    Pipeline p;
    p.context()->at("input")->set(1);

    p.addLinearProc([](Node*, Node* in, Node* out) -> Result {
        out->set(in->get().toInt() + 1);
        return Result::ok();
    });
    p.addLinearProc([](Node*, Node*, Node*) -> Result {
        return Result::fail(-9, "stop");
    });
    p.addLinearProc([](Node*, Node*, Node* out) -> Result {
        out->set(999);
        return Result::ok();
    });

    p.sync();

    VE_ASSERT_EQ(p.state(), Pipeline::ERRORED);
    VE_ASSERT(p.lastResult().isError());
    VE_ASSERT_EQ(p.lastResult().code(), -9);
    VE_ASSERT_EQ(p.lastResult().message(), std::string("stop"));
    VE_ASSERT(p.context()->get("output").isNull());
}

VE_TEST(pipeline_async_runs_to_completion)
{
    AsioLoop loop("test.async");
    loop.start();

    std::atomic<bool> finished{false};
    Pipeline p;
    p.addLinearProc([](Node*, Node*, Node* out) -> Result {
        out->set(42);
        return Result::ok();
    });

    // async moves the handle and posts the first dispatch onto the given loop;
    // the shared graph keeps itself alive until completion, then frees. It does
    // NOT block the caller, so we wait on the completion callback.
    pipeline::async(std::move(p), &loop, [&](Pipeline& pipe) {
        VE_ASSERT(pipe.lastResult().isSuccess());
        VE_ASSERT_EQ(pipe.context()->get("output").toInt(), 42);
        finished.store(true);
    });

    while (!finished.load()) std::this_thread::yield();
    loop.stop();

    VE_ASSERT(finished.load());
}

VE_TEST(pipeline_cross_loop_proc_chain)
{
    // Two procs bound to two different running loops; sync() pumps a third
    // (caller-side) loop while the work hops loopA -> loopB and back to finish.
    AsioLoop a("pipe.loopA");
    AsioLoop b("pipe.loopB");
    AsioLoop driver("pipe.driver");   // not started: sync() pumps it on this thread
    a.start();
    b.start();

    Pipeline p;
    p.context()->at("input")->set(1);

    p.addLinearProc([](Node*, Node* in, Node* out) -> Result {
        out->set(in->get().toInt() + 2);
        return Result::ok();
    }, &a);
    p.addLinearProc([](Node*, Node* in, Node* out) -> Result {
        out->set(in->get().toInt() * 10);
        return Result::ok();
    }, &b);

    p.sync(&driver);

    VE_ASSERT_EQ(p.state(), Pipeline::DONE);
    VE_ASSERT(p.lastResult().isSuccess());
    VE_ASSERT_EQ(p.context()->get("output").toInt(), 30);  // (1 + 2) * 10

    a.stop();
    b.stop();
}

VE_TEST(pipeline_cross_loop_async)
{
    // Same cross-loop graph driven by async: no blocking, completion via callback.
    AsioLoop a("pipe.async.loopA");
    AsioLoop b("pipe.async.loopB");
    AsioLoop driver("pipe.async.driver");
    a.start();
    b.start();
    driver.start();

    std::atomic<bool> finished{false};
    Pipeline p;
    p.context()->at("input")->set(3);

    p.addLinearProc([](Node*, Node* in, Node* out) -> Result {
        out->set(in->get().toInt() + 4);
        return Result::ok();
    }, &a);
    p.addLinearProc([](Node*, Node* in, Node* out) -> Result {
        out->set(in->get().toInt() * 2);
        return Result::ok();
    }, &b);

    pipeline::async(std::move(p), &driver, [&](Pipeline& pipe) {
        VE_ASSERT(pipe.lastResult().isSuccess());
        VE_ASSERT_EQ(pipe.context()->get("output").toInt(), 14);  // (3 + 4) * 2
        finished.store(true);
    });

    while (!finished.load()) std::this_thread::yield();
    driver.stop();
    a.stop();
    b.stop();

    VE_ASSERT(finished.load());
}

// A registered command that takes real wall-clock time, bound to a worker loop
// so it never runs on the calling/driver thread.
static void regSlowCommand()
{
    command::factory().reg("_test_slow",
        [](Node*, Node* in, Node* out) -> Result {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            out->set(in->get().toInt() + 100);
            return Result::ok();
        },
        "sleep 50ms then add 100");
}

VE_TEST(pipeline_slow_command_sync)
{
    regSlowCommand();

    AsioLoop worker("slow.sync.worker");
    AsioLoop driver("slow.sync.driver");   // not started: sync() pumps it here
    worker.start();

    Pipeline p;
    p.context()->at("input")->set(5);
    Command cmd = command::create("_test_slow", p.context(), nullptr, nullptr);
    p.addCommand(cmd, &worker);             // command runs on the worker loop

    // Blocks here, pumping `driver`, until the 50ms command finishes off-thread.
    p.sync(&driver);

    VE_ASSERT_EQ(p.state(), Pipeline::DONE);
    VE_ASSERT(p.lastResult().isSuccess());
    VE_ASSERT_EQ(p.context()->get("output").toInt(), 105);

    worker.stop();
}

VE_TEST(pipeline_slow_command_async)
{
    regSlowCommand();

    AsioLoop worker("slow.async.worker");
    AsioLoop driver("slow.async.driver");
    worker.start();
    driver.start();

    std::atomic<bool> done{false};
    Pipeline p;
    p.context()->at("input")->set(7);
    Command cmd = command::create("_test_slow", p.context(), nullptr, nullptr);
    p.addCommand(cmd, &worker);

    pipeline::async(std::move(p), &driver, [&](Pipeline& pipe) {
        VE_ASSERT(pipe.lastResult().isSuccess());
        VE_ASSERT_EQ(pipe.context()->get("output").toInt(), 107);  // 7 + 100
        done.store(true);
    });

    // async must NOT block: the 50ms command cannot have finished yet.
    VE_ASSERT(!done.load());

    while (!done.load()) std::this_thread::yield();
    worker.stop();
    driver.stop();

    VE_ASSERT(done.load());
}
