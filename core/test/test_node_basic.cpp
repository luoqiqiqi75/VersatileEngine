// test_node_basic.cpp — creation, child query, count, iteration
#include "ve_test.h"
#include "ve/core/node.h"

using namespace ve;

// ============================================================================
// Creation
// ============================================================================

VE_TEST(node_create_default) {
    Node n;
    VE_ASSERT_EQ(n.name(), "");
    VE_ASSERT(n.parent() == nullptr);
    VE_ASSERT_EQ(n.count(), 0);
}

VE_TEST(node_create_named) {
    Node n("root");
    VE_ASSERT_EQ(n.name(), "root");
    VE_ASSERT_EQ(n.count(), 0);
}

// ============================================================================
// Named children
// ============================================================================

VE_TEST(node_append_named) {
    Node root("root");
    Node* a = root.append("a");
    Node* b = root.append("b");

    VE_ASSERT(a != nullptr);
    VE_ASSERT(b != nullptr);
    VE_ASSERT_EQ(root.count(), 2);
    VE_ASSERT_EQ(a->name(), "a");
    VE_ASSERT_EQ(b->name(), "b");
    VE_ASSERT(a->parent() == &root);
    VE_ASSERT(b->parent() == &root);
}

VE_TEST(node_child_by_name) {
    Node root("root");
    root.append("x");
    root.append("y");

    VE_ASSERT(root.child("x") != nullptr);
    VE_ASSERT(root.child("y") != nullptr);
    VE_ASSERT(root.child("z") == nullptr);
    VE_ASSERT_EQ(root.child("x")->name(), "x");
}

VE_TEST(node_insert_own_name) {
    Node root("root");
    Node* c = new Node("myname");
    root.insert(c);
    VE_ASSERT_EQ(root.child("myname"), c);
    VE_ASSERT_EQ(root.count(), 1);
}

VE_TEST(node_has) {
    Node root("root");
    root.append("foo");
    VE_ASSERT(root.has("foo"));
    VE_ASSERT(!root.has("bar"));
}

VE_TEST(node_has_ptr) {
    Node root("root");
    Node* a = root.append("a");
    Node other("other");
    VE_ASSERT(root.has(a));
    VE_ASSERT(!root.has(&other));
    VE_ASSERT(!root.has(nullptr));
}

VE_TEST(node_has_global_index) {
    Node root("root");
    root.append("a");
    root.append("b");
    VE_ASSERT(root.has(0));
    VE_ASSERT(root.has(1));
    VE_ASSERT(!root.has(2));
    VE_ASSERT(!root.has(-1));
}

// ============================================================================
// Duplicate-named children (XML pattern)
// ============================================================================

VE_TEST(node_duplicate_names) {
    Node root("root");
    Node* i0 = root.append("item");
    Node* i1 = root.append("item");
    Node* i2 = root.append("item");

    VE_ASSERT_EQ(root.count(), 3);
    VE_ASSERT_EQ(root.count("item"), 3);
    VE_ASSERT_EQ(root.child("item", 0), i0);
    VE_ASSERT_EQ(root.child("item", 1), i1);
    VE_ASSERT_EQ(root.child("item", 2), i2);
    VE_ASSERT(root.child("item", 3) == nullptr);
    VE_ASSERT_EQ(root.child("item"), i0);  // default index=0
}

VE_TEST(node_children_by_name) {
    Node root("root");
    root.append("item");
    root.append("other");
    root.append("item");

    auto items = root.children("item");
    VE_ASSERT_EQ(items.sizeAsInt(), 2);
    auto others = root.children("other");
    VE_ASSERT_EQ(others.sizeAsInt(), 1);
}

// ============================================================================
// Anonymous children (list pattern)
// ============================================================================

VE_TEST(node_anonymous_children) {
    Node root("root");
    Node* c0 = root.append("");
    Node* c1 = root.append("");
    Node* c2 = root.append("");

    VE_ASSERT_EQ(root.count(), 3);
    VE_ASSERT_EQ(root.count(""), 3);
    VE_ASSERT_EQ(c0->name(), "");
    VE_ASSERT_EQ(root.child("", 0), c0);
    VE_ASSERT_EQ(root.child("", 1), c1);
    VE_ASSERT_EQ(root.child("", 2), c2);
}

// ============================================================================
// Global index: child(int)
// ============================================================================

VE_TEST(node_child_global_pure_list) {
    Node root("root");
    for (int i = 0; i < 100; ++i) root.append("");

    VE_ASSERT_EQ(root.count(), 100);
    VE_ASSERT(root.child(0) != nullptr);
    VE_ASSERT(root.child(50) != nullptr);
    VE_ASSERT(root.child(99) != nullptr);
    VE_ASSERT(root.child(100) == nullptr);
    VE_ASSERT(root.child(-1) == nullptr);
}

VE_TEST(node_child_global_named) {
    Node root("root");
    Node* a = root.append("a");
    Node* b = root.append("b");
    Node* c = root.append("c");

    // Dict order: "" (empty), "a", "b", "c"
    VE_ASSERT_EQ(root.child(0), a);
    VE_ASSERT_EQ(root.child(1), b);
    VE_ASSERT_EQ(root.child(2), c);
}

VE_TEST(node_child_global_mixed) {
    Node root("root");
    Node* a0 = root.append("");
    Node* a1 = root.append("");
    Node* x  = root.append("x");
    Node* a2 = root.append("");   // goes to "" group (already first)
    Node* y  = root.append("y");

    VE_ASSERT_EQ(root.count(), 5);

    // Dict order: "" group [a0,a1,a2], "x" group [x], "y" group [y]
    VE_ASSERT_EQ(root.child(0), a0);
    VE_ASSERT_EQ(root.child(1), a1);
    VE_ASSERT_EQ(root.child(2), a2);
    VE_ASSERT_EQ(root.child(3), x);
    VE_ASSERT_EQ(root.child(4), y);
}

// ============================================================================
// Iteration
// ============================================================================

VE_TEST(node_children_order) {
    Node root("root");
    root.append("b");
    root.append("a");
    root.append("c");

    auto kids = root.children();
    VE_ASSERT_EQ(kids.sizeAsInt(), 3);
    VE_ASSERT_EQ(kids[0]->name(), "b");
    VE_ASSERT_EQ(kids[1]->name(), "a");
    VE_ASSERT_EQ(kids[2]->name(), "c");
}

VE_TEST(node_child_names) {
    Node root("root");
    root.append("x");
    root.append("y");
    root.append("x");  // dup, same group

    auto names = root.childNames();
    VE_ASSERT_EQ(names.sizeAsInt(), 2);
    VE_ASSERT_EQ(names[0], "x");
    VE_ASSERT_EQ(names[1], "y");
}

VE_TEST(node_count) {
    Node root("root");
    VE_ASSERT_EQ(root.count(), 0);
    VE_ASSERT_EQ(root.count("x"), 0);

    root.append("x");
    root.append("x");
    root.append("y");
    VE_ASSERT_EQ(root.count(), 3);
    VE_ASSERT_EQ(root.count("x"), 2);
    VE_ASSERT_EQ(root.count("y"), 1);
    VE_ASSERT_EQ(root.count("z"), 0);
}

// ============================================================================
// Container interface: operator[], iterator
// ============================================================================

VE_TEST(node_operator_index_int) {
    Node root("root");
    auto* a = root.append("a");
    auto* b = root.append("b");
    VE_ASSERT_EQ(root[0], a);
    VE_ASSERT_EQ(root[1], b);
    VE_ASSERT(root[2] == nullptr);
    VE_ASSERT(root[-1] == nullptr);
}

VE_TEST(node_operator_index_name) {
    Node root("root");
    auto* x = root.append("x");
    auto* y = root.append("y");
    VE_ASSERT_EQ(root["x"], x);
    VE_ASSERT_EQ(root["y"], y);
    VE_ASSERT(root["z"] == nullptr);
}

VE_TEST(node_range_for_empty) {
    Node root("root");
    int cnt = 0;
    for (auto* c : root) { (void)c; ++cnt; }
    VE_ASSERT_EQ(cnt, 0);
}

VE_TEST(node_range_for_named) {
    Node root("root");
    root.append("b");
    root.append("a");
    root.append("c");

    Vector<std::string> names;
    for (auto* c : root) names.push_back(c->name());

    VE_ASSERT_EQ(names.sizeAsInt(), 3);
    VE_ASSERT_EQ(names[0], "b");
    VE_ASSERT_EQ(names[1], "a");
    VE_ASSERT_EQ(names[2], "c");
}

VE_TEST(node_range_for_anon) {
    Node root("root");
    for (int i = 0; i < 5; ++i) root.append("");

    int cnt = 0;
    for (auto* c : root) { VE_ASSERT(c != nullptr); ++cnt; }
    VE_ASSERT_EQ(cnt, 5);
}

VE_TEST(node_range_for_mixed) {
    Node root("root");
    auto* a0 = root.append("");
    auto* a1 = root.append("");
    auto* x  = root.append("x");
    auto* a2 = root.append("");
    auto* y  = root.append("y");

    Vector<Node*> got;
    for (auto* c : root) got.push_back(c);

    // order: "" group [a0,a1,a2], "x" [x], "y" [y]
    VE_ASSERT_EQ(got.sizeAsInt(), 5);
    VE_ASSERT_EQ(got[0], a0);
    VE_ASSERT_EQ(got[1], a1);
    VE_ASSERT_EQ(got[2], a2);
    VE_ASSERT_EQ(got[3], x);
    VE_ASSERT_EQ(got[4], y);
}

VE_TEST(node_iterator_matches_children) {
    Node root("root");
    for (int i = 0; i < 3; ++i) root.append("");
    root.append("x");
    root.append("y");
    for (int i = 0; i < 2; ++i) root.append("x");

    auto kids = root.children();
    Vector<Node*> via_iter;
    for (auto* c : root) via_iter.push_back(c);

    VE_ASSERT_EQ(kids.sizeAsInt(), via_iter.sizeAsInt());
    for (int i = 0; i < kids.sizeAsInt(); ++i)
        VE_ASSERT_EQ(kids[i], via_iter[i]);
}

VE_TEST(node_iterator_postincrement) {
    Node root("root");
    root.append("a");
    root.append("b");

    auto it = root.begin();
    auto* first = *it++;
    auto* second = *it;
    VE_ASSERT_EQ(first->name(), "a");
    VE_ASSERT_EQ(second->name(), "b");
}

VE_TEST(node_rbegin_rend_empty) {
    Node root("root");
    VE_ASSERT(root.rbegin() == root.rend());
}

VE_TEST(node_reverse_named) {
    Node root("root");
    root.append("a");
    root.append("b");
    root.append("c");

    Vector<std::string> names;
    for (auto it = root.rbegin(); it != root.rend(); ++it)
        names.push_back((*it)->name());

    VE_ASSERT_EQ(names.sizeAsInt(), 3);
    VE_ASSERT_EQ(names[0], "c");
    VE_ASSERT_EQ(names[1], "b");
    VE_ASSERT_EQ(names[2], "a");
}

VE_TEST(node_reverse_anon) {
    Node root("root");
    for (int i = 0; i < 5; ++i) root.append();

    int cnt = 0;
    for (auto it = root.rbegin(); it != root.rend(); ++it) {
        VE_ASSERT(*it != nullptr);
        ++cnt;
    }
    VE_ASSERT_EQ(cnt, 5);
}

VE_TEST(node_reverse_mixed) {
    Node root("root");
    auto* a0 = root.append();
    auto* a1 = root.append();
    auto* x  = root.append("x");
    auto* a2 = root.append();
    auto* y  = root.append("y");

    // forward order: "" [a0,a1,a2], "x" [x], "y" [y]
    // reverse:       y, x, a2, a1, a0
    Vector<Node*> got;
    for (auto it = root.rbegin(); it != root.rend(); ++it)
        got.push_back(*it);

    VE_ASSERT_EQ(got.sizeAsInt(), 5);
    VE_ASSERT_EQ(got[0], y);
    VE_ASSERT_EQ(got[1], x);
    VE_ASSERT_EQ(got[2], a2);
    VE_ASSERT_EQ(got[3], a1);
    VE_ASSERT_EQ(got[4], a0);
}

VE_TEST(node_reverse_matches_forward) {
    Node root("root");
    for (int i = 0; i < 3; ++i) root.append();
    root.append("x");
    root.append("y");
    for (int i = 0; i < 2; ++i) root.append("x");

    Vector<Node*> fwd;
    for (auto* c : root) fwd.push_back(c);

    Vector<Node*> rev;
    for (auto it = root.rbegin(); it != root.rend(); ++it)
        rev.push_back(*it);

    VE_ASSERT_EQ(fwd.sizeAsInt(), rev.sizeAsInt());
    int n = fwd.sizeAsInt();
    for (int i = 0; i < n; ++i)
        VE_ASSERT_EQ(fwd[i], rev[(uint32_t)(n - 1 - i)]);
}

VE_TEST(node_reverse_postincrement) {
    Node root("root");
    root.append("a");
    root.append("b");

    auto it = root.rbegin();
    auto* first = *it++;
    auto* second = *it;
    VE_ASSERT_EQ(first->name(), "b");
    VE_ASSERT_EQ(second->name(), "a");
}
