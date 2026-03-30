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

    // valid keys: # and name# (implicit index 0)
    VE_ASSERT(Node::isKey("#"));        // global index 0
    VE_ASSERT(Node::isKey("a#"));       // name with index 0

    // invalid keys
    VE_ASSERT(!Node::isKey(""));        // empty
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

    // keys with implicit index 0
    VE_ASSERT_EQ(Node::keyIndex("hello"), 0);
    VE_ASSERT_EQ(Node::keyIndex("#"), 0);
    VE_ASSERT_EQ(Node::keyIndex("a#"), 0);

    // invalid keys
    VE_ASSERT_EQ(Node::keyIndex(""), -1);
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

VE_TEST(node_insert_at_zero) {
    Node root("root");
    auto* a = new Node("x");
    auto* b = new Node("y");

    // insert(child) to create first, then insert(child, 0) to prepend
    root.insert(a);
    VE_ASSERT(root.insert(b, 0));
    VE_ASSERT_EQ(root.count(), 2);
    VE_ASSERT_EQ(root.child(0), b);
    VE_ASSERT_EQ(root.child(1), a);
}

VE_TEST(node_insert_out_of_range_rejected) {
    Node root("root");
    auto* c = new Node();

    // index 0 on empty root → valid (the only valid position)
    VE_ASSERT(root.insert(c, 0));
    VE_ASSERT_EQ(root.count(), 1);

    // index 5 > count()==1 → rejected
    auto* d = new Node();
    VE_ASSERT(!root.insert(d, 5));
    VE_ASSERT_EQ(root.count(), 1);

    // negative overflow: -3 + 1 + 1 = -1 → rejected
    VE_ASSERT(!root.insert(d, -3));
    VE_ASSERT_EQ(root.count(), 1);
    delete d;
}

VE_TEST(node_insert_negative_index) {
    Node root("root");
    auto* a = new Node("a");
    auto* b = new Node("b");
    auto* c = new Node("c");
    auto* d = new Node("d");

    root.insert(a);         // [a]
    root.insert(b);         // [a, b]
    root.insert(c, -1);    // -1 → append → [a, b, c]
    VE_ASSERT_EQ(root.count(), 3);
    VE_ASSERT_EQ(root.child(2), c);

    root.insert(d, -2);    // -2 + 3+1 = 2 → insert before c → [a, b, d, c]
    VE_ASSERT_EQ(root.count(), 4);
    VE_ASSERT_EQ(root.child(0), a);
    VE_ASSERT_EQ(root.child(1), b);
    VE_ASSERT_EQ(root.child(2), d);
    VE_ASSERT_EQ(root.child(3), c);
}

VE_TEST(node_insert_batch) {
    Node root("root");
    root.append("x");   // [x]

    Node::Nodes batch;
    batch.push_back(new Node("a"));
    batch.push_back(new Node("b"));
    batch.push_back(new Node("c"));

    // batch insert at position 0 (prepend)
    VE_ASSERT(root.insert(batch, 0));
    VE_ASSERT_EQ(root.count(), 4);
    VE_ASSERT_EQ(root.child(0)->name(), "a");
    VE_ASSERT_EQ(root.child(1)->name(), "b");
    VE_ASSERT_EQ(root.child(2)->name(), "c");
    VE_ASSERT_EQ(root.child(3)->name(), "x");
}

VE_TEST(node_insert_batch_append) {
    Node root("root");
    root.append("x");   // [x]

    Node::Nodes batch;
    batch.push_back(new Node("a"));
    batch.push_back(new Node("b"));

    // batch insert at -1 (append)
    VE_ASSERT(root.insert(batch));
    VE_ASSERT_EQ(root.count(), 3);
    VE_ASSERT_EQ(root.child(0)->name(), "x");
    VE_ASSERT_EQ(root.child(1)->name(), "a");
    VE_ASSERT_EQ(root.child(2)->name(), "b");
}

VE_TEST(node_insert_batch_middle) {
    Node root("root");
    root.append("x");
    root.append("y");   // [x, y]

    Node::Nodes batch;
    batch.push_back(new Node("a"));
    batch.push_back(new Node("a"));

    // batch insert at position 1 (between x and y)
    VE_ASSERT(root.insert(batch, 1));
    VE_ASSERT_EQ(root.count(), 4);
    VE_ASSERT_EQ(root.child(0)->name(), "x");
    VE_ASSERT_EQ(root.child(1)->name(), "a");
    VE_ASSERT_EQ(root.child(2)->name(), "a");
    VE_ASSERT_EQ(root.child(3)->name(), "y");

    // same-name indices should be correct
    VE_ASSERT_EQ(root.count("a"), 2);
    VE_ASSERT_EQ(root.child("a", 0), root.child(1));
    VE_ASSERT_EQ(root.child("a", 1), root.child(2));
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

VE_TEST(node_append_overlap) {
    Node root("root");
    Node* target = root.append("item", 2);  // creates 3 nodes (1 + 2 overlap)

    VE_ASSERT(target != nullptr);
    VE_ASSERT_EQ(root.count(), 3);
    VE_ASSERT_EQ(root.count("item"), 3);
    VE_ASSERT_EQ(root.child("item", 0), target);  // returns first
}

VE_TEST(node_append_anon_overlap) {
    Node root("root");
    Node* c = root.append(3);  // append("", 3) → creates 4 anon nodes

    VE_ASSERT(c != nullptr);
    VE_ASSERT_EQ(root.count(), 4);
    VE_ASSERT_EQ(root.child(0), c);  // returns first
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
// Copy
// ============================================================================

VE_TEST(node_copy_into_empty_tree) {
    Node src("src");
    src.set(42);
    src.append("config")->set("fast");
    src.append("item")->set(10);
    src.append("item")->set(20);
    src.append("")->set(30);

    Node dst("dst");
    dst.copy(&src);

    VE_ASSERT_EQ(dst.getInt(), 42);
    VE_ASSERT_EQ(dst.count(), 4);
    VE_ASSERT_EQ(dst.child("config")->getString(), "fast");
    VE_ASSERT_EQ(dst.child("item", 0)->getInt(), 10);
    VE_ASSERT_EQ(dst.child("item", 1)->getInt(), 20);
    VE_ASSERT_EQ(dst.child(3)->getInt(), 30);
    VE_ASSERT(dst.child("config") != src.child("config"));
    VE_ASSERT(dst.child("item", 0) != src.child("item", 0));
}

VE_TEST(node_copy_inserts_before_matched_anchor) {
    Node src("src");
    src.append("head")->set(1);
    src.append("tail")->set(2);

    Node dst("dst");
    Node* tail = dst.append("tail");
    tail->set(-1);

    dst.copy(&src);

    VE_ASSERT_EQ(dst.count(), 2);
    VE_ASSERT_EQ(dst.child(0)->name(), "head");
    VE_ASSERT_EQ(dst.child(1), tail);
    VE_ASSERT_EQ(tail->getInt(), 2);
}

VE_TEST(node_copy_preserves_extra_children_when_auto_remove_false) {
    Node src("src");
    src.append("keep")->set(7);

    Node dst("dst");
    Node* extra = dst.append("extra");
    Node* keep  = dst.append("keep");
    keep->set(-1);

    dst.copy(&src, true, false);

    VE_ASSERT_EQ(dst.count(), 2);
    VE_ASSERT_EQ(dst.child("keep"), keep);
    VE_ASSERT_EQ(keep->getInt(), 7);
    VE_ASSERT(dst.has(extra));
}

VE_TEST(node_copy_auto_insert_false_does_not_add_missing_children) {
    Node src("src");
    src.append("keep")->set(7);
    src.append("add")->set(3);

    Node dst("dst");
    Node* keep = dst.append("keep");
    keep->set(-1);

    dst.copy(&src, false, false);

    VE_ASSERT_EQ(dst.count(), 1);
    VE_ASSERT_EQ(dst.child("keep"), keep);
    VE_ASSERT_EQ(keep->getInt(), 7);
    VE_ASSERT(!dst.has("add"));
}

VE_TEST(node_copy_removes_extra_children_when_auto_remove_true) {
    Node src("src");
    src.append("keep")->set(7);

    Node dst("dst");
    dst.append("extra")->set(99);
    Node* keep = dst.append("keep");
    keep->set(-1);

    dst.copy(&src, true, true);

    VE_ASSERT_EQ(dst.count(), 1);
    VE_ASSERT_EQ(dst.child("keep"), keep);
    VE_ASSERT_EQ(keep->getInt(), 7);
    VE_ASSERT(!dst.has("extra"));
}

VE_TEST(node_copy_matches_duplicate_names_by_overlap) {
    Node src("src");
    src.append("item")->set(10);
    src.append("item")->set(20);

    Node dst("dst");
    Node* first  = dst.append("item");
    Node* second = dst.append("item");
    dst.append("item")->set(99);

    dst.copy(&src, true, true);

    VE_ASSERT_EQ(dst.count("item"), 2);
    VE_ASSERT_EQ(dst.child("item", 0), first);
    VE_ASSERT_EQ(dst.child("item", 1), second);
    VE_ASSERT_EQ(first->getInt(), 10);
    VE_ASSERT_EQ(second->getInt(), 20);
}

VE_TEST(node_copy_matches_anonymous_children_by_occurrence) {
    Node src("src");
    src.append("")->set(1);
    src.append("named")->set(2);
    src.append("")->set(3);

    Node dst("dst");
    Node* anon0 = dst.append("");
    dst.append("named")->set(-1);
    Node* anon1 = dst.append("");
    dst.append("")->set(99);

    dst.copy(&src, true, true);

    VE_ASSERT_EQ(dst.count(), 3);
    VE_ASSERT_EQ(dst.child(0), anon0);
    VE_ASSERT_EQ(dst.child(2), anon1);
    VE_ASSERT_EQ(anon0->getInt(), 1);
    VE_ASSERT_EQ(dst.child("named")->getInt(), 2);
    VE_ASSERT_EQ(anon1->getInt(), 3);
}

VE_TEST(node_copy_reuses_nested_anonymous_children_in_place) {
    Node src("src");
    src.at("config/joint_types")->at(0)->set(10);
    src.at("config/joint_types")->at(1)->set(20);

    Node dst("dst");
    Node* joint_types = dst.at("config/joint_types");
    Node* first = joint_types->at(0);
    Node* second = joint_types->at(1);
    Node* extra = joint_types->at(2);
    first->set(-1);
    second->set(-2);
    extra->set(99);

    dst.copy(&src, true, false);

    VE_ASSERT_EQ(joint_types->count(), 3);
    VE_ASSERT_EQ(joint_types->child(0), first);
    VE_ASSERT_EQ(joint_types->child(1), second);
    VE_ASSERT_EQ(joint_types->child(2), extra);
    VE_ASSERT_EQ(first->getInt(), 10);
    VE_ASSERT_EQ(second->getInt(), 20);
    VE_ASSERT_EQ(extra->getInt(), 99);
}

VE_TEST(node_copy_reuses_nested_duplicate_named_children_by_overlap) {
    Node src("src");
    Node* src_cfg = src.at("config");
    src_cfg->append("joint")->set(1);
    src_cfg->append("joint")->set(2);

    Node dst("dst");
    Node* dst_cfg = dst.at("config");
    Node* first = dst_cfg->append("joint");
    Node* second = dst_cfg->append("joint");
    Node* extra = dst_cfg->append("joint");
    first->set(-1);
    second->set(-2);
    extra->set(99);

    dst.copy(&src, true, false);

    VE_ASSERT_EQ(dst_cfg->count("joint"), 3);
    VE_ASSERT_EQ(dst_cfg->child("joint", 0), first);
    VE_ASSERT_EQ(dst_cfg->child("joint", 1), second);
    VE_ASSERT_EQ(dst_cfg->child("joint", 2), extra);
    VE_ASSERT_EQ(first->getInt(), 1);
    VE_ASSERT_EQ(second->getInt(), 2);
    VE_ASSERT_EQ(extra->getInt(), 99);
}

VE_TEST(node_copy_nested_auto_remove_clears_extra_children) {
    Node src("src");
    src.at("value")->append("joint")->set(1);
    src.at("value")->append("joint")->set(2);

    Node dst("dst");
    Node* value = dst.at("value");
    Node* first = value->append("joint");
    Node* second = value->append("joint");
    value->append("joint")->set(99);
    first->set(-1);
    second->set(-2);

    value->copy(src.at("value"), true, true, false);

    VE_ASSERT_EQ(value->count("joint"), 2);
    VE_ASSERT_EQ(value->child("joint", 0), first);
    VE_ASSERT_EQ(value->child("joint", 1), second);
    VE_ASSERT_EQ(first->getInt(), 1);
    VE_ASSERT_EQ(second->getInt(), 2);
}

VE_TEST(node_copy_default_set_emits_changed_even_if_equal) {
    Node src("src");
    src.set(9);
    src.append("keep")->set(7);

    Node dst("dst");
    dst.set(9);
    Node* keep = dst.append("keep");
    keep->set(7);

    int root_changed = 0;
    int keep_changed = 0;
    dst.connect<Node::NODE_CHANGED>(&dst, [&](const Var&, const Var&) { ++root_changed; });
    keep->connect<Node::NODE_CHANGED>(keep, [&](const Var&, const Var&) { ++keep_changed; });

    dst.copy(&src);

    VE_ASSERT_EQ(root_changed, 1);
    VE_ASSERT_EQ(keep_changed, 1);
    VE_ASSERT_EQ(dst.getInt(), 9);
    VE_ASSERT_EQ(keep->getInt(), 7);
}

VE_TEST(node_copy_auto_update_suppresses_equal_value_signal) {
    Node src("src");
    src.set(9);
    src.append("keep")->set(7);

    Node dst("dst");
    dst.set(9);
    Node* keep = dst.append("keep");
    keep->set(7);

    int root_changed = 0;
    int keep_changed = 0;
    dst.connect<Node::NODE_CHANGED>(&dst, [&](const Var&, const Var&) { ++root_changed; });
    keep->connect<Node::NODE_CHANGED>(keep, [&](const Var&, const Var&) { ++keep_changed; });

    dst.copy(&src, true, false, true);

    VE_ASSERT_EQ(root_changed, 0);
    VE_ASSERT_EQ(keep_changed, 0);
    VE_ASSERT_EQ(dst.getInt(), 9);
    VE_ASSERT_EQ(keep->getInt(), 7);
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
