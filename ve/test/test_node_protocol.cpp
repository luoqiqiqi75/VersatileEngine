#include "ve_test.h"

#include "../src/service/node_protocol.h"
#include "../src/service/node_task_service.h"
#include "../src/service/subscribe_service.h"

#include <ve/core/command.h>
#include <ve/core/loop.h>
#include <ve/core/node.h>

#include <atomic>
#include <chrono>
#include <thread>

using namespace ve;

static bool wait_until(const std::function<bool()>& fn, int timeoutMs = 1000)
{
    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + std::chrono::milliseconds(timeoutMs);
    while (clock::now() < deadline) {
        if (fn()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return fn();
}

VE_TEST(node_protocol_get_set_and_list) {
    Node root("root");
    service::SubscribeService subscribe(&root);
    service::NodeTaskService tasks(&root);

    Node setReq("req");
    Node setReply("rep");
    setReq.set("op", "node.set");
    setReq.set("path", "a/value");
    setReq.at("value")->set(Var(42));
    service::dispatchNodeProtocol(&root, &setReq, &setReply, &subscribe, &tasks);
    VE_ASSERT(setReply.get("ok").toBool(false));
    VE_ASSERT_EQ(root.find("a/value")->getInt(), 42);

    Node getReq("req");
    Node getReply("rep");
    getReq.set("op", "node.get");
    getReq.set("path", "a/value");
    getReq.set("meta", true);
    service::dispatchNodeProtocol(&root, &getReq, &getReply, &subscribe, &tasks);
    VE_ASSERT(getReply.get("ok").toBool(false));
    Node* getData = getReply.find("data");
    VE_ASSERT(getData != nullptr);
    VE_ASSERT_EQ(getData->get("value").toInt(), 42);
    VE_ASSERT(getData->find("meta") != nullptr);

    Node listReq("req");
    Node listReply("rep");
    listReq.set("op", "node.list");
    listReq.set("path", "a");
    service::dispatchNodeProtocol(&root, &listReq, &listReply, &subscribe, &tasks);
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

VE_TEST(subscribe_service_counts_are_shared) {
    Node root("root");
    service::SubscribeService s1(&root);
    service::SubscribeService s2(&root);

    s1.subscribe(1, "watch/me");
    s2.subscribe(2, "/watch/me");
    VE_ASSERT_EQ(static_cast<int>(s1.getSubscriberCount("watch/me")), 2);
    VE_ASSERT_EQ(static_cast<int>(s2.getSubscriberCount("/watch/me")), 2);

    s1.removeSession(1);
    VE_ASSERT_EQ(static_cast<int>(s2.getSubscriberCount("watch/me")), 1);

    s2.unsubscribe(2, "watch/me");
    VE_ASSERT_EQ(static_cast<int>(s1.getSubscriberCount("watch/me")), 0);
}

VE_TEST(node_protocol_async_command_creates_task_and_event) {
    EventLoop loop("proto_async");
    loop.start();

    command::reg("_test_proto_async",
        [](Node*) -> Result {
            return Result::ok(Var("async-done"));
        },
        LoopRef::from(loop),
        "async protocol test");

    Node root("root");
    service::SubscribeService subscribe(&root);
    service::NodeTaskService tasks(&root);

    Node taskEvent("event");
    std::atomic<bool> gotEvent{false};

    Node req("req");
    Node reply("rep");
    req.set("op", "command.run");
    req.set("id", 99);
    req.set("name", "_test_proto_async");
    req.set("wait", false);

    service::dispatchNodeProtocol(&root, &req, &reply, &subscribe, &tasks, 500, false, 0, true,
        [&](const Node& event) {
        taskEvent.copy(&event, true, true, true);
        gotEvent.store(true, std::memory_order_release);
    });
    VE_ASSERT(reply.get("ok").toBool(false));
    VE_ASSERT(reply.get("accepted").toBool(false));
    VE_ASSERT(!reply.get("task_id").toString().empty());

    VE_ASSERT(wait_until([&]() { return gotEvent.load(std::memory_order_acquire); }, 1500));
    VE_ASSERT_EQ(taskEvent.get("event").toString(), "task.result");
    VE_ASSERT(taskEvent.get("ok").toBool(false));
    VE_ASSERT_EQ(taskEvent.get("data").toString(), "async-done");

    Node* taskNode = root.find("ve/server/tasks/" + reply.get("task_id").toString());
    VE_ASSERT(taskNode != nullptr);
    VE_ASSERT_EQ(taskNode->get("status").toString(), "done");
    VE_ASSERT_EQ(taskNode->get("result").toString(), "async-done");

    loop.stop();
    GlobalCommandFactory().root()->erase("_test_proto_async");
}
