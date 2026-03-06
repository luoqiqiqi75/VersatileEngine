// test_node.cpp — ve::Node + ve::Schema tests
#include "ve_test.h"
#include "ve/core/node.h"
#include "ve/core/log.h"

using namespace ve;

// ============================================================================
// Basic creation
// ============================================================================

VE_TEST(node_create_default) {
    Node n;
    VE_ASSERT_EQ(n.name(), "");
    VE_ASSERT(n.parent() == nullptr);
    VE_ASSERT_EQ(n.childCount(), 0);
}

VE_TEST(node_create_named) {
    Node n("root");
    VE_ASSERT_EQ(n.name(), "root");
}

// ============================================================================
// Named children
// ============================================================================

VE_TEST(node_append_named_child) {
    Node root("root");
    Node* a = root.append("a", new Node());
    Node* b = root.append("b", new Node());

    VE_ASSERT_EQ(root.childCount(), 2);
    VE_ASSERT_EQ(a->name(), "a");
    VE_ASSERT_EQ(b->name(), "b");
    VE_ASSERT(a->parent() == &root);
    VE_ASSERT(b->parent() == &root);
}

VE_TEST(node_child_by_name) {
    Node root("root");
    root.append("x", new Node());
    root.append("y", new Node());

    VE_ASSERT(root.child("x") != nullptr);
    VE_ASSERT(root.child("y") != nullptr);
    VE_ASSERT(root.child("z") == nullptr);
    VE_ASSERT_EQ(root.child("x")->name(), "x");
}

VE_TEST(node_append_own_name) {
    Node root("root");
    Node* c = new Node("myname");
    root.append(c);
    VE_ASSERT_EQ(root.child("myname"), c);
    VE_ASSERT_EQ(root.childCount(), 1);
}

VE_TEST(node_has_child) {
    Node root("root");
    root.append("foo", new Node());
    VE_ASSERT(root.hasChild("foo"));
    VE_ASSERT(!root.hasChild("bar"));
}

VE_TEST(node_remove_child_ptr) {
    Node root("root");
    Node* a = root.append("a", new Node());
    root.append("b", new Node());

    VE_ASSERT_EQ(root.childCount(), 2);
    bool ok = root.remove(a);
    VE_ASSERT(ok);
    VE_ASSERT_EQ(root.childCount(), 1);
    VE_ASSERT(root.child("a") == nullptr);
    VE_ASSERT(root.child("b") != nullptr);
}

VE_TEST(node_remove_by_name) {
    Node root("root");
    root.append("x", new Node());
    root.append("y", new Node());

    bool ok = root.remove("x");
    VE_ASSERT(ok);
    VE_ASSERT_EQ(root.childCount(), 1);
    VE_ASSERT(root.child("x") == nullptr);
}

VE_TEST(node_remove_nonexistent) {
    Node root("root");
    bool ok = root.remove("nope");
    VE_ASSERT(!ok);
}

VE_TEST(node_clear_children) {
    Node root("root");
    root.append("a", new Node());
    root.append("b", new Node());
    root.append("c", new Node());
    VE_ASSERT_EQ(root.childCount(), 3);

    root.clearChildren();
    VE_ASSERT_EQ(root.childCount(), 0);
}

// ============================================================================
// Duplicate-named children (XML pattern)
// ============================================================================

VE_TEST(node_duplicate_names) {
    Node root("root");
    Node* i0 = root.append("item", new Node());
    Node* i1 = root.append("item", new Node());
    Node* i2 = root.append("item", new Node());

    VE_ASSERT_EQ(root.childCount(), 3);
    VE_ASSERT_EQ(root.childCount("item"), 3);

    VE_ASSERT_EQ(root.child("item", 0), i0);
    VE_ASSERT_EQ(root.child("item", 1), i1);
    VE_ASSERT_EQ(root.child("item", 2), i2);
    VE_ASSERT(root.child("item", 3) == nullptr);
    VE_ASSERT_EQ(root.child("item"), i0);
}

VE_TEST(node_children_by_name) {
    Node root("root");
    root.append("item", new Node());
    root.append("other", new Node());
    root.append("item", new Node());

    auto items = root.children("item");
    VE_ASSERT_EQ(items.sizeAsInt(), 2);

    auto others = root.children("other");
    VE_ASSERT_EQ(others.sizeAsInt(), 1);
}

VE_TEST(node_remove_duplicate_by_index) {
    Node root("root");
    root.append("item", new Node());
    root.append("item", new Node());
    root.append("item", new Node());

    bool ok = root.remove("item", 1);
    VE_ASSERT(ok);
    VE_ASSERT_EQ(root.childCount("item"), 2);
    VE_ASSERT_EQ(root.childCount(), 2);
}

// ============================================================================
// Anonymous children (list pattern)
// ============================================================================

VE_TEST(node_anonymous_children) {
    Node root("root");
    Node* c0 = root.append("", new Node());
    Node* c1 = root.append("", new Node());
    Node* c2 = root.append("", new Node());

    VE_ASSERT_EQ(root.childCount(), 3);
    VE_ASSERT_EQ(root.childCount(""), 3);
    VE_ASSERT_EQ(c0->name(), "");
    VE_ASSERT_EQ(root.child("", 0), c0);
    VE_ASSERT_EQ(root.child("", 1), c1);
    VE_ASSERT_EQ(root.child("", 2), c2);
}

// ============================================================================
// Global index: childAt
// ============================================================================

VE_TEST(node_childAt_pure_list) {
    Node root("root");
    for (int i = 0; i < 100; ++i)
        root.append("", new Node());

    VE_ASSERT_EQ(root.childCount(), 100);
    VE_ASSERT(root.childAt(0) != nullptr);
    VE_ASSERT(root.childAt(50) != nullptr);
    VE_ASSERT(root.childAt(99) != nullptr);
    VE_ASSERT(root.childAt(100) == nullptr);
    VE_ASSERT(root.childAt(-1) == nullptr);
}

VE_TEST(node_childAt_named) {
    Node root("root");
    Node* a = root.append("a", new Node());
    Node* b = root.append("b", new Node());
    Node* c = root.append("c", new Node());

    VE_ASSERT_EQ(root.childAt(0), a);
    VE_ASSERT_EQ(root.childAt(1), b);
    VE_ASSERT_EQ(root.childAt(2), c);
}

VE_TEST(node_childAt_mixed) {
    Node root("root");
    Node* a0 = root.append("", new Node());
    Node* a1 = root.append("", new Node());
    Node* x  = root.append("x", new Node());
    Node* a2 = root.append("", new Node());
    Node* y  = root.append("y", new Node());

    VE_ASSERT_EQ(root.childCount(), 5);

    // OrdDict order: "" group (3), "x" (1), "y" (1)
    VE_ASSERT_EQ(root.childAt(0), a0);
    VE_ASSERT_EQ(root.childAt(1), a1);
    VE_ASSERT_EQ(root.childAt(2), a2);
    VE_ASSERT_EQ(root.childAt(3), x);
    VE_ASSERT_EQ(root.childAt(4), y);
}

VE_TEST(node_indexOf) {
    Node root("root");
    Node* a = root.append("a", new Node());
    Node* b = root.append("b", new Node());
    Node* c = root.append("c", new Node());

    VE_ASSERT_EQ(root.indexOf(a), 0);
    VE_ASSERT_EQ(root.indexOf(b), 1);
    VE_ASSERT_EQ(root.indexOf(c), 2);

    Node other("other");
    VE_ASSERT_EQ(root.indexOf(&other), -1);
}

// ============================================================================
// Iteration
// ============================================================================

VE_TEST(node_children_order) {
    Node root("root");
    root.append("b", new Node());
    root.append("a", new Node());
    root.append("c", new Node());

    auto kids = root.children();
    VE_ASSERT_EQ(kids.sizeAsInt(), 3);
    VE_ASSERT_EQ(kids[0]->name(), "b");
    VE_ASSERT_EQ(kids[1]->name(), "a");
    VE_ASSERT_EQ(kids[2]->name(), "c");
}

VE_TEST(node_child_names) {
    Node root("root");
    root.append("x", new Node());
    root.append("y", new Node());
    root.append("x", new Node()); // duplicate, same group

    auto names = root.childNames();
    VE_ASSERT_EQ(names.sizeAsInt(), 2);
    VE_ASSERT_EQ(names[0], "x");
    VE_ASSERT_EQ(names[1], "y");
}

// ============================================================================
// Name validation
// ============================================================================

VE_TEST(node_valid_name) {
    VE_ASSERT(Node::isValidName(""));
    VE_ASSERT(Node::isValidName("hello"));
    VE_ASSERT(Node::isValidName("item_2"));
    VE_ASSERT(Node::isValidName("a.b")); // dots are fine

    VE_ASSERT(!Node::isValidName("#"));
    VE_ASSERT(!Node::isValidName("#0"));
    VE_ASSERT(!Node::isValidName("item#2"));
    VE_ASSERT(!Node::isValidName("/"));
    VE_ASSERT(!Node::isValidName("a/b"));
    VE_ASSERT(!Node::isValidName("a#b/c"));
}

VE_TEST(node_insert_invalid_name_rejected) {
    Node root("root");
    Node* child = new Node();

    VE_ASSERT(!root.insert("bad#name", child));
    VE_ASSERT(!root.insert("bad/name", child));
    VE_ASSERT_EQ(root.childCount(), 0);

    delete child;
}

// ============================================================================
// Insert (core)
// ============================================================================

VE_TEST(node_insert_name) {
    Node root("root");
    Node* a = new Node();
    Node* b = new Node();

    VE_ASSERT(root.insert("a", a));
    VE_ASSERT(root.insert("b", b));
    VE_ASSERT_EQ(root.childCount(), 2);
    VE_ASSERT_EQ(root.child("a"), a);
    VE_ASSERT_EQ(root.child("b"), b);
}

VE_TEST(node_insert_name_index) {
    Node root("root");
    Node* i0 = new Node();
    Node* i1 = new Node();
    Node* i2 = new Node();

    root.insert("item", i0);
    root.insert("item", i2);
    // insert i1 between i0 and i2
    VE_ASSERT(root.insert("item", 1, i1));
    VE_ASSERT_EQ(root.childCount("item"), 3);
    VE_ASSERT_EQ(root.child("item", 0), i0);
    VE_ASSERT_EQ(root.child("item", 1), i1);
    VE_ASSERT_EQ(root.child("item", 2), i2);
}

VE_TEST(node_insert_name_index_bounds) {
    Node root("root");
    Node* child = new Node();

    // insert at index 5 when group doesn't exist → fail
    VE_ASSERT(!root.insert("x", 5, child));
    VE_ASSERT_EQ(root.childCount(), 0);

    // insert at index 0 when group doesn't exist → ok (empty → create)
    VE_ASSERT(root.insert("x", 0, child));
    VE_ASSERT_EQ(root.childCount(), 1);
}

VE_TEST(node_insert_at_auto_fill) {
    Node root("root");
    root.append("", new Node()); // anon #0

    Node* target = new Node();
    VE_ASSERT(root.insertAt(5, target));

    // should have 6 anonymous children: #0, #1(filler), #2, #3, #4(filler), #5(target)
    VE_ASSERT_EQ(root.childCount(), 6);
    VE_ASSERT_EQ(root.child("", 5), target);

    // fillers at indices 1-4 should exist
    for (int i = 1; i < 5; ++i)
        VE_ASSERT(root.child("", i) != nullptr);
}

VE_TEST(node_insert_at_no_auto_fill) {
    Node root("root");
    Node* child = new Node();

    // without auto_fill, insertAt beyond size fails
    VE_ASSERT(!root.insertAt(5, child, false));
    VE_ASSERT_EQ(root.childCount(), 0);

    // at index 0 is fine
    VE_ASSERT(root.insertAt(0, child, false));
    VE_ASSERT_EQ(root.childCount(), 1);
}

VE_TEST(node_insert_at_100_imol_style) {
    Node root("root");
    root.append("", new Node()); // 1 anonymous child

    Node* target = new Node();
    VE_ASSERT(root.insertAt(100, target));

    // 1 existing + 99 fillers + 1 target = 101
    VE_ASSERT_EQ(root.childCount(), 101);
    VE_ASSERT_EQ(root.child("", 100), target);
    VE_ASSERT(root.child("", 0) != nullptr);
    VE_ASSERT(root.child("", 50) != nullptr);
}

VE_TEST(node_insert_at_prepend) {
    Node root("root");
    Node* a = new Node();
    Node* b = new Node();
    root.insert("", a);
    root.insertAt(0, b); // prepend

    VE_ASSERT_EQ(root.childCount(), 2);
    VE_ASSERT_EQ(root.child("", 0), b);
    VE_ASSERT_EQ(root.child("", 1), a);
}

// ============================================================================
// Append calls insert
// ============================================================================

VE_TEST(node_append_returns_child) {
    Node root("root");
    Node* child = new Node();
    Node* result = root.append("x", child);
    VE_ASSERT_EQ(result, child);
}

VE_TEST(node_append_invalid_name_returns_null) {
    Node root("root");
    Node* child = new Node();
    Node* result = root.append("bad#name", child);
    VE_ASSERT(result == nullptr);
    delete child;
}

// ============================================================================
// Parent management
// ============================================================================

VE_TEST(node_reparent) {
    Node root1("r1");
    Node root2("r2");

    Node* child = root1.append("c", new Node());
    VE_ASSERT(child->parent() == &root1);
    VE_ASSERT_EQ(root1.childCount(), 1);

    root2.append("c", child);
    VE_ASSERT(child->parent() == &root2);
    VE_ASSERT_EQ(root1.childCount(), 0);
    VE_ASSERT_EQ(root2.childCount(), 1);
}

VE_TEST(node_destructor_deletes_children) {
    auto* root = new Node("root");
    root->append("a", new Node());
    root->append("b", new Node());
    root->append("c", new Node());
    delete root;
    VE_ASSERT(true);
}

// ============================================================================
// Parent navigation (imol p(level))
// ============================================================================

VE_TEST(node_parent_level) {
    Node root("root");
    Node* a = root.append("a", new Node());
    Node* b = a->append("b", new Node());
    Node* c = b->append("c", new Node());

    VE_ASSERT(c->parent() == b);       // level=0 → direct parent
    VE_ASSERT(c->parent(0) == b);
    VE_ASSERT(c->parent(1) == a);      // level=1 → grandparent
    VE_ASSERT(c->parent(2) == &root);  // level=2 → great-grandparent
    VE_ASSERT(c->parent(3) == nullptr);// level=3 → beyond root
}

VE_TEST(node_index_in_parent) {
    Node root("root");
    Node* a = root.append("a", new Node());
    Node* b = root.append("b", new Node());
    Node* c = root.append("c", new Node());

    VE_ASSERT_EQ(a->indexInParent(), 0);
    VE_ASSERT_EQ(b->indexInParent(), 1);
    VE_ASSERT_EQ(c->indexInParent(), 2);
    VE_ASSERT_EQ(root.indexInParent(), -1); // root has no parent
}

// ============================================================================
// Sibling navigation (imol b(offset))
// ============================================================================

VE_TEST(node_sibling_offset) {
    Node root("root");
    Node* a = root.append("a", new Node());
    Node* b = root.append("b", new Node());
    Node* c = root.append("c", new Node());

    VE_ASSERT_EQ(a->sibling(1), b);
    VE_ASSERT_EQ(a->sibling(2), c);
    VE_ASSERT_EQ(c->sibling(-1), b);
    VE_ASSERT_EQ(c->sibling(-2), a);
    VE_ASSERT(a->sibling(-1) == nullptr);  // before first
    VE_ASSERT(c->sibling(1) == nullptr);   // after last
}

VE_TEST(node_prev_next) {
    Node root("root");
    Node* a = root.append("a", new Node());
    Node* b = root.append("b", new Node());
    Node* c = root.append("c", new Node());

    VE_ASSERT(a->prev() == nullptr);
    VE_ASSERT_EQ(a->next(), b);
    VE_ASSERT_EQ(b->prev(), a);
    VE_ASSERT_EQ(b->next(), c);
    VE_ASSERT_EQ(c->prev(), b);
    VE_ASSERT(c->next() == nullptr);
}

VE_TEST(node_first_last) {
    Node root("root");
    Node* a = root.append("a", new Node());
    root.append("b", new Node());
    Node* c = root.append("c", new Node());

    VE_ASSERT_EQ(root.first(), a);
    VE_ASSERT_EQ(root.last(), c);

    Node empty("empty");
    VE_ASSERT(empty.first() == nullptr);
    VE_ASSERT(empty.last() == nullptr);
}

// ============================================================================
// Shadow (prototype chain)
// ============================================================================

VE_TEST(node_shadow_child_fallback) {
    Node prototype("proto");
    prototype.append("default_x", new Node());
    prototype.append("default_y", new Node());

    Node instance("inst");
    instance.append("local_z", new Node());
    instance.setShadow(&prototype);

    VE_ASSERT(instance.child("local_z") != nullptr);
    VE_ASSERT(instance.child("default_x") != nullptr);
    VE_ASSERT(instance.child("default_y") != nullptr);
    VE_ASSERT(instance.child("nope") == nullptr);
}

VE_TEST(node_shadow_chain) {
    Node base("base");
    base.append("from_base", new Node());

    Node mid("mid");
    mid.append("from_mid", new Node());
    mid.setShadow(&base);

    Node leaf("leaf");
    leaf.append("from_leaf", new Node());
    leaf.setShadow(&mid);

    VE_ASSERT(leaf.child("from_leaf") != nullptr);
    VE_ASSERT(leaf.child("from_mid") != nullptr);
    VE_ASSERT(leaf.child("from_base") != nullptr);
    VE_ASSERT(leaf.child("nope") == nullptr);
}

VE_TEST(node_shadow_has_child) {
    Node proto("proto");
    proto.append("field", new Node());

    Node inst("inst");
    inst.setShadow(&proto);

    VE_ASSERT(inst.hasChild("field"));
    VE_ASSERT(!inst.hasChild("other"));
}

// ============================================================================
// Schema
// ============================================================================

VE_TEST(schema_build_flat) {
    auto schema = Schema::create({SchemaField("x"), SchemaField("y"), SchemaField("z")});
    VE_ASSERT_EQ(schema->fieldCount(), 3);

    Node node("point");
    schema->build(&node);

    VE_ASSERT_EQ(node.childCount(), 3);
    VE_ASSERT(node.child("x") != nullptr);
    VE_ASSERT(node.child("y") != nullptr);
    VE_ASSERT(node.child("z") != nullptr);
}

VE_TEST(schema_build_nested) {
    auto inner = Schema::create({SchemaField("a"), SchemaField("b")});
    auto outer = Schema::create({
        SchemaField("x"),
        SchemaField("sub", inner),
        SchemaField("y")
    });

    Node root("root");
    outer->build(&root);

    VE_ASSERT_EQ(root.childCount(), 3);
    VE_ASSERT(root.child("x") != nullptr);
    VE_ASSERT(root.child("y") != nullptr);

    Node* sub = root.child("sub");
    VE_ASSERT(sub != nullptr);
    VE_ASSERT_EQ(sub->childCount(), 2);
    VE_ASSERT(sub->child("a") != nullptr);
    VE_ASSERT(sub->child("b") != nullptr);
}

VE_TEST(schema_shared) {
    auto schema = Schema::create({SchemaField("x"), SchemaField("y")});

    Node n1("n1");
    schema->build(&n1);

    Node n2("n2");
    schema->build(&n2);

    VE_ASSERT_EQ(n1.childCount(), 2);
    VE_ASSERT_EQ(n2.childCount(), 2);
    VE_ASSERT(n1.child("x") != n2.child("x"));
}

// ============================================================================
// Static root
// ============================================================================

VE_TEST(node_static_root) {
    Node* r1 = Node::root();
    Node* r2 = Node::root();
    VE_ASSERT(r1 == r2);
    VE_ASSERT_EQ(r1->name(), "");
}

// ============================================================================
// Path: resolve() with / separator and name#N
// ============================================================================

VE_TEST(node_resolve_simple) {
    Node root("root");
    root.append("a", new Node());
    Node* b = root.append("b", new Node());
    b->append("c", new Node());

    VE_ASSERT_EQ(root.resolve("a")->name(), "a");
    VE_ASSERT_EQ(root.resolve("b")->name(), "b");
    VE_ASSERT_EQ(root.resolve("b/c")->name(), "c");
    VE_ASSERT(root.resolve("b/d") == nullptr);
    VE_ASSERT(root.resolve("z") == nullptr);
}

VE_TEST(node_resolve_duplicate_index) {
    Node root("root");
    root.append("item", new Node());
    Node* i1 = root.append("item", new Node());
    root.append("item", new Node());

    VE_ASSERT_EQ(root.resolve("item"), root.child("item", 0));
    VE_ASSERT_EQ(root.resolve("item#1"), i1);
    VE_ASSERT_EQ(root.resolve("item#2"), root.child("item", 2));
    VE_ASSERT(root.resolve("item#3") == nullptr);
}

VE_TEST(node_resolve_global_index) {
    Node root("root");
    Node* a = root.append("a", new Node());
    Node* b = root.append("b", new Node());

    VE_ASSERT_EQ(root.resolve("#0"), a);
    VE_ASSERT_EQ(root.resolve("#1"), b);
    VE_ASSERT(root.resolve("#2") == nullptr);
}

VE_TEST(node_resolve_absolute) {
    Node root("root");
    Node* a = root.append("a", new Node());
    Node* b = a->append("b", new Node());
    Node* c = b->append("c", new Node());

    // from any depth, /a/b/c reaches c via root
    VE_ASSERT_EQ(c->resolve("/a/b/c"), c);
    VE_ASSERT_EQ(b->resolve("/a/b"), b);
    VE_ASSERT_EQ(c->resolve("/a"), a);
}

VE_TEST(node_resolve_nested_index) {
    Node root("root");
    Node* items = root.append("items", new Node());
    items->append("item", new Node());
    Node* i1 = items->append("item", new Node());
    i1->append("value", new Node());

    VE_ASSERT_EQ(root.resolve("items/item#1/value")->name(), "value");
}

VE_TEST(node_resolve_empty) {
    Node root("root");
    VE_ASSERT_EQ(root.resolve(""), &root);
}

// ============================================================================
// Path: path() builds path string
// ============================================================================

VE_TEST(node_path_simple) {
    Node root("root");
    Node* a = root.append("a", new Node());
    Node* b = a->append("b", new Node());
    Node* c = b->append("c", new Node());

    VE_ASSERT_EQ(a->path(&root), "a");
    VE_ASSERT_EQ(b->path(&root), "a/b");
    VE_ASSERT_EQ(c->path(&root), "a/b/c");
}

VE_TEST(node_path_duplicate) {
    Node root("root");
    root.append("item", new Node());
    Node* i1 = root.append("item", new Node());

    // duplicate names get index
    VE_ASSERT_EQ(i1->path(&root), "item#1");
}

VE_TEST(node_path_roundtrip) {
    Node root("root");
    Node* a = root.append("a", new Node());
    Node* b = a->append("b", new Node());
    Node* c = b->append("c", new Node());

    std::string p = c->path(&root);
    VE_ASSERT_EQ(root.resolve(p), c);
}

// ============================================================================
// isAncestorOf
// ============================================================================

VE_TEST(node_is_ancestor_of) {
    Node root("root");
    Node* a = root.append("a", new Node());
    Node* b = a->append("b", new Node());
    Node* c = b->append("c", new Node());

    VE_ASSERT(root.isAncestorOf(a));
    VE_ASSERT(root.isAncestorOf(b));
    VE_ASSERT(root.isAncestorOf(c));
    VE_ASSERT(a->isAncestorOf(b));
    VE_ASSERT(a->isAncestorOf(c));
    VE_ASSERT(b->isAncestorOf(c));

    VE_ASSERT(!c->isAncestorOf(a));
    VE_ASSERT(!b->isAncestorOf(a));
    VE_ASSERT(!a->isAncestorOf(&root));
    VE_ASSERT(!root.isAncestorOf(&root)); // not ancestor of self
    VE_ASSERT(!root.isAncestorOf(nullptr));
}

VE_TEST(node_is_ancestor_sibling) {
    Node root("root");
    Node* a = root.append("a", new Node());
    Node* b = root.append("b", new Node());

    VE_ASSERT(!a->isAncestorOf(b));
    VE_ASSERT(!b->isAncestorOf(a));
}

// ============================================================================
// ensure (create nodes along path)
// ============================================================================

VE_TEST(node_ensure_simple) {
    Node root("root");
    Node* c = root.ensure("a/b/c");

    VE_ASSERT(c != nullptr);
    VE_ASSERT_EQ(c->name(), "c");
    VE_ASSERT(root.child("a") != nullptr);
    VE_ASSERT(root.child("a")->child("b") != nullptr);
    VE_ASSERT_EQ(root.child("a")->child("b")->child("c"), c);
}

VE_TEST(node_ensure_existing) {
    Node root("root");
    Node* a = root.append("a", new Node());
    a->append("b", new Node());

    // ensure existing path returns the existing node
    Node* b = root.ensure("a/b");
    VE_ASSERT_EQ(b, a->child("b"));
}

VE_TEST(node_ensure_partial_existing) {
    Node root("root");
    root.append("a", new Node());

    // a exists, b does not → create b
    Node* b = root.ensure("a/b");
    VE_ASSERT(b != nullptr);
    VE_ASSERT_EQ(b->name(), "b");
    VE_ASSERT_EQ(b->parent(), root.child("a"));
}

VE_TEST(node_ensure_indexed) {
    Node root("root");

    // ensure item#2 → create 3 "item" children
    Node* i2 = root.ensure("item#2");
    VE_ASSERT(i2 != nullptr);
    VE_ASSERT_EQ(root.childCount("item"), 3);
    VE_ASSERT_EQ(root.child("item", 2), i2);
}

VE_TEST(node_ensure_global_index) {
    Node root("root");

    // ensure #3 → create 4 anonymous children
    Node* n3 = root.ensure("#3");
    VE_ASSERT(n3 != nullptr);
    VE_ASSERT_EQ(root.childCount(""), 4);
    VE_ASSERT_EQ(root.child("", 3), n3);
}

VE_TEST(node_ensure_nested_index) {
    Node root("root");

    // ensure items/item#1/value
    Node* val = root.ensure("items/item#1/value");
    VE_ASSERT(val != nullptr);
    VE_ASSERT_EQ(val->name(), "value");

    // items should have 2 "item" children
    Node* items = root.child("items");
    VE_ASSERT(items != nullptr);
    VE_ASSERT_EQ(items->childCount("item"), 2);
    VE_ASSERT_EQ(items->child("item", 1)->child("value"), val);
}

VE_TEST(node_ensure_empty_returns_self) {
    Node root("root");
    VE_ASSERT_EQ(root.ensure(""), &root);
}

// ============================================================================
// erase (remove node at path)
// ============================================================================

VE_TEST(node_erase_simple) {
    Node root("root");
    root.append("a", new Node());
    root.append("b", new Node());

    VE_ASSERT_EQ(root.childCount(), 2);
    VE_ASSERT(root.erase("a"));
    VE_ASSERT_EQ(root.childCount(), 1);
    VE_ASSERT(root.child("a") == nullptr);
    VE_ASSERT(root.child("b") != nullptr);
}

VE_TEST(node_erase_nested) {
    Node root("root");
    Node* a = root.append("a", new Node());
    a->append("b", new Node());
    a->append("c", new Node());

    VE_ASSERT(root.erase("a/b"));
    VE_ASSERT_EQ(a->childCount(), 1);
    VE_ASSERT(a->child("b") == nullptr);
    VE_ASSERT(a->child("c") != nullptr);
}

VE_TEST(node_erase_indexed) {
    Node root("root");
    root.append("item", new Node());
    root.append("item", new Node());
    root.append("item", new Node());

    VE_ASSERT(root.erase("item#1"));
    VE_ASSERT_EQ(root.childCount("item"), 2);
}

VE_TEST(node_erase_nonexistent) {
    Node root("root");
    VE_ASSERT(!root.erase("nope"));
    VE_ASSERT(!root.erase("a/b/c"));
}

VE_TEST(node_erase_root_fails) {
    Node root("root");
    // cannot erase self (no parent)
    VE_ASSERT(!root.erase(""));
}

VE_TEST(node_erase_no_delete) {
    Node root("root");
    Node* a = root.append("a", new Node());

    VE_ASSERT(root.erase("a", false));
    VE_ASSERT(root.child("a") == nullptr);
    VE_ASSERT(a->parent() == nullptr);

    // a is still alive, manual cleanup
    delete a;
}

// ============================================================================
// Mutex: exposed for external use
// ============================================================================

VE_TEST(node_mutex_exists) {
    Node n("test");
    auto& m = n.mutex();
    m.lock();
    m.unlock();
    VE_ASSERT(true);
}

// ============================================================================
// Stress tests
// ============================================================================

VE_TEST(node_10k_anonymous) {
    Node root("root");
    for (int i = 0; i < 10000; ++i)
        root.append("", new Node());

    VE_ASSERT_EQ(root.childCount(), 10000);
    VE_ASSERT(root.childAt(0) != nullptr);
    VE_ASSERT(root.childAt(5000) != nullptr);
    VE_ASSERT(root.childAt(9999) != nullptr);
    VE_ASSERT(root.childAt(10000) == nullptr);

    Node* mid = root.childAt(5000);
    VE_ASSERT(mid != nullptr);
    root.remove(mid);
    VE_ASSERT_EQ(root.childCount(), 9999);
}

VE_TEST(node_10k_named) {
    Node root("root");
    for (int i = 0; i < 10000; ++i)
        root.append("n" + std::to_string(i), new Node());

    VE_ASSERT_EQ(root.childCount(), 10000);
    VE_ASSERT(root.child("n0") != nullptr);
    VE_ASSERT(root.child("n9999") != nullptr);
    VE_ASSERT(root.child("n10000") == nullptr);

    root.remove("n5000");
    VE_ASSERT_EQ(root.childCount(), 9999);
    VE_ASSERT(root.child("n5000") == nullptr);
}

// ============================================================================
// Complex structure tests (with log output)
// ============================================================================

// Simulates a robot description tree (like URDF)
VE_TEST(node_complex_robot) {
    Node robot("robot");

    // base_link → joint1 → link1 → joint2 → link2
    auto* base = robot.append("base_link", new Node());
    auto* j1 = base->append("joint", new Node());
    auto* l1 = j1->append("link", new Node());
    auto* j2 = l1->append("joint", new Node());
    auto* l2 = j2->append("link", new Node());

    // each link has visual + collision + inertial
    for (auto* link : {base, l1, l2}) {
        link->append("visual", new Node());
        link->append("collision", new Node());
        link->append("inertial", new Node());
    }

    // each joint has axis + limit + dynamics
    for (auto* joint : {j1, j2}) {
        joint->append("axis", new Node());
        joint->append("limit", new Node());
        joint->append("dynamics", new Node());
    }

    veLogI << "=== Robot tree ===\n" << robot.dump();

    VE_ASSERT_EQ(robot.childCount(), 1); // base_link
    VE_ASSERT(robot.resolve("base_link/visual") != nullptr);
    VE_ASSERT(robot.resolve("base_link/joint/link/joint/link/visual") != nullptr);

    // path roundtrip
    auto p = l2->path(&robot);
    veLogI << "l2 path: " << p;
    VE_ASSERT_EQ(robot.resolve(p), l2);
}

// Simulates an XML-like config: repeated <item> children
VE_TEST(node_complex_xml_list) {
    Node config("config");

    auto* settings = config.append("settings", new Node());
    settings->append("theme", new Node());
    settings->append("lang", new Node());

    auto* items = config.append("items", new Node());
    for (int i = 0; i < 20; ++i) {
        auto* item = items->append("item", new Node());
        item->append("id", new Node());
        item->append("name", new Node());
        item->append("value", new Node());
    }

    veLogI << "=== XML-like config ===\n" << config.dump();

    VE_ASSERT_EQ(items->childCount("item"), 20);
    VE_ASSERT_EQ(items->child("item", 5)->childCount(), 3);

    // path with index
    auto* v10 = config.resolve("items/item#10/value");
    VE_ASSERT(v10 != nullptr);
    VE_ASSERT_EQ(v10->name(), "value");

    // ensure beyond existing
    auto* v25 = config.ensure("items/item#25/extra");
    VE_ASSERT(v25 != nullptr);
    VE_ASSERT_EQ(items->childCount("item"), 26); // 0..25

    veLogI << "item count after ensure: " << items->childCount("item");
}

// Deep tree: 100 levels deep
VE_TEST(node_complex_deep_tree) {
    Node root("root");
    Node* cur = &root;
    for (int i = 0; i < 100; ++i)
        cur = cur->append("level", new Node());

    // cur is now at depth 100
    VE_ASSERT_EQ(cur->name(), "level");

    // climb back
    VE_ASSERT(cur->parent(99) == &root);
    VE_ASSERT(cur->parent(100) == nullptr);

    // path
    auto p = cur->path(&root);
    VE_ASSERT_EQ(root.resolve(p), cur);

    // isAncestorOf
    VE_ASSERT(root.isAncestorOf(cur));
    VE_ASSERT(!cur->isAncestorOf(&root));

    veLogI << "deep tree path length: " << p.size() << " chars";
}

// Mixed anonymous + named + shadow
VE_TEST(node_complex_shadow_mixed) {
    // prototype: default sensor layout
    Node proto("proto");
    proto.append("camera", new Node());
    proto.append("lidar", new Node());
    proto.append("imu", new Node());

    // instance 1: override camera, inherit rest
    Node inst("inst");
    inst.setShadow(&proto);
    inst.append("camera", new Node()); // override
    inst.append("gps", new Node());    // local only

    // anonymous data list
    for (int i = 0; i < 5; ++i)
        inst.append("", new Node());

    veLogI << "=== Shadow mixed ===\n" << inst.dump();

    VE_ASSERT(inst.child("camera") != nullptr);
    VE_ASSERT(inst.child("camera") != proto.child("camera")); // overridden
    VE_ASSERT(inst.child("lidar") == proto.child("lidar"));   // from shadow
    VE_ASSERT(inst.child("imu") == proto.child("imu"));       // from shadow
    VE_ASSERT(inst.child("gps") != nullptr);                   // local
    VE_ASSERT_EQ(inst.childCount(""), 5);                      // anonymous
    VE_ASSERT(inst.child("nonexistent") == nullptr);
}

// Wide tree: 1000 named groups × 3 children each
VE_TEST(node_complex_wide_tree) {
    Node root("root");
    for (int g = 0; g < 1000; ++g) {
        std::string name = "g" + std::to_string(g);
        for (int c = 0; c < 3; ++c)
            root.append(name, new Node());
    }

    VE_ASSERT_EQ(root.childCount(), 3000);
    VE_ASSERT_EQ(root.childCount("g500"), 3);
    VE_ASSERT(root.child("g999", 2) != nullptr);
    VE_ASSERT(root.child("g999", 3) == nullptr);

    // resolve with index
    auto* target = root.resolve("g500#1");
    VE_ASSERT(target != nullptr);
    VE_ASSERT_EQ(target->name(), "g500");

    // erase middle of a group
    VE_ASSERT(root.erase("g500#1"));
    VE_ASSERT_EQ(root.childCount("g500"), 2);
    VE_ASSERT_EQ(root.childCount(), 2999);

    veLogI << "wide tree: " << root.childNames().sizeAsInt() << " distinct names";
}

// Reparent stress: move nodes between parents
VE_TEST(node_complex_reparent_stress) {
    Node p1("p1");
    Node p2("p2");

    // create 100 children under p1
    for (int i = 0; i < 100; ++i)
        p1.append("c" + std::to_string(i), new Node());

    VE_ASSERT_EQ(p1.childCount(), 100);

    // move all to p2
    while (p1.childCount() > 0) {
        auto* c = p1.first();
        p2.append(c->name(), c);
    }

    VE_ASSERT_EQ(p1.childCount(), 0);
    VE_ASSERT_EQ(p2.childCount(), 100);

    // verify all reparented
    for (int i = 0; i < 100; ++i) {
        auto* c = p2.child("c" + std::to_string(i));
        VE_ASSERT(c != nullptr);
        VE_ASSERT(c->parent() == &p2);
    }

    veLogI << "reparent stress: moved 100 nodes";
}

// Ensure + erase roundtrip
VE_TEST(node_complex_ensure_erase) {
    Node root("root");

    // build a deep path
    auto* leaf = root.ensure("a/b/c/d/e/f");
    VE_ASSERT(leaf != nullptr);
    VE_ASSERT_EQ(leaf->name(), "f");

    auto p = leaf->path(&root);
    VE_ASSERT_EQ(p, "a/b/c/d/e/f");

    // erase leaf
    VE_ASSERT(root.erase("a/b/c/d/e/f"));
    VE_ASSERT(root.resolve("a/b/c/d/e/f") == nullptr);
    VE_ASSERT(root.resolve("a/b/c/d/e") != nullptr); // parent still exists

    // re-ensure
    auto* leaf2 = root.ensure("a/b/c/d/e/f");
    VE_ASSERT(leaf2 != nullptr);
    VE_ASSERT_EQ(leaf2->name(), "f");
    VE_ASSERT_EQ(root.resolve("a/b/c/d/e/f"), leaf2);

    veLogI << "ensure/erase roundtrip OK";
}
