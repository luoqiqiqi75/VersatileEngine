// test_command.cpp - current command / pipeline refactor tests

#include "ve_test.h"

#include <ve/core/command.h>
#include <ve/core/loop.h>
#include <ve/core/pipeline.h>
#include <ve/core/node.h>
#include <ve/core/schema.h>

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
    command::reg(command::factory(), "_test_cmd_add",
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
    VE_ASSERT_EQ(command::description("_test_cmd_add"), std::string("add five"));

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
    VE_ASSERT(cmd.contextNode() != nullptr);
    VE_ASSERT_EQ(cmd.inputNode(), cmd.contextNode()->at("in"));
    VE_ASSERT_EQ(cmd.outputNode(), cmd.contextNode()->at("out"));

    cmd.inputNode()->set(41);
    VE_ASSERT_EQ(cmd.run().outputNode()->getInt(), 42);
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
    VE_ASSERT_EQ(cVal.run().outputNode()->getInt(), 7);

    // plain typed args: in is the argument list, arg I = in/I
    Command cSum(command::reg(f, "_test_wrap_sum", [](int a, int b) { return a + b; }));
    cSum.inputNode()->append()->set(40);
    cSum.inputNode()->append()->set(2);
    VE_ASSERT_EQ(cSum.run().outputNode()->getInt(), 42);

    // void(const Var&): consumes args[0]
    Var seen;
    Command cSink(command::reg(f, "_test_wrap_sink", [&](const Var& v) { seen = v; }));
    cSink.inputNode()->append()->set(5);
    VE_ASSERT(cSink.run().result().isSuccess());
    VE_ASSERT_EQ(seen.toInt(), 5);

    // Var in, Var out: the echo shape — return value replaces out
    Command cEcho(command::reg(f, "_test_wrap_echo", [](const Var& v) { return v; }));
    cEcho.inputNode()->append()->set(7);
    VE_ASSERT(cEcho.run().result().isSuccess());
    VE_ASSERT_EQ(cEcho.outputNode()->getInt(), 7);
}

VE_TEST(command_reg_result_returns_pass_through)
{
    auto& f = command::factory();

    // Result return passes through verbatim — nothing is written to out
    Command cCheck(command::reg(f, "_test_wrap_check",
        [](int v) -> Result {
            return v == 1 ? Result::ok() : Result::fail(-42, "refused");
        }));
    Node* arg = cCheck.inputNode()->append();
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
    cNode.inputNode()->append()->set(41);
    VE_ASSERT_EQ(cNode.run().outputNode()->getInt(), 42);
    VE_ASSERT(cNode.result().isSuccess());
}

VE_TEST(command_call_uses_bound_loop)
{
    Loop loop("cmd.loop");
    std::atomic<int> called{0};

    command::reg(command::factory(), "_test_cmd_loop",
        [&](Node*, Node* in, Node* out) -> Result {
            called.fetch_add(1);
            out->set(in->get().toInt() * 2);
            return Result::ok();
        },
        {}, &loop);

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
    p.inputNode()->set(1);
    Node mid;

    auto* c1 = p.addProc([](Node*, Node* in, Node* out) -> Result {
        out->set(in->get().toInt() + 2);
        return Result::ok();
    });
    c1->setContextNodes(p.contextNode(), p.inputNode(), &mid);

    auto* c2 = p.addProc([](Node*, Node* in, Node* out) -> Result {
        out->set(in->get().toInt() * 3);
        return Result::ok();
    });
    c2->setContextNodes(p.contextNode(), &mid, p.outputNode());

    bool finished = false;
    p.onFinished(nullptr, [&](Pipeline& pipe) {
        finished = true;
        VE_ASSERT_EQ(pipe.outputNode()->getInt(), 9);
    });
    p.sync();

    VE_ASSERT(finished);
    VE_ASSERT(p.result().isSuccess());
    VE_ASSERT_EQ(p.outputNode()->getInt(), 9);
}

VE_TEST(pipeline_connects_command_inputs_and_outputs)
{
    command::reg("_test_pipe_double",
        [](Node*, Node* in, Node* out) -> Result {
            out->set(in->get().toInt() * 2);
            return Result::ok();
        });

    Pipeline p;
    Node mid1, mid2;

    auto* c1 = p.addProc([](Node*, Node* in, Node* out) -> Result {
        out->set(in->get().toInt() + 4);
        return Result::ok();
    });
    c1->setContextNodes(p.contextNode(), p.inputNode(), &mid1);

    auto* c2 = p.add(command::create("_test_pipe_double"));
    c2->setContextNodes(p.contextNode(), &mid1, &mid2);

    auto* c3 = p.addProc([](Node*, Node* in, Node* out) -> Result {
        out->set(in->get().toInt() + 1);
        return Result::ok();
    });
    c3->setContextNodes(p.contextNode(), &mid2, p.outputNode());

    p.inputNode()->set(6);
    p.sync();

    VE_ASSERT(p.result().isSuccess());
    VE_ASSERT_EQ(p.outputNode()->getInt(), 21);   // (6 + 4) * 2 + 1
}

VE_TEST(pipeline_aborts_on_error)
{
    Pipeline p;
    p.inputNode()->set(1);
    Node mid;

    int third_ran = 0;
    auto* c1 = p.addProc([](Node*, Node* in, Node* out) -> Result {
        out->set(in->get().toInt() + 1);
        return Result::ok();
    });
    c1->setContextNodes(p.contextNode(), p.inputNode(), &mid);
    p.addProc([](Node*, Node*, Node*) -> Result {
        return Result::fail(-9, "stop");
    });
    p.addProc([&](Node*, Node*, Node* out) -> Result {
        ++third_ran;
        out->set(999);
        return Result::ok();
    });

    bool finished = false;
    p.onFinished(nullptr, [&](Pipeline&) { finished = true; });
    p.sync();

    VE_ASSERT(finished);                            // FINISHED fires on error too; result carries it
    VE_ASSERT(p.result().isError());
    VE_ASSERT_EQ(p.result().code(), -9);
    VE_ASSERT_EQ(p.result().message(), std::string("stop"));
    VE_ASSERT_EQ(third_ran, 0);                     // chain stopped at the failing step
}

VE_TEST(pipeline_async_inline_completes)
{
    bool finished = false;
    Pipeline p;
    auto* c = p.addProc([](Node*, Node*, Node* out) -> Result {
        out->set(42);
        return Result::ok();
    });
    c->setContextNodes(p.contextNode(), p.inputNode(), p.outputNode());
    p.onFinished(nullptr, [&](Pipeline& pipe) {
        finished = true;
        VE_ASSERT(pipe.result().isSuccess());
        VE_ASSERT_EQ(pipe.outputNode()->getInt(), 42);
    });
    p.async();

    VE_ASSERT(finished);
}

VE_TEST(pipeline_cross_loop_proc_chain)
{
    AsioLoop a("pipe.loopA");
    AsioLoop b("pipe.loopB");
    a.start();
    b.start();

    Pipeline p;
    p.inputNode()->set(1);
    Node mid;

    auto* c1 = p.addProc([](Node*, Node* in, Node* out) -> Result {
        out->set(in->get().toInt() + 2);
        return Result::ok();
    }, &a);
    c1->setContextNodes(p.contextNode(), p.inputNode(), &mid);

    auto* c2 = p.addProc([](Node*, Node* in, Node* out) -> Result {
        out->set(in->get().toInt() * 10);
        return Result::ok();
    }, &b);
    c2->setContextNodes(p.contextNode(), &mid, p.outputNode());

    p.sync();

    VE_ASSERT(p.result().isSuccess());
    VE_ASSERT_EQ(p.outputNode()->getInt(), 30);  // (1 + 2) * 10

    a.stop();
    b.stop();
}

VE_TEST(pipeline_cross_loop_async)
{
    AsioLoop a("pipe.async.loopA");
    AsioLoop b("pipe.async.loopB");
    a.start();
    b.start();

    std::atomic<bool> finished{false};
    Pipeline p;
    p.inputNode()->set(3);
    Node mid;

    auto* c1 = p.addProc([](Node*, Node* in, Node* out) -> Result {
        out->set(in->get().toInt() + 4);
        return Result::ok();
    }, &a);
    c1->setContextNodes(p.contextNode(), p.inputNode(), &mid);

    auto* c2 = p.addProc([](Node*, Node* in, Node* out) -> Result {
        out->set(in->get().toInt() * 2);
        return Result::ok();
    }, &b);
    c2->setContextNodes(p.contextNode(), &mid, p.outputNode());

    p.onFinished(nullptr, [&](Pipeline& pipe) {
        VE_ASSERT(pipe.result().isSuccess());
        VE_ASSERT_EQ(pipe.outputNode()->getInt(), 14);  // (3 + 4) * 2
        finished.store(true);
    });
    p.async();

    while (!finished.load()) std::this_thread::yield();
    a.stop();
    b.stop();

    VE_ASSERT(finished.load());
}

// A registered command that takes real wall-clock time, bound to a worker loop
// so it never runs on the calling thread.
static Node* regSlowCommand()
{
    return command::reg("_test_slow",
        [](Node*, Node* in, Node* out) -> Result {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            out->set(in->get().toInt() + 100);
            return Result::ok();
        });
}

VE_TEST(pipeline_slow_command_sync)
{
    auto* slow_n = regSlowCommand();

    AsioLoop worker("slow.sync.worker");
    worker.start();

    Pipeline p;
    p.inputNode()->set(5);
    Command cmd(slow_n);
    cmd.setLoop(&worker);
    auto* c = p.add(cmd);
    c->setContextNodes(p.contextNode(), p.inputNode(), p.outputNode());

    p.sync();

    VE_ASSERT(p.result().isSuccess());
    VE_ASSERT_EQ(p.outputNode()->getInt(), 105);

    worker.stop();
}

VE_TEST(pipeline_slow_command_async)
{
    auto* slow_n = regSlowCommand();

    AsioLoop worker("slow.async.worker");
    worker.start();

    std::atomic<bool> done{false};
    Pipeline p;
    p.inputNode()->set(7);
    Command cmd(slow_n);
    cmd.setLoop(&worker);
    auto* c = p.add(cmd);
    c->setContextNodes(p.contextNode(), p.inputNode(), p.outputNode());

    p.onFinished(nullptr, [&](Pipeline& pipe) {
        VE_ASSERT(pipe.result().isSuccess());
        VE_ASSERT_EQ(pipe.outputNode()->getInt(), 107);  // 7 + 100
        done.store(true);
    });
    p.async();

    VE_ASSERT(!done.load());

    while (!done.load()) std::this_thread::yield();
    worker.stop();

    VE_ASSERT(done.load());
}

VE_TEST(factory_bind_from_instruction)
{
    // The instruction-driven arg binder: CLI tokens -> named input fields, coerced
    // by the input_schema. This is the terminal analog of cmd.input<JsonS>(body).
    auto& f = command::factory();
    Node* n = f.reg("_test_bind",
        Var::callable([](Node*, Node*, Node*) -> Result { return Result::ok(); }));
    schema::JsonS::toNode(n->at("instruction"),
        R"({"usage":"_test_bind <path> [count]",
            "input_schema":{"type":"object",
              "properties":{
                "path":{"type":"string"},
                "count":{"type":"integer"},
                "flag":{"type":"boolean"}},
              "required":["path"]}})");

    // positional path + integer + boolean flag
    {
        Node in;
        std::string err;
        VE_ASSERT(command::bind(f, "_test_bind", ve::Strings{"a/b", "5", "--flag"}, &in, &err));
        VE_ASSERT_EQ(in.get("path").toString(), std::string("a/b"));
        VE_ASSERT_EQ(in.get("count").toInt(), 5);
        VE_ASSERT(in.get("flag").toBool());
    }

    // named flag fills path; positionals then skip it
    {
        Node in;
        VE_ASSERT(command::bind(f, "_test_bind", ve::Strings{"--path", "x"}, &in, nullptr));
        VE_ASSERT_EQ(in.get("path").toString(), std::string("x"));
    }

    // missing required -> fail with message
    {
        Node in;
        std::string err;
        VE_ASSERT(!command::bind(f, "_test_bind", ve::Strings{}, &in, &err));
        VE_ASSERT(!err.empty());
    }

    VE_ASSERT_EQ(command::usage(f, "_test_bind"), std::string("_test_bind <path> [count]"));
}

VE_TEST(factory_bind_resolves_local_ref)
{
    // instruction authors may keep JSON Schema $ref to #/definitions/*;
    // terminal bind/usage must follow them against the factory root.
    auto& f = command::factory();
    Node* n = f.reg("_test_bind_ref",
        Var::callable([](Node*, Node*, Node*) -> Result { return Result::ok(); }));
    schema::JsonS::toNode(n->at("instruction"),
        R"({"usage":"_test_bind_ref <name>",
            "input_schema":{"$ref":"#/definitions/device_selector"}})");
    schema::JsonS::toNode(f.node()->at("definitions/device_selector"),
        R"({"type":"object",
            "properties":{"name":{"type":"string"}},
            "required":["name"],
            "additionalProperties":false})");

    {
        Node in;
        std::string err;
        VE_ASSERT(command::bind(f, "_test_bind_ref", ve::Strings{"body"}, &in, &err));
        VE_ASSERT_EQ(in.get("name").toString(), std::string("body"));
    }
    {
        Node in;
        VE_ASSERT(command::bind(f, "_test_bind_ref", ve::Strings{"--name", "body"}, &in, nullptr));
        VE_ASSERT_EQ(in.get("name").toString(), std::string("body"));
    }
    {
        Node in;
        std::string err;
        VE_ASSERT(!command::bind(f, "_test_bind_ref", ve::Strings{}, &in, &err));
        VE_ASSERT(!err.empty());
    }

    Node* schema = command::inputSchema(f, "_test_bind_ref");
    VE_ASSERT(schema != nullptr);
    VE_ASSERT(schema->find("properties/name") != nullptr);

    // resolveSchemas expands a describe-style copy in place
    Node copy;
    copy.copy(n->find("instruction"));
    VE_ASSERT(copy.find("input_schema/$ref") != nullptr);
    command::resolveSchemas(&copy, f.node());
    VE_ASSERT(copy.find("input_schema/properties/name") != nullptr);
    VE_ASSERT(copy.find("input_schema/$ref") == nullptr);
}
