// test_node_signal.cpp — Node signal tests (NODE_CHILD_ADDED, NODE_CHILD_REMOVED, NODE_ACTIVATED, bubbling)
#include "ve_test.h"
#include "ve/core/node.h"

using namespace ve;

// ============================================================================
// NODE_CHILD_ADDED — single insert
// ============================================================================

VE_TEST(node_signal_insert_fires_added) {
    Node root("root");
    int fired = 0;
    std::string key;
    int overlap = -1;
    root.connect(Node::NODE_ADDED, &root, [&](const std::string& k, int ov) {
        key = k; overlap = ov; ++fired;
    });
    root.append("child");
    VE_ASSERT_EQ(fired, 1);
    VE_ASSERT_EQ(key, "child");
    VE_ASSERT_EQ(overlap, 0);  // single insert → overlap 0
}

VE_TEST(node_signal_insert_anon_fires_added) {
    Node root("root");
    std::string key;
    root.connect(Node::NODE_ADDED, &root, [&](const std::string& k, int) { key = k; });
    root.append("");
    VE_ASSERT_EQ(key, "#0");  // first anon child → global index 0
}

VE_TEST(node_signal_insert_at_fires_key) {
    Node root("root");
    root.append("a");
    root.append("b");

    std::string key;
    root.connect(Node::NODE_ADDED, &root, [&](const std::string& k, int) { key = k; });
    root.insert(new Node("mid"), 1);  // insert at index 1

    VE_ASSERT_EQ(key, "mid");
}

VE_TEST(node_signal_insert_overlap_key) {
    Node root("root");
    root.append("item");

    std::string key;
    root.connect(Node::NODE_ADDED, &root, [&](const std::string& k, int) { key = k; });
    root.append("item");  // second "item" → "item#1"

    VE_ASSERT_EQ(key, "item#1");
}

// ============================================================================
// NODE_CHILD_ADDED — batch insert
// ============================================================================

VE_TEST(node_signal_batch_insert_fires_once) {
    Node root("root");
    int fired = 0;
    std::string key;
    int overlap = -1;
    root.connect(Node::NODE_ADDED, &root, [&](const std::string& k, int ov) {
        key = k; overlap = ov; ++fired;
    });

    Node::Nodes batch;
    for (int i = 0; i < 5; ++i) batch.push_back(new Node("n" + std::to_string(i)));
    root.insert(batch);

    VE_ASSERT_EQ(fired, 1);       // fires once for the whole batch
    VE_ASSERT_EQ(key, "n0");      // key of first inserted child
    VE_ASSERT_EQ(overlap, 4);     // 5 children → overlap = 4
}

VE_TEST(node_signal_batch_insert_anon) {
    Node root("root");
    std::string key;
    int overlap = -1;
    root.connect(Node::NODE_ADDED, &root, [&](const std::string& k, int ov) { key = k; overlap = ov; });

    Node::Nodes batch;
    for (int i = 0; i < 3; ++i) batch.push_back(new Node(""));
    root.insert(batch);

    VE_ASSERT_EQ(key, "#0");
    VE_ASSERT_EQ(overlap, 2);  // 3 children → overlap = 2
}

// ============================================================================
// NODE_CHILD_REMOVED — take / remove
// ============================================================================

VE_TEST(node_signal_take_fires_removed) {
    Node root("root");
    auto* c = root.append("child");
    int fired = 0;
    std::string key;
    int overlap = -1;
    root.connect(Node::NODE_REMOVED, &root, [&](const std::string& k, int ov) {
        key = k; overlap = ov; ++fired;
    });
    auto* taken = root.take(c);
    VE_ASSERT_EQ(fired, 1);
    VE_ASSERT_EQ(key, "child");
    VE_ASSERT_EQ(overlap, 0);  // single remove → overlap 0
    VE_ASSERT(taken == c);
    delete taken;
}

VE_TEST(node_signal_remove_overlap_key) {
    Node root("root");
    root.append("item");
    auto* second = root.append("item");  // item#1
    root.append("item");                 // item#2

    std::string key;
    root.connect(Node::NODE_REMOVED, &root, [&](const std::string& k, int) { key = k; });
    root.take(second);

    VE_ASSERT_EQ(key, "item#1");
    delete second;
}

VE_TEST(node_signal_remove_anon) {
    Node root("root");
    root.append("");
    auto* c = root.append("");

    std::string key;
    root.connect(Node::NODE_REMOVED, &root, [&](const std::string& k, int) { key = k; });
    root.take(c);

    VE_ASSERT_EQ(key, "#1");
    delete c;
}

// ============================================================================
// NODE_CHILD_REMOVED — clear
// ============================================================================

VE_TEST(node_signal_clear_fires_removed) {
    Node root("root");
    for (int i = 0; i < 10; ++i) root.append("n" + std::to_string(i));

    int fired = 0;
    std::string key;
    int overlap = -1;
    root.connect(Node::NODE_REMOVED, &root, [&](const std::string& k, int ov) {
        key = k; overlap = ov; ++fired;
    });
    root.clear();

    VE_ASSERT_EQ(fired, 1);
    VE_ASSERT_EQ(key, "#0");      // clear reports from index 0
    VE_ASSERT_EQ(overlap, 9);     // 10 children → overlap = 9
}

VE_TEST(node_signal_clear_empty_no_fire) {
    Node root("root");
    int fired = 0;
    root.connect(Node::NODE_REMOVED, &root, [&](const std::string&, int) { ++fired; });
    root.clear();
    VE_ASSERT_EQ(fired, 0);  // no children → no signal
}

// ============================================================================
// SILENT flag — suppress signals
// ============================================================================

VE_TEST(node_signal_silent_suppresses_added) {
    Node root("root");
    int fired = 0;
    root.connect(Node::NODE_ADDED, &root, [&](const std::string&, int) { ++fired; });
    root.silent(true);
    root.append("x");
    VE_ASSERT_EQ(fired, 0);  // signal suppressed

    root.silent(false);
    root.append("y");
    VE_ASSERT_EQ(fired, 1);  // signal fires again
}

VE_TEST(node_signal_silent_suppresses_removed) {
    Node root("root");
    auto* c = root.append("x");
    int fired = 0;
    root.connect(Node::NODE_REMOVED, &root, [&](const std::string&, int) { ++fired; });
    root.silent(true);
    root.remove(c);
    VE_ASSERT_EQ(fired, 0);

    auto* c2 = root.append("y");
    root.silent(false);
    root.remove(c2);
    VE_ASSERT_EQ(fired, 1);
}

// ============================================================================
// NODE_ACTIVATED — signal bubbling
// ============================================================================

VE_TEST(node_signal_activate_fires_locally) {
    Node root("root");
    int64_t sig = -1;
    void* src_ptr = nullptr;
    root.connect(Node::NODE_ACTIVATED, &root, [&](int64_t s, void* p) {
        sig = s; src_ptr = p;
    });
    root.activate(42, &root);

    VE_ASSERT_EQ(sig, 42);
    VE_ASSERT(src_ptr == &root);
}

VE_TEST(node_signal_activate_bubbles_up) {
    Node root("root");
    root.watch(true);
    auto* child = root.append("child");

    int64_t root_sig = -1;
    void* root_src = nullptr;
    root.connect(Node::NODE_ACTIVATED, &root, [&](int64_t s, void* p) {
        root_sig = s; root_src = p;
    });

    child->activate(99, child);

    VE_ASSERT_EQ(root_sig, 99);       // bubbled to root
    VE_ASSERT(root_src == child);       // source is child
}

VE_TEST(node_signal_activate_not_watching_stops_bubble) {
    Node root("root");
    // NOT watching → bubble should stop
    auto* child = root.append("child");

    int fired = 0;
    root.connect(Node::NODE_ACTIVATED, &root, [&](int, void*) { ++fired; });
    child->activate(1, child);

    VE_ASSERT_EQ(fired, 0);  // root not watching → no bubble received
}

VE_TEST(node_signal_activate_silent_stops_bubble) {
    Node root("root");
    root.watch(true);
    auto* child = root.append("child");
    child->silent(true);

    int fired = 0;
    root.connect(Node::NODE_ACTIVATED, &root, [&](int, void*) { ++fired; });
    child->activate(1, child);

    VE_ASSERT_EQ(fired, 0);  // child is silent → stops emission + bubbling
}

VE_TEST(node_signal_activate_deep_chain) {
    Node root("root");
    root.watch(true);
    auto* a = root.append("a");
    a->watch(true);
    auto* b = a->append("b");
    b->watch(true);
    auto* c = b->append("c");

    int root_count = 0;
    root.connect(Node::NODE_ACTIVATED, &root, [&](int, void*) { ++root_count; });
    int a_count = 0;
    a->connect(Node::NODE_ACTIVATED, a, [&](int, void*) { ++a_count; });
    int b_count = 0;
    b->connect(Node::NODE_ACTIVATED, b, [&](int, void*) { ++b_count; });

    c->activate(7, c);  // c → b → a → root

    VE_ASSERT_EQ(b_count, 1);
    VE_ASSERT_EQ(a_count, 1);
    VE_ASSERT_EQ(root_count, 1);
}

// ============================================================================
// NODE_ACTIVATED — triggered by insert/remove
// ============================================================================

VE_TEST(node_signal_insert_triggers_activate) {
    Node root("root");
    root.watch(true);

    int activated = 0;
    root.connect(Node::NODE_ACTIVATED, &root, [&](int64_t sig, void*) {
        if (sig == Node::NODE_ADDED) ++activated;
    });

    root.append("test");
    VE_ASSERT_EQ(activated, 1);
}

VE_TEST(node_signal_remove_triggers_activate) {
    Node root("root");
    root.watch(true);
    auto* c = root.append("test");

    int activated = 0;
    root.connect(Node::NODE_ACTIVATED, &root, [&](int64_t sig, void*) {
        if (sig == Node::NODE_REMOVED) ++activated;
    });

    root.remove(c);
    VE_ASSERT_EQ(activated, 1);
}

// ============================================================================
// Multiple signals — connect count tracking
// ============================================================================

VE_TEST(node_signal_copy_triggers_added_and_removed) {
    Node src("src");
    src.append("head");
    src.append("tail");

    Node dst("dst");
    dst.append("tail");
    dst.append("extra");

    int added = 0;
    int removed = 0;
    std::string added_key;
    std::string removed_key;
    int added_overlap = -1;
    int removed_overlap = -1;

    dst.connect(Node::NODE_ADDED, &dst, [&](const std::string& key, int overlap) {
        ++added;
        added_key = key;
        added_overlap = overlap;
    });
    dst.connect(Node::NODE_REMOVED, &dst, [&](const std::string& key, int overlap) {
        ++removed;
        removed_key = key;
        removed_overlap = overlap;
    });

    dst.copy(&src, true, true);

    VE_ASSERT_EQ(added, 1);
    VE_ASSERT_EQ(added_key, "head");
    VE_ASSERT_EQ(added_overlap, 0);
    VE_ASSERT_EQ(removed, 1);
    VE_ASSERT_EQ(removed_key, "extra");
    VE_ASSERT_EQ(removed_overlap, 0);
}

VE_TEST(node_signal_multiple_inserts_fires) {
    Node root("root");
    int fires = 0;
    int last_overlap = -1;
    root.connect(Node::NODE_ADDED, &root, [&](const std::string&, int ov) {
        last_overlap = ov; ++fires;
    });

    root.append("a");
    VE_ASSERT_EQ(last_overlap, 0);  // single → overlap 0
    root.append("b");
    root.append("c");

    VE_ASSERT_EQ(fires, 3);         // fires once per insert
    VE_ASSERT_EQ(last_overlap, 0);  // each single insert → overlap 0
}

VE_TEST(node_signal_disconnect_stops_delivery) {
    Node root("root");
    Object obs("obs");
    int fired = 0;
    root.connect(Node::NODE_ADDED, &obs, [&](const std::string&, int) { ++fired; });

    root.append("a");
    VE_ASSERT_EQ(fired, 1);

    root.disconnect(Node::NODE_ADDED, &obs);
    root.append("b");
    VE_ASSERT_EQ(fired, 1);  // disconnected, no more signals
}
