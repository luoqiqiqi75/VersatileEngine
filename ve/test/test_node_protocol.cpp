#include "ve_test.h"

#include "../src/service/node_commands.h"

#include <ve/core/node.h>
#include <ve/core/command.h>
#include <ve/core/schema.h>
#include <ve/core/pipeline.h>

using namespace ve;

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

VE_TEST(node_dispatch_get_set_and_children) {
    service::registerNodeCommands();
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
    service::registerNodeCommands();
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
    service::registerNodeCommands();
    Node root("root");
    service::Session session(&root, &root);

    Pipeline pipe;
    pipe.contextNode()->set("op", "subscribe");
    pipe.contextNode()->at("params")->set("path", "a");
    runEnvelope(&session, pipe);
    VE_ASSERT(pipe.contextNode()->get("code").toInt(0) < 0);
}

VE_TEST(node_dispatch_export_full) {
    service::registerNodeCommands();
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
    service::registerNodeCommands();
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
    service::registerNodeCommands();
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
    service::registerNodeCommands();
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
    schema::importAs<schema::JsonS>(&event, lastPush);
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
