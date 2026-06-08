// test_command.cpp - current command / pipeline refactor tests

#include "ve_test.h"

#include <ve/core/command.h>
#include <ve/core/loop.h>
#include <ve/core/pipeline.h>
#include <ve/core/node.h>

#include <atomic>

using namespace ve;

namespace {

static Command makeCommand(const std::string& key, Node* ctx, Node* in, Node* out)
{
    return command::create(key, ctx, in, out);
}

} // namespace

VE_TEST(result_code_segments)
{
    Result ok = Result::ok();
    VE_ASSERT(ok.isSuccess());
    VE_ASSERT(!ok.isError());
    VE_ASSERT(!ok.isAccepted());
    VE_ASSERT_EQ(ok.code, 0);

    Result fail = Result::fail(-7, "bad");
    VE_ASSERT(fail.isError());
    VE_ASSERT_EQ(fail.code, -7);
    VE_ASSERT_EQ(fail.message, std::string("bad"));

    Result accepted = Result::accept(9, "queued");
    VE_ASSERT(accepted.isAccepted());
    VE_ASSERT_EQ(accepted.code, 9);
    VE_ASSERT_EQ(accepted.message, std::string("queued"));
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

    Command cmd = makeCommand("_test_cmd_add", &ctx, in, out);
    VE_ASSERT(cmd.valid());
    VE_ASSERT_EQ(cmd.help(), std::string("add five"));

    Result r = cmd.run();
    VE_ASSERT(r.isSuccess());
    VE_ASSERT_EQ(out->get().toInt(), 12);
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

    Command cmd = makeCommand("_test_cmd_loop", &ctx, in, out);
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
    Pipeline p("pipe.linear");
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
    pipeline::start(p, [&](Pipeline& pipe) {
        finished = true;
        VE_ASSERT_EQ(pipe.context()->get("output").toInt(), 9);
    });
    p.wait();

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

    Pipeline p("pipe.command");
    p.addProc([](Node*, Node* in, Node* out) -> Result {
        out->set(in->get().toInt() + 4);
        return Result::ok();
    }, "request", {});

    Command cmd = makeCommand("_test_pipe_double", p.context(), nullptr, nullptr);
    p.addCommand(cmd);

    p.addProc([](Node*, Node* in, Node* out) -> Result {
        out->set(in->get().toInt() + 1);
        return Result::ok();
    }, {}, "reply");

    p.context()->at("request")->set(6);
    pipeline::start(p);
    p.wait();

    VE_ASSERT_EQ(p.state(), Pipeline::DONE);
    VE_ASSERT_EQ(p.context()->get("reply").toInt(), 21);
}

VE_TEST(pipeline_aborts_on_error)
{
    Pipeline p("pipe.error");
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

    pipeline::start(p);
    p.wait();

    VE_ASSERT_EQ(p.state(), Pipeline::ERRORED);
    VE_ASSERT(p.lastResult().isError());
    VE_ASSERT_EQ(p.lastResult().code, -9);
    VE_ASSERT_EQ(p.lastResult().message, std::string("stop"));
    VE_ASSERT(p.context()->get("output").isNull());
}

VE_TEST(pipeline_start_takes_detached_ownership)
{
    struct TrackingPipeline : Pipeline {
        bool* deleted = nullptr;
        TrackingPipeline(const std::string& name, bool* deletedFlag)
            : Pipeline(name), deleted(deletedFlag) {}
        ~TrackingPipeline() { if (deleted) *deleted = true; }
    };

    bool finished = false;
    bool deleted = false;
    auto* p = new TrackingPipeline("pipe.detached", &deleted);
    p->addLinearProc([](Node*, Node*, Node* out) -> Result {
        out->set(42);
        return Result::ok();
    });

    pipeline::start(p, [&](Pipeline& pipe) {
        VE_ASSERT(pipe.lastResult().isSuccess());
        VE_ASSERT_EQ(pipe.context()->get("output").toInt(), 42);
        finished = true;
    });

    VE_ASSERT(finished);
    VE_ASSERT(deleted);
}
