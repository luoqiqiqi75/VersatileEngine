#include "ve_test.h"

#include "../src/service/node_commands.h"
#include "../src/service/node_session.h"

#include <ve/core/node.h>

using namespace ve;

VE_TEST(node_dispatch_get_set_and_list) {
    service::registerNodeCommands();
    Node root("root");

    Node setReq("req");
    Node setReply("rep");
    setReq.set("op", "node.set");
    setReq.set("path", "a/value");
    setReq.at("value")->set(Var(42));
    service::dispatchNode(&root, &setReq, &setReply);
    VE_ASSERT(setReply.get("ok").toBool(false));
    VE_ASSERT_EQ(root.find("a/value")->getInt(), 42);

    Node getReq("req");
    Node getReply("rep");
    getReq.set("op", "node.get");
    getReq.set("path", "a/value");
    getReq.set("meta", true);
    service::dispatchNode(&root, &getReq, &getReply);
    VE_ASSERT(getReply.get("ok").toBool(false));
    Node* getData = getReply.find("data");
    VE_ASSERT(getData != nullptr);
    VE_ASSERT_EQ(getData->get("value").toInt(), 42);
    VE_ASSERT(getData->find("meta") != nullptr);

    Node listReq("req");
    Node listReply("rep");
    listReq.set("op", "node.list");
    listReq.set("path", "a");
    service::dispatchNode(&root, &listReq, &listReply);
    VE_ASSERT(listReply.get("ok").toBool(false));
    Node* children = listReply.find("data/children");
    VE_ASSERT(children != nullptr);
    VE_ASSERT_EQ(children->count(), 1);
}

VE_TEST(node_dispatch_batch_keeps_item_boundaries) {
    service::registerNodeCommands();
    Node root("root");
    root.set("one", 1);
    root.set("two", 2);

    Node batchReq("req");
    Node batchReply("rep");
    batchReq.set("op", "batch");

    Node* item1 = batchReq.at("items")->append("");
    item1->set("op", "node.get");
    item1->set("path", "one");

    Node* item2 = batchReq.at("items")->append("");
    item2->set("op", "node.get");
    item2->set("path", "two");

    service::dispatchNode(&root, &batchReq, &batchReply, nullptr, 8);
    VE_ASSERT(batchReply.get("ok").toBool(false));
    Node* items = batchReply.find("data");
    VE_ASSERT(items != nullptr);
    VE_ASSERT_EQ(items->count(), 2);
    VE_ASSERT_EQ(items->child(0)->get("data/value").toInt(), 1);
    VE_ASSERT_EQ(items->child(1)->get("data/value").toInt(), 2);
}

VE_TEST(node_dispatch_subscribe_unsupported_without_session) {
    service::registerNodeCommands();
    Node root("root");

    // No Session passed -> subscribe is unsupported on this (sessionless) call.
    Node req("req");
    Node reply("rep");
    req.set("op", "subscribe");
    req.set("path", "a");
    service::dispatchNode(&root, &req, &reply);
    VE_ASSERT(!reply.get("ok").toBool(true));
    VE_ASSERT_EQ(reply.get("code").toString(), std::string("unsupported"));
}

VE_TEST(node_dispatch_subscribe_with_session_pushes) {
    service::registerNodeCommands();
    Node root("root");
    root.set("watch/me", 1);

    int hits = 0;
    Var lastValue;
    service::Session session(&root, [&](const std::string&, const Var& value) {
        ++hits;
        lastValue = value;
    });

    Node subReq("req");
    Node subReply("rep");
    subReq.set("op", "subscribe");
    subReq.set("path", "watch/me");
    service::dispatchNode(&root, &subReq, &subReply, &session);
    VE_ASSERT(subReply.get("ok").toBool(false));

    root.find("watch/me")->set(7);
    VE_ASSERT_EQ(hits, 1);
    VE_ASSERT_EQ(lastValue.toInt(), 7);

    Node unsubReq("req");
    Node unsubReply("rep");
    unsubReq.set("op", "unsubscribe");
    unsubReq.set("path", "watch/me");
    service::dispatchNode(&root, &unsubReq, &unsubReply, &session);
    VE_ASSERT(unsubReply.get("ok").toBool(false));

    root.find("watch/me")->set(9);
    VE_ASSERT_EQ(hits, 1);   // no more pushes after unsubscribe
}
