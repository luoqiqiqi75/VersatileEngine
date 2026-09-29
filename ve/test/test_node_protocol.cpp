#include "ve_test.h"

#include "../src/service/node_commands.h"

#include <ve/core/node.h>
#include <ve/core/command.h>
#include <ve/core/factory.h>
#include <ve/core/schema.h>
#include <ve/core/pipeline.h>
#include <ve/core/res.h>

using namespace ve;

VE_SETUP(node_protocol) {
    auto& f = factory::at("service/op");
    schema::JsonS::toNode(f.node(), std::string(res::read("ve/service/op.json")));
    service::registerNodeCommands(f);
}

static bool runEnvelope(service::Session* session, Pipeline& pipe)
{
    pipe.contextNode()->set("_session", Var::ptr(session));

    Node* batch_n = pipe.contextNode()->find("batch");
    if (batch_n) {
        Node* out = pipe.contextNode()->at("data");
        for (auto* item : batch_n->children()) {
            auto ref = service::resolveCmd(item);
            if (!ref.factory) {
                pipe.contextNode()->set("code", int64_t(service::ERR_NOT_FOUND));
                pipe.contextNode()->set("message", "unknown: " + ref.key);
                return true;
            }
            Command* c = pipe.add(command::create(*ref.factory, ref.key));
            c->setContextNodes(pipe.contextNode(), item->at("params"), out->append());
        }
    } else {
        auto ref = service::resolveCmd(pipe.contextNode());
        if (!ref.factory) {
            pipe.contextNode()->set("code", int64_t(ref.key.empty() ? service::ERR_INVALID : service::ERR_NOT_FOUND));
            pipe.contextNode()->set("message", ref.key.empty() ? std::string("op or cmd required") : "unknown: " + ref.key);
            return true;
        }
        Command* c = pipe.add(command::create(*ref.factory, ref.key));
        c->setContextNodes(pipe.contextNode(), pipe.contextNode()->at("params"), pipe.contextNode()->at("data"));
    }

    pipe.sync();
    return service::finalizeReply(pipe);
}

VE_TEST(node_async_batch_dispatches_registered_and_default_loops)
{
    AsioLoop connection("test.node.async.connection");
    AsioLoop worker("test.node.async");
    connection.start();
    worker.start();
    std::atomic<int> calls{0};
    std::atomic<bool> wrong_loop{false};
    auto bound = command::reg("_test.node.bound_async", [&](Node*, Node*, Node*) {
        if (loop::current() != &worker) wrong_loop = true;
        ++calls;
        return Result::ok();
    });
    bound->set("loop", Var::ptr(static_cast<Loop*>(&worker)));
    Node root;
    root.set("value", 123);
    auto session = std::make_shared<service::Session>(&root, &root);
    std::atomic<int> acknowledgements{0};
    std::promise<std::string> completed;
    auto finished = completed.get_future();
    connection.post([&] {
        Pipeline pipe;
        schema::JsonS::toNode(pipe.contextNode(), R"({"async":true,"batch":[
            {"cmd":"_test.node.bound_async"},{"op":"get","params":{"path":"value"}}
        ]})");
        service::executeNodeRequest(pipe, session, [&](const Node& reply) {
            if (reply.get("accepted").toBool(false)) {
                ++acknowledgements;
            } else {
                if (loop::current() != &connection) wrong_loop = true;
                completed.set_value(schema::fromNode<schema::JsonS>(&reply, schema::JsonS::compact()));
            }
        });
    });
    Node reply;
    schema::JsonS::toNode(&reply, finished.get());
    connection.stop();
    worker.stop();
    bound->remove("loop");
    VE_ASSERT_EQ(reply.get("code").toInt(-1), 0);
    VE_ASSERT_EQ(reply.find("data")->child(1)->get("value").toInt(), 123);
    VE_ASSERT_EQ(acknowledgements.load(), 1);
    VE_ASSERT_EQ(calls.load(), 1);
    VE_ASSERT(!wrong_loop.load());
}

VE_TEST(node_async_option_is_separate_from_command_params)
{
    bool input_async = false;
    command::reg("_test.node.async_param", [&](Node*, Node* in, Node* out) {
        input_async = in->get("async").toBool(false);
        out->set("value", in->get("value"));
        return Result::ok();
    });
    Node root;
    auto session = std::make_shared<service::Session>(&root, &root);
    Pipeline pipe;
    schema::JsonS::toNode(pipe.contextNode(),
        R"({"cmd":"_test.node.async_param","async":false,"params":{"async":true,"value":123}})");
    int value = 0;
    service::executeNodeRequest(pipe, session, [&](const Node& reply) {
        value = reply.get("data/value").toInt();
    });
    VE_ASSERT(input_async);
    VE_ASSERT_EQ(value, 123);
}

VE_TEST(node_foreground_batch_preserves_each_command_loop)
{
    AsioLoop connection("test.node.connection");
    AsioLoop worker("test.node.worker");
    connection.start();
    worker.start();
    std::atomic<bool> wrong_loop{false};
    auto bound = command::reg("_test.node.batch_bound", [&](Node*, Node*, Node* out) {
        if (loop::current() != &worker) wrong_loop = true;
        out->set("value", 1);
        return Result::ok();
    });
    bound->set("loop", Var::ptr(static_cast<Loop*>(&worker)));
    command::reg("_test.node.batch_unbound", [&](Node*, Node*, Node* out) {
        if (loop::current() != &connection) wrong_loop = true;
        out->set("value", 2);
        return Result::ok();
    });
    Node root;
    auto session = std::make_shared<service::Session>(&root, &root);
    std::promise<std::string> completed;
    auto finished = completed.get_future();
    connection.post([&] {
        Pipeline pipe;
        schema::JsonS::toNode(pipe.contextNode(), R"({"batch":[
            {"cmd":"_test.node.batch_bound"},{"cmd":"_test.node.batch_unbound"},
            {"cmd":"_test.node.batch_bound"}
        ]})");
        service::executeNodeRequest(pipe, session, [&](const Node& reply) {
            completed.set_value(schema::fromNode<schema::JsonS>(&reply, schema::JsonS::compact()));
        });
    });
    Node reply;
    schema::JsonS::toNode(&reply, finished.get());
    connection.stop();
    worker.stop();
    bound->remove("loop");
    VE_ASSERT(!wrong_loop.load());
    VE_ASSERT_EQ(reply.get("code").toInt(-1), 0);
    VE_ASSERT_EQ(reply.find("data")->count(), 3);
    VE_ASSERT_EQ(reply.find("data")->child(1)->get("value").toInt(), 2);
}

VE_TEST(node_dispatch_get_set_and_children) {
    Node root("root");
    service::Session session(&root, &root);

    // set
    {
        Pipeline pipe;
        pipe.contextNode()->set("op", "set");
        pipe.contextNode()->at("params")->set("path", "a/value");
        pipe.contextNode()->at("params")->at("value")->set(Var(42));
        runEnvelope(&session, pipe);
        VE_ASSERT_EQ(pipe.contextNode()->get("code").toInt(-1), 0);
        VE_ASSERT_EQ(root.find("a/value")->getInt(), 42);
    }

    // get
    {
        Pipeline pipe;
        pipe.contextNode()->set("op", "get");
        pipe.contextNode()->at("params")->set("path", "a/value");
        runEnvelope(&session, pipe);
        VE_ASSERT_EQ(pipe.contextNode()->get("code").toInt(-1), 0);
        VE_ASSERT_EQ(pipe.contextNode()->get("data/value").toInt(), 42);
    }

    // children
    {
        Pipeline pipe;
        pipe.contextNode()->set("op", "children");
        pipe.contextNode()->at("params")->set("path", "a");
        runEnvelope(&session, pipe);
        VE_ASSERT_EQ(pipe.contextNode()->get("code").toInt(-1), 0);
        Node* children = pipe.contextNode()->find("data/children");
        VE_ASSERT(children != nullptr);
        VE_ASSERT_EQ(children->count(), 1);
    }
}

VE_TEST(node_dispatch_batch) {
    Node root("root");
    root.set("one", 1);
    root.set("two", 2);
    service::Session session(&root, &root);

    Pipeline pipe;
    Node* batch = pipe.contextNode()->at("batch");

    Node* item1 = batch->append();
    item1->set("op", "get");
    item1->at("params")->set("path", "one");

    Node* item2 = batch->append();
    item2->set("op", "get");
    item2->at("params")->set("path", "two");

    runEnvelope(&session, pipe);
    VE_ASSERT_EQ(pipe.contextNode()->get("code").toInt(-1), 0);
    Node* data = pipe.contextNode()->find("data");
    VE_ASSERT(data != nullptr);
    VE_ASSERT_EQ(data->count(), 2);
    VE_ASSERT_EQ(data->child(0)->get("value").toInt(), 1);
    VE_ASSERT_EQ(data->child(1)->get("value").toInt(), 2);
}

VE_TEST(node_dispatch_subscribe_unsupported_without_send) {
    Node root("root");
    service::Session session(&root, &root);

    Pipeline pipe;
    pipe.contextNode()->set("op", "subscribe");
    pipe.contextNode()->at("params")->set("path", "a");
    runEnvelope(&session, pipe);
    VE_ASSERT(pipe.contextNode()->get("code").toInt(0) < 0);
}

VE_TEST(node_dispatch_export_full) {
    Node root("root");
    root.set("a/x", 1);
    root.set("a/y", 2);
    root.set("a/deep/z", 3);
    service::Session session(&root, &root);

    Pipeline pipe;
    pipe.contextNode()->set("op", "export");
    pipe.contextNode()->at("params")->set("path", "a");
    runEnvelope(&session, pipe);
    VE_ASSERT_EQ(pipe.contextNode()->get("code").toInt(-1), 0);
    Node* tree = pipe.contextNode()->find("data/tree");
    VE_ASSERT(tree != nullptr);
    VE_ASSERT_EQ(tree->get("x").toInt(), 1);
    VE_ASSERT_EQ(tree->get("y").toInt(), 2);
    VE_ASSERT_EQ(tree->get("deep/z").toInt(), 3);
}

VE_TEST(node_dispatch_export_depth) {
    Node root("root");
    root.set("a/x", 1);
    root.set("a/deep/z", 3);
    service::Session session(&root, &root);

    Pipeline pipe;
    pipe.contextNode()->set("op", "export");
    pipe.contextNode()->at("params")->set("path", "a");
    pipe.contextNode()->at("params")->set("depth", int64_t(1));
    runEnvelope(&session, pipe);
    VE_ASSERT_EQ(pipe.contextNode()->get("code").toInt(-1), 0);
    Node* tree = pipe.contextNode()->find("data/tree");
    VE_ASSERT(tree != nullptr);
    VE_ASSERT_EQ(tree->get("x").toInt(), 1);
    // depth=1: "deep" child exists but "deep/z" not exported
    VE_ASSERT(tree->find("deep") != nullptr);
    VE_ASSERT(tree->find("deep/z") == nullptr);
}

VE_TEST(node_dispatch_import) {
    Node root("root");
    service::Session session(&root, &root);

    Pipeline pipe;
    pipe.contextNode()->set("op", "import");
    pipe.contextNode()->at("params")->set("path", "b");
    pipe.contextNode()->at("params")->at("tree")->set("x", 10);
    pipe.contextNode()->at("params")->at("tree")->set("y", 20);
    runEnvelope(&session, pipe);
    VE_ASSERT_EQ(pipe.contextNode()->get("code").toInt(-1), 0);
    VE_ASSERT_EQ(root.get("b/x").toInt(), 10);
    VE_ASSERT_EQ(root.get("b/y").toInt(), 20);
}

VE_TEST(node_dispatch_watch_with_session_pushes) {
    Node root("root");
    root.set("watch/me", 1);

    std::string lastPush;
    service::Session session(&root, &root, [&](std::string msg) {
        lastPush = std::move(msg);
    });

    // subscribe
    {
        Pipeline pipe;
        pipe.contextNode()->set("op", "subscribe");
        pipe.contextNode()->at("params")->set("path", "watch/me");
        runEnvelope(&session, pipe);
        VE_ASSERT_EQ(pipe.contextNode()->get("code").toInt(-1), 0);
    }

    // trigger change
    root.find("watch/me")->set(7);
    VE_ASSERT(!lastPush.empty());

    // verify event JSON contains the data
    Node event;
    schema::toNode<schema::JsonS>(&event, lastPush);
    VE_ASSERT_EQ(event.get("event").toString(), std::string("node.changed"));
    VE_ASSERT_EQ(event.find("data")->getInt(), 7);

    // unsubscribe
    lastPush.clear();
    {
        Pipeline pipe;
        pipe.contextNode()->set("op", "unsubscribe");
        pipe.contextNode()->at("params")->set("path", "watch/me");
        runEnvelope(&session, pipe);
        VE_ASSERT_EQ(pipe.contextNode()->get("code").toInt(-1), 0);
    }

    root.find("watch/me")->set(9);
    VE_ASSERT(lastPush.empty());
}

VE_TEST(node_session_disconnect_isolated_from_subscriber) {
    Node root("root");
    root.set("watch/me", 1);

    std::string subscriberPush;
    service::Session subscriber(&root, &root, [&](std::string msg) {
        subscriberPush = std::move(msg);
    });

    {
        Pipeline pipe;
        pipe.contextNode()->set("op", "subscribe");
        pipe.contextNode()->at("params")->set("path", "watch/me");
        runEnvelope(&subscriber, pipe);
        VE_ASSERT_EQ(pipe.contextNode()->get("code").toInt(-1), 0);
    }

    std::string disconnectedPush;
    {
        service::Session disconnectedClient(&root, &root, [&](std::string msg) {
            disconnectedPush = std::move(msg);
        });
        Pipeline pipe;
        pipe.contextNode()->set("op", "subscribe");
        pipe.contextNode()->at("params")->set("path", "watch/me");
        runEnvelope(&disconnectedClient, pipe);
        VE_ASSERT_EQ(pipe.contextNode()->get("code").toInt(-1), 0);
    }

    root.find("watch/me")->set(7);
    VE_ASSERT(!subscriberPush.empty());
    VE_ASSERT(disconnectedPush.empty());

    Node event;
    schema::toNode<schema::JsonS>(&event, subscriberPush);
    VE_ASSERT_EQ(event.get("event").toString(), std::string("node.changed"));
    VE_ASSERT_EQ(event.find("data")->getInt(), 7);
}
