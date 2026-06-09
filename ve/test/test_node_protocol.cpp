#include "ve_test.h"

#include "../src/service/node_protocol.h"
#include "../src/service/node_task_service.h"
#include "../src/service/node_session.h"

#include <ve/core/node.h>

using namespace ve;

VE_TEST(node_protocol_get_set_and_list) {
    Node root("root");
    service::Session session(&root, {});
    service::NodeTaskService tasks(&root);

    Node setReq("req");
    Node setReply("rep");
    setReq.set("op", "node.set");
    setReq.set("path", "a/value");
    setReq.at("value")->set(Var(42));
    service::dispatchNodeProtocol(&root, &setReq, &setReply, &session, &tasks);
    VE_ASSERT(setReply.get("ok").toBool(false));
    VE_ASSERT_EQ(root.find("a/value")->getInt(), 42);

    Node getReq("req");
    Node getReply("rep");
    getReq.set("op", "node.get");
    getReq.set("path", "a/value");
    getReq.set("meta", true);
    service::dispatchNodeProtocol(&root, &getReq, &getReply, &session, &tasks);
    VE_ASSERT(getReply.get("ok").toBool(false));
    Node* getData = getReply.find("data");
    VE_ASSERT(getData != nullptr);
    VE_ASSERT_EQ(getData->get("value").toInt(), 42);
    VE_ASSERT(getData->find("meta") != nullptr);

    Node listReq("req");
    Node listReply("rep");
    listReq.set("op", "node.list");
    listReq.set("path", "a");
    service::dispatchNodeProtocol(&root, &listReq, &listReply, &session, &tasks);
    VE_ASSERT(listReply.get("ok").toBool(false));
    Node* children = listReply.find("data/children");
    VE_ASSERT(children != nullptr);
    VE_ASSERT_EQ(children->count(), 1);
}

VE_TEST(node_protocol_batch_keeps_item_boundaries) {
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

    service::dispatchNodeProtocol(&root, &batchReq, &batchReply, nullptr, nullptr, 8);
    VE_ASSERT(batchReply.get("ok").toBool(false));
    Node* items = batchReply.find("data");
    VE_ASSERT(items != nullptr);
    VE_ASSERT_EQ(items->count(), 2);
    VE_ASSERT_EQ(items->child(0)->get("data/value").toInt(), 1);
    VE_ASSERT_EQ(items->child(1)->get("data/value").toInt(), 2);
}

VE_TEST(node_protocol_command_run_is_disabled_on_refactor_branch) {
    Node root("root");
    service::Session session(&root, {});
    service::NodeTaskService tasks(&root);

    Node req("req");
    Node reply("rep");
    req.set("op", "command.run");
    req.set("id", 99);
    req.set("name", "_test_proto_disabled");
    req.set("wait", false);

    service::dispatchNodeProtocol(&root, &req, &reply, &session, &tasks, 500);
    VE_ASSERT(!reply.get("ok").toBool(true));
    VE_ASSERT_EQ(reply.get("code").toString(), std::string("unsupported"));
    VE_ASSERT_EQ(reply.get("error").toString(),
                 std::string("command.run is disabled on the command refactor branch; use HTTP /cmd"));
}
