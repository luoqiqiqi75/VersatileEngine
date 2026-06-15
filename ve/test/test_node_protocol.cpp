#include "ve_test.h"

#include "../src/service/node_commands.h"

#include <ve/core/node.h>
#include <ve/core/command.h>
#include <ve/core/schema.h>
#include <ve/core/pipeline.h>

using namespace ve;

static bool runEnvelope(service::Session* session, Pipeline& pipe)
{
    std::string cmd_str = pipe.contextNode()->get("cmd").toString();
    auto ref = service::resolveCmd(cmd_str);
    if (!ref.factory) {
        pipe.contextNode()->set("code", int64_t(service::ERR_NOT_FOUND));
        pipe.contextNode()->set("message", "unknown: " + cmd_str);
        return true;
    }
    pipe.contextNode()->set("_session", Var::ptr(session));
    Command* c = pipe.add(command::create(*ref.factory, ref.key));
    c->setContextNodes(pipe.contextNode(), pipe.contextNode()->at("params"), pipe.contextNode()->at("data"));
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
        pipe.contextNode()->set("cmd", "node.set");
        pipe.contextNode()->at("params")->set("path", "a/value");
        pipe.contextNode()->at("params")->at("value")->set(Var(42));
        runEnvelope(&session, pipe);
        VE_ASSERT_EQ(pipe.contextNode()->get("code").toInt(-1), 0);
        VE_ASSERT_EQ(root.find("a/value")->getInt(), 42);
    }

    // get
    {
        Pipeline pipe;
        pipe.contextNode()->set("cmd", "node.get");
        pipe.contextNode()->at("params")->set("path", "a/value");
        runEnvelope(&session, pipe);
        VE_ASSERT_EQ(pipe.contextNode()->get("code").toInt(-1), 0);
        VE_ASSERT_EQ(pipe.contextNode()->get("data/value").toInt(), 42);
    }

    // children
    {
        Pipeline pipe;
        pipe.contextNode()->set("cmd", "node.children");
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
    pipe.contextNode()->set("cmd", "batch");
    Node* params = pipe.contextNode()->at("params");

    Node* item1 = params->append();
    item1->set("cmd", "node.get");
    item1->at("params")->set("path", "one");

    Node* item2 = params->append();
    item2->set("cmd", "node.get");
    item2->at("params")->set("path", "two");

    runEnvelope(&session, pipe);
    VE_ASSERT_EQ(pipe.contextNode()->get("code").toInt(-1), 0);
    Node* data = pipe.contextNode()->find("data");
    VE_ASSERT(data != nullptr);
    VE_ASSERT_EQ(data->count(), 2);
    VE_ASSERT_EQ(data->child(0)->get("value").toInt(), 1);
    VE_ASSERT_EQ(data->child(1)->get("value").toInt(), 2);
}

VE_TEST(node_dispatch_watch_unsupported_without_send) {
    service::registerNodeCommands();
    Node root("root");
    service::Session session(&root, &root);

    Pipeline pipe;
    pipe.contextNode()->set("cmd", "node.watch");
    pipe.contextNode()->at("params")->set("path", "a");
    runEnvelope(&session, pipe);
    VE_ASSERT(pipe.contextNode()->get("code").toInt(0) < 0);
}

VE_TEST(node_dispatch_watch_with_session_pushes) {
    service::registerNodeCommands();
    Node root("root");
    root.set("watch/me", 1);

    std::string lastPush;
    service::Session session(&root, &root, [&](std::string msg) {
        lastPush = std::move(msg);
    });

    // watch
    {
        Pipeline pipe;
        pipe.contextNode()->set("cmd", "node.watch");
        pipe.contextNode()->at("params")->set("path", "watch/me");
        runEnvelope(&session, pipe);
        VE_ASSERT_EQ(pipe.contextNode()->get("code").toInt(-1), 0);
    }

    // trigger change
    root.find("watch/me")->set(7);
    VE_ASSERT(!lastPush.empty());

    // verify event JSON contains the value
    Node event;
    schema::importAs<schema::JsonS>(&event, lastPush);
    VE_ASSERT_EQ(event.get("event").toString(), std::string("node.changed"));
    VE_ASSERT_EQ(event.get("value").toInt(), 7);

    // unwatch
    lastPush.clear();
    {
        Pipeline pipe;
        pipe.contextNode()->set("cmd", "node.unwatch");
        pipe.contextNode()->at("params")->set("path", "watch/me");
        runEnvelope(&session, pipe);
        VE_ASSERT_EQ(pipe.contextNode()->get("code").toInt(-1), 0);
    }

    root.find("watch/me")->set(9);
    VE_ASSERT(lastPush.empty());
}
