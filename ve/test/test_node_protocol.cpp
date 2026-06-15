#include "ve_test.h"

#include "../src/service/node_commands.h"
#include "../src/service/node_session.h"

#include <ve/core/node.h>
#include <ve/core/schema.h>

using namespace ve;

VE_TEST(node_dispatch_get_set_and_children) {
    service::registerNodeCommands();
    Node root("root");
    service::Session session(&root);

    // set
    Node setCtx;
    setCtx.set("cmd", "node.set");
    setCtx.at("params")->set("path", "a/value");
    setCtx.at("params")->at("value")->set(Var(42));
    service::dispatch(&session, &setCtx);
    VE_ASSERT_EQ(setCtx.get("code").toInt(-1), 0);
    VE_ASSERT_EQ(root.find("a/value")->getInt(), 42);

    // get
    Node getCtx;
    getCtx.set("cmd", "node.get");
    getCtx.at("params")->set("path", "a/value");
    service::dispatch(&session, &getCtx);
    VE_ASSERT_EQ(getCtx.get("code").toInt(-1), 0);
    VE_ASSERT_EQ(getCtx.get("data/value").toInt(), 42);

    // children
    Node childCtx;
    childCtx.set("cmd", "node.children");
    childCtx.at("params")->set("path", "a");
    service::dispatch(&session, &childCtx);
    VE_ASSERT_EQ(childCtx.get("code").toInt(-1), 0);
    Node* children = childCtx.find("data/children");
    VE_ASSERT(children != nullptr);
    VE_ASSERT_EQ(children->count(), 1);
}

VE_TEST(node_dispatch_batch) {
    service::registerNodeCommands();
    Node root("root");
    root.set("one", 1);
    root.set("two", 2);
    service::Session session(&root);

    Node ctx;
    ctx.set("cmd", "batch");
    Node* params = ctx.at("params");

    Node* item1 = params->append();
    item1->set("cmd", "node.get");
    item1->at("params")->set("path", "one");

    Node* item2 = params->append();
    item2->set("cmd", "node.get");
    item2->at("params")->set("path", "two");

    service::dispatch(&session, &ctx);
    VE_ASSERT_EQ(ctx.get("code").toInt(-1), 0);
    Node* data = ctx.find("data");
    VE_ASSERT(data != nullptr);
    VE_ASSERT_EQ(data->count(), 2);
    VE_ASSERT_EQ(data->child(0)->get("data/value").toInt(), 1);
    VE_ASSERT_EQ(data->child(1)->get("data/value").toInt(), 2);
}

VE_TEST(node_dispatch_watch_unsupported_without_send) {
    service::registerNodeCommands();
    Node root("root");
    service::Session session(&root);  // no send fn → watch unsupported

    Node ctx;
    ctx.set("cmd", "node.watch");
    ctx.at("params")->set("path", "a");
    service::dispatch(&session, &ctx);
    VE_ASSERT(ctx.get("code").toInt(0) < 0);
}

VE_TEST(node_dispatch_watch_with_session_pushes) {
    service::registerNodeCommands();
    Node root("root");
    root.set("watch/me", 1);

    std::string lastPush;
    service::Session session(&root, [&](std::string msg) {
        lastPush = std::move(msg);
    });

    // watch
    Node watchCtx;
    watchCtx.set("cmd", "node.watch");
    watchCtx.at("params")->set("path", "watch/me");
    service::dispatch(&session, &watchCtx);
    VE_ASSERT_EQ(watchCtx.get("code").toInt(-1), 0);

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
    Node unwatchCtx;
    unwatchCtx.set("cmd", "node.unwatch");
    unwatchCtx.at("params")->set("path", "watch/me");
    service::dispatch(&session, &unwatchCtx);
    VE_ASSERT_EQ(unwatchCtx.get("code").toInt(-1), 0);

    root.find("watch/me")->set(9);
    VE_ASSERT(lastPush.empty());   // no more pushes after unwatch
}
