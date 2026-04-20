// test_node_value.cpp — Node value operations + ve::n() global accessor
#include "ve_test.h"
#include "ve/core/node.h"
#include "ve/core/convert.h"

using namespace ve;

// ============================================================================
// Node value / set / get (non-null means has value)
// ============================================================================

VE_TEST(node_value_default_no_value) {
    Node n("test");
    VE_ASSERT(n.get().isNull());
}

VE_TEST(node_value_set_int) {
    Node n("test");
    n.set(Var(42));
    VE_ASSERT_EQ(n.get().toInt(), 42);
}

VE_TEST(node_value_set_string) {
    Node n("test");
    n.set(Var("hello"));
    VE_ASSERT_EQ(n.get().toString(), "hello");
}

VE_TEST(node_value_set_double) {
    Node n("test");
    n.set(Var(3.14));
    VE_ASSERT_NEAR(n.get().toDouble(), 3.14, 0.001);
}

VE_TEST(node_value_set_overwrites) {
    Node n("test");
    n.set(Var(1));
    VE_ASSERT_EQ(n.get().toInt(), 1);
    n.set(Var(2));
    VE_ASSERT_EQ(n.get().toInt(), 2);
}

VE_TEST(node_value_set_rvalue) {
    Node n("test");
    Var v(99);
    n.set(std::move(v));
    VE_ASSERT_EQ(n.get().toInt(), 99);
}

VE_TEST(node_value_get_typed) {
    Node n("test");
    n.set(Var(42));
    VE_ASSERT_EQ(n.getInt(), 42);
    VE_ASSERT_NEAR(n.getDouble(), 42.0, 0.001);
    VE_ASSERT_EQ(n.getString(), "42");
}

VE_TEST(node_value_get_default) {
    Node n("test");
    VE_ASSERT_EQ(n.getInt(99), 99);
    VE_ASSERT_NEAR(n.getDouble(1.5), 1.5, 0.001);
    VE_ASSERT_EQ(n.getString(), "");
}

VE_TEST(node_value_getAs_template) {
    Node n("test");
    n.set(Var(42));
    VE_ASSERT_EQ(n.getAs<int>(), 42);
    VE_ASSERT_EQ(n.getAs<std::string>(), "42");
}

VE_TEST(node_value_get_child_path) {
    Node root("root");
    auto* child = root.append("x");
    child->set(Var(7));
    VE_ASSERT_EQ(root.find("x")->getInt(), 7);
    VE_ASSERT(root.find("nonexistent") == nullptr);
}

// ============================================================================
// Node::update — conditional set
// ============================================================================

VE_TEST(node_value_update_changes) {
    Node n("test");
    bool changed = n.update(Var(1));
    VE_ASSERT(changed);
    VE_ASSERT_EQ(n.get().toInt(), 1);

    changed = n.update(Var(2));
    VE_ASSERT(changed);
    VE_ASSERT_EQ(n.get().toInt(), 2);
}

VE_TEST(node_value_update_no_change) {
    Node n("test");
    n.set(Var(42));
    bool changed = n.update(Var(42));
    VE_ASSERT(!changed);
}

// ============================================================================
// NODE_DATA_CHANGED signal
// ============================================================================

VE_TEST(node_signal_data_changed_fires) {
    Node n("test");
    int fired = 0;
    Var new_val, old_val;
    n.connect(Node::NODE_CHANGED, &n, [&](const Var& nv, const Var& ov) {
        new_val = nv; old_val = ov; ++fired;
    });

    n.set(Var(42));
    VE_ASSERT_EQ(fired, 1);
    VE_ASSERT_EQ(new_val.toInt(), 42);
    VE_ASSERT(old_val.isNull());
}

VE_TEST(node_signal_data_changed_old_value) {
    Node n("test");
    n.set(Var(1));

    Var new_val, old_val;
    n.connect(Node::NODE_CHANGED, &n, [&](const Var& nv, const Var& ov) {
        new_val = nv; old_val = ov;
    });

    n.set(Var(2));
    VE_ASSERT_EQ(new_val.toInt(), 2);
    VE_ASSERT_EQ(old_val.toInt(), 1);
}

VE_TEST(node_signal_data_changed_update_no_fire) {
    Node n("test");
    n.set(Var(42));

    int fired = 0;
    n.connect(Node::NODE_CHANGED, &n, [&](const Var&, const Var&) { ++fired; });

    n.update(Var(42));
    VE_ASSERT_EQ(fired, 0);

    n.update(Var(99));
    VE_ASSERT_EQ(fired, 1);
}

VE_TEST(node_signal_data_changed_bubbles) {
    Node root("root");
    root.watch(true);
    auto* child = root.append("child");
    // ve does not inherit watch on append (unlike imol ModuleObject). activate() from set()
    // is only invoked when the node whose value changes is watching.
    child->watch(true);

    int64_t sig = -1;
    void* src = nullptr;
    root.connect(Node::NODE_ACTIVATED, &root, [&](int64_t s, void* p) {
        sig = s; src = p;
    });

    child->set(Var(10));
    VE_ASSERT_EQ(sig, Node::NODE_CHANGED);
    VE_ASSERT(src == child);
}

VE_TEST(node_signal_data_changed_silent) {
    Node n("test");
    int fired = 0;
    n.connect(Node::NODE_CHANGED, &n, [&](const Var&, const Var&) { ++fired; });

    n.silent(true);
    n.set(Var(1));
    VE_ASSERT_EQ(fired, 0);

    n.silent(false);
    n.set(Var(2));
    VE_ASSERT_EQ(fired, 1);
}

// ============================================================================
// dump() with value
// ============================================================================

VE_TEST(node_dump_shows_value) {
    Node n("item");
    n.set(Var(42));
    std::string d = n.dump();
    VE_ASSERT(d.find("42") != std::string::npos);
}

// ============================================================================
// ve::n() global accessor + ve::node::root()
// ============================================================================

VE_TEST(node_global_data_root) {
    auto* dr = ve::node::root();
    VE_ASSERT(dr != nullptr);
    VE_ASSERT(dr == ve::node::root());
}

VE_TEST(node_global_n_create_and_get) {
    ve::n("test_global/a/b")->set(Var(42));
    VE_ASSERT_EQ(ve::n("test_global/a/b")->getInt(), 42);
}

VE_TEST(node_global_n_path_hierarchy) {
    ve::n("test_hier/x/y")->set(Var(1));
    ve::n("test_hier/x/z")->set(Var(2));

    auto* x = ve::node::root()->find("test_hier/x");
    VE_ASSERT(x != nullptr);
    VE_ASSERT_EQ(x->count(), 2);
}

VE_TEST(node_global_n_slash_path) {
    ve::n("test_n/a/b")->set(Var(99));
    VE_ASSERT_EQ(ve::n("test_n/a/b")->getInt(), 99);
    VE_ASSERT(ve::node::root()->find("test_n/a/b") != nullptr);
}

VE_TEST(node_global_n_find_without_create) {
    VE_ASSERT(ve::node::root()->find("test_n_no_create/missing") == nullptr);
    VE_ASSERT(ve::n("test_n_no_create/missing", false) == nullptr);
    VE_ASSERT(ve::node::root()->find("test_n_no_create/missing") == nullptr);
}

VE_TEST(node_global_n_update) {
    auto* node = ve::n("test_upd/val");
    node->set(Var(0));
    VE_ASSERT(!node->update(Var(0)));
    VE_ASSERT(node->update(Var(1)));
    VE_ASSERT_EQ(node->getInt(), 1);
}
