// test_node_management.cpp — insert, append, take, remove, clear, name validation
#include "ve_test.h"
#include "ve/core/node.h"

using namespace ve;

// ============================================================================
// Key validation
// ============================================================================

VE_TEST(node_isKey) {
    // valid keys: plain name
    VE_ASSERT(Node::isKey("hello"));
    VE_ASSERT(Node::isKey("item_2"));
    VE_ASSERT(Node::isKey("a.b"));

    // valid keys: name#N
    VE_ASSERT(Node::isKey("item#0"));
    VE_ASSERT(Node::isKey("item#2"));
    VE_ASSERT(Node::isKey("tag#100"));

    // valid keys: #N (global index)
    VE_ASSERT(Node::isKey("#0"));
    VE_ASSERT(Node::isKey("#99"));

    // invalid keys
    VE_ASSERT(!Node::isKey(""));        // empty
    VE_ASSERT(!Node::isKey("#"));       // no digits after #
    VE_ASSERT(!Node::isKey("a#"));      // no digits after #
    VE_ASSERT(!Node::isKey("#abc"));    // non-digit after #
    VE_ASSERT(!Node::isKey("a#b"));     // non-digit after #
    VE_ASSERT(!Node::isKey("/"));       // path separator
    VE_ASSERT(!Node::isKey("a/b"));     // path separator
    VE_ASSERT(!Node::isKey("a#1/b"));   // path separator
}

VE_TEST(node_keyIndex) {
    // keys with explicit index
    VE_ASSERT_EQ(Node::keyIndex("#0"), 0);
    VE_ASSERT_EQ(Node::keyIndex("#5"), 5);
    VE_ASSERT_EQ(Node::keyIndex("#99"), 99);
    VE_ASSERT_EQ(Node::keyIndex("item#0"), 0);
    VE_ASSERT_EQ(Node::keyIndex("item#3"), 3);
    VE_ASSERT_EQ(Node::keyIndex("tag#100"), 100);

    // keys without explicit index
    VE_ASSERT_EQ(Node::keyIndex("hello"), -1);
    VE_ASSERT_EQ(Node::keyIndex(""), -1);
    VE_ASSERT_EQ(Node::keyIndex("#"), -1);
    VE_ASSERT_EQ(Node::keyIndex("a#"), -1);
    VE_ASSERT_EQ(Node::keyIndex("#abc"), -1);
}

VE_TEST(node_insert_null_rejected) {
    Node root("root");
    VE_ASSERT(!root.insert(nullptr));
    VE_ASSERT(!root.insert(nullptr, 0));
    VE_ASSERT_EQ(root.count(), 0);
}

// ============================================================================
// Insert
// ============================================================================

VE_TEST(node_insert_basic) {
    Node root("root");
    Node* a = new Node("a");
    Node* b = new Node("b");

    VE_ASSERT(root.insert(a));
    VE_ASSERT(root.insert(b));
    VE_ASSERT_EQ(root.count(), 2);
    VE_ASSERT_EQ(root.child("a"), a);
    VE_ASSERT_EQ(root.child("b"), b);
}

VE_TEST(node_insert_at_index) {
    Node root("root");
    auto* i0 = new Node("item");
    auto* i2 = new Node("item");
    auto* i1 = new Node("item");

    root.insert(i0);
    root.insert(i2);
    // insert i1 between i0 and i2
    VE_ASSERT(root.insert(i1, 1));
    VE_ASSERT_EQ(root.count("item"), 3);
    VE_ASSERT_EQ(root.child("item", 0), i0);
    VE_ASSERT_EQ(root.child("item", 1), i1);
    VE_ASSERT_EQ(root.child("item", 2), i2);
}

VE_TEST(node_insert_at_zero_new_group) {
    Node root("root");
    auto* c = new Node("x");
    // insert at 0 when group doesn't exist → OK
    VE_ASSERT(root.insert(c, 0));
    VE_ASSERT_EQ(root.count(), 1);
}

VE_TEST(node_insert_auto_fill) {
    Node root("root");
    root.append("");  // anon #0

    auto* target = new Node();
    VE_ASSERT(root.insert(target, 5));  // auto_fill = true

    // 1 existing + 4 fillers + 1 target = 6
    VE_ASSERT_EQ(root.count(), 6);
    VE_ASSERT_EQ(root.child("", 5), target);
    for (int i = 1; i < 5; ++i)
        VE_ASSERT(root.child("", i) != nullptr);
}

VE_TEST(node_insert_no_auto_fill) {
    Node root("root");
    auto* c = new Node();

    // auto_fill=false, index > size → clamp to end
    VE_ASSERT(root.insert(c, 5, false));
    VE_ASSERT_EQ(root.count(), 1);
    VE_ASSERT_EQ(root.child("", 0), c);
}

VE_TEST(node_insert_auto_fill_100) {
    Node root("root");
    root.append("");

    auto* target = new Node();
    VE_ASSERT(root.insert(target, 100));

    // 1 existing + 99 fillers + 1 target = 101
    VE_ASSERT_EQ(root.count(), 101);
    VE_ASSERT_EQ(root.child("", 100), target);
}

VE_TEST(node_insert_prepend) {
    Node root("root");
    auto* a = new Node();
    auto* b = new Node();
    root.insert(a);
    root.insert(b, 0);  // prepend

    VE_ASSERT_EQ(root.count(), 2);
    VE_ASSERT_EQ(root.child("", 0), b);
    VE_ASSERT_EQ(root.child("", 1), a);
}

VE_TEST(node_insert_reparent) {
    Node r1("r1");
    Node r2("r2");

    Node* c = r1.append("c");
    VE_ASSERT(c->parent() == &r1);
    VE_ASSERT_EQ(r1.count(), 1);

    r2.insert(c);  // auto-reparents from r1
    VE_ASSERT(c->parent() == &r2);
    VE_ASSERT_EQ(r1.count(), 0);
    VE_ASSERT_EQ(r2.count(), 1);
}

// ============================================================================
// Append (convenience wrappers)
// ============================================================================

VE_TEST(node_append_basic) {
    Node root("root");
    Node* x = root.append("x");
    VE_ASSERT(x != nullptr);
    VE_ASSERT_EQ(x->name(), "x");
    VE_ASSERT(x->parent() == &root);
}

VE_TEST(node_append_any_name) {
    // insert no longer rejects names with # or / — validation is in key/path methods
    Node root("root");
    VE_ASSERT(root.append("has#hash") != nullptr);
    VE_ASSERT(root.append("has/slash") != nullptr);
    VE_ASSERT_EQ(root.count(), 2);
}

VE_TEST(node_append_at_index) {
    Node root("root");
    root.append("");
    Node* target = root.append("", 5);

    VE_ASSERT(target != nullptr);
    VE_ASSERT_EQ(root.count(), 6);
    VE_ASSERT_EQ(root.child("", 5), target);
}

VE_TEST(node_append_anon_at_index) {
    Node root("root");
    Node* c = root.append(3);  // append(int, bool)

    VE_ASSERT(c != nullptr);
    VE_ASSERT_EQ(root.count(), 4);  // 3 fillers + 1
    VE_ASSERT_EQ(root.child("", 3), c);
}

// ============================================================================
// Take
// ============================================================================

VE_TEST(node_take_by_ptr) {
    Node root("root");
    Node* a = root.append("a");
    root.append("b");

    Node* taken = root.take(a);
    VE_ASSERT_EQ(taken, a);
    VE_ASSERT(a->parent() == nullptr);
    VE_ASSERT_EQ(root.count(), 1);
    VE_ASSERT(root.child("a") == nullptr);

    delete a;
}

VE_TEST(node_take_by_name) {
    Node root("root");
    Node* x = root.append("x");
    root.append("y");

    Node* taken = root.take("x");
    VE_ASSERT_EQ(taken, x);
    VE_ASSERT_EQ(root.count(), 1);

    delete x;
}

VE_TEST(node_take_null) {
    Node root("root");
    VE_ASSERT(root.take(nullptr) == nullptr);
}

VE_TEST(node_take_nonexistent) {
    Node root("root");
    VE_ASSERT(root.take("nope") == nullptr);
}

// ============================================================================
// Remove
// ============================================================================

VE_TEST(node_remove_by_ptr) {
    Node root("root");
    Node* a = root.append("a");
    root.append("b");

    VE_ASSERT(root.remove(a));
    VE_ASSERT_EQ(root.count(), 1);
    VE_ASSERT(root.child("a") == nullptr);
    VE_ASSERT(root.child("b") != nullptr);
}

VE_TEST(node_remove_by_name_index) {
    Node root("root");
    root.append("item");
    root.append("item");
    root.append("item");

    VE_ASSERT(root.remove("item", 1));
    VE_ASSERT_EQ(root.count("item"), 2);
    VE_ASSERT_EQ(root.count(), 2);
}

VE_TEST(node_remove_all_by_name) {
    Node root("root");
    root.append("x");
    root.append("y");
    root.append("x");

    VE_ASSERT(root.remove("x"));
    VE_ASSERT_EQ(root.count("x"), 0);
    VE_ASSERT_EQ(root.count(), 1);
}

VE_TEST(node_remove_nonexistent) {
    Node root("root");
    VE_ASSERT(!root.remove("nope"));
}

// ============================================================================
// Clear
// ============================================================================

VE_TEST(node_clear) {
    Node root("root");
    root.append("a");
    root.append("b");
    root.append("c");
    VE_ASSERT_EQ(root.count(), 3);

    root.clear();
    VE_ASSERT_EQ(root.count(), 0);
}

VE_TEST(node_clear_no_delete) {
    Node root("root");
    Node* a = root.append("a");

    root.clear(false);  // don't delete children
    VE_ASSERT_EQ(root.count(), 0);
    VE_ASSERT(a->parent() == nullptr);

    delete a;  // manual cleanup
}

// ============================================================================
// Destructor
// ============================================================================

VE_TEST(node_destructor_deletes_children) {
    auto* root = new Node("root");
    root->append("a");
    root->append("b");
    root->append("c");
    delete root;
    VE_ASSERT(true);  // no crash = pass
}
