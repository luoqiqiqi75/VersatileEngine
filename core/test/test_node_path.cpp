// test_node_path.cpp — key, path, resolve, ensure, erase, shadow, schema, static root, mutex
#include "ve_test.h"
#include "ve/core/node.h"

using namespace ve;

// ============================================================================
// keyOf
// ============================================================================

VE_TEST(node_keyOf_unique) {
    Node root("root");
    Node* a = root.append("a");
    VE_ASSERT_EQ(root.keyOf(a), "a");
}

VE_TEST(node_keyOf_duplicate) {
    Node root("root");
    root.append("item");
    Node* i1 = root.append("item");
    Node* i2 = root.append("item");

    VE_ASSERT_EQ(root.keyOf(root.child("item", 0)), "item#0");
    VE_ASSERT_EQ(root.keyOf(i1), "item#1");
    VE_ASSERT_EQ(root.keyOf(i2), "item#2");
}

VE_TEST(node_keyOf_anonymous) {
    Node root("root");
    Node* a0 = root.append("");
    Node* a1 = root.append("");

    // anonymous → "#" + global index
    VE_ASSERT_EQ(root.keyOf(a0), "#0");
    VE_ASSERT_EQ(root.keyOf(a1), "#1");
}

VE_TEST(node_keyOf_anon_with_named) {
    Node root("root");
    Node* a0 = root.append("");
    Node* x  = root.append("x");
    Node* a1 = root.append("");

    // true insertion order: a0=0, x=1, a1=2
    VE_ASSERT_EQ(root.keyOf(a0), "#0");
    VE_ASSERT_EQ(root.keyOf(a1), "#2");
    VE_ASSERT_EQ(root.keyOf(x), "x");
}

VE_TEST(node_keyOf_not_child) {
    Node root("root");
    Node other("other");
    VE_ASSERT_EQ(root.keyOf(&other), "");
}

// ============================================================================
// childAt (key parsing)
// ============================================================================

VE_TEST(node_childAt_name) {
    Node root("root");
    Node* a = root.append("a");
    VE_ASSERT_EQ(root.childAt("a"), a);
}

VE_TEST(node_childAt_name_index) {
    Node root("root");
    root.append("item");
    Node* i1 = root.append("item");
    root.append("item");

    VE_ASSERT_EQ(root.childAt("item"), root.child("item", 0));
    VE_ASSERT_EQ(root.childAt("item#1"), i1);
    VE_ASSERT_EQ(root.childAt("item#2"), root.child("item", 2));
    VE_ASSERT(root.childAt("item#3") == nullptr);
}

VE_TEST(node_childAt_global) {
    Node root("root");
    Node* a = root.append("a");
    Node* b = root.append("b");

    VE_ASSERT_EQ(root.childAt("#0"), a);
    VE_ASSERT_EQ(root.childAt("#1"), b);
    VE_ASSERT(root.childAt("#2") == nullptr);
}

VE_TEST(node_childAt_empty) {
    Node root("root");
    VE_ASSERT(root.childAt("") == nullptr);
}

// ============================================================================
// resolve
// ============================================================================

VE_TEST(node_resolve_simple) {
    Node root("root");
    root.append("a");
    Node* b = root.append("b");
    b->append("c");

    VE_ASSERT_EQ(root.resolve("a")->name(), "a");
    VE_ASSERT_EQ(root.resolve("b")->name(), "b");
    VE_ASSERT_EQ(root.resolve("b/c")->name(), "c");
    VE_ASSERT(root.resolve("b/d") == nullptr);
    VE_ASSERT(root.resolve("z") == nullptr);
}

VE_TEST(node_resolve_duplicate_index) {
    Node root("root");
    root.append("item");
    Node* i1 = root.append("item");
    root.append("item");

    VE_ASSERT_EQ(root.resolve("item"), root.child("item", 0));
    VE_ASSERT_EQ(root.resolve("item#1"), i1);
    VE_ASSERT_EQ(root.resolve("item#2"), root.child("item", 2));
    VE_ASSERT(root.resolve("item#3") == nullptr);
}

VE_TEST(node_resolve_global_index) {
    Node root("root");
    Node* a = root.append("a");
    Node* b = root.append("b");

    VE_ASSERT_EQ(root.resolve("#0"), a);
    VE_ASSERT_EQ(root.resolve("#1"), b);
    VE_ASSERT(root.resolve("#2") == nullptr);
}

VE_TEST(node_resolve_absolute) {
    Node root("root");
    Node* a = root.append("a");
    Node* b = a->append("b");
    Node* c = b->append("c");

    VE_ASSERT_EQ(c->resolve("/a/b/c"), c);
    VE_ASSERT_EQ(b->resolve("/a/b"), b);
    VE_ASSERT_EQ(c->resolve("/a"), a);
}

VE_TEST(node_resolve_nested_index) {
    Node root("root");
    Node* items = root.append("items");
    items->append("item");
    Node* i1 = items->append("item");
    i1->append("value");

    VE_ASSERT_EQ(root.resolve("items/item#1/value")->name(), "value");
}

VE_TEST(node_resolve_empty) {
    Node root("root");
    VE_ASSERT_EQ(root.resolve(""), &root);
}

// ============================================================================
// path
// ============================================================================

VE_TEST(node_path_simple) {
    Node root("root");
    Node* a = root.append("a");
    Node* b = a->append("b");
    Node* c = b->append("c");

    VE_ASSERT_EQ(a->path(&root), "a");
    VE_ASSERT_EQ(b->path(&root), "a/b");
    VE_ASSERT_EQ(c->path(&root), "a/b/c");
}

VE_TEST(node_path_duplicate) {
    Node root("root");
    root.append("item");
    Node* i1 = root.append("item");

    VE_ASSERT_EQ(i1->path(&root), "item#1");
}

VE_TEST(node_path_roundtrip) {
    Node root("root");
    Node* a = root.append("a");
    Node* b = a->append("b");
    Node* c = b->append("c");

    std::string p = c->path(&root);
    VE_ASSERT_EQ(root.resolve(p), c);
}

// ============================================================================
// ensure
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
    Node* a = root.append("a");
    a->append("b");

    Node* b = root.ensure("a/b");
    VE_ASSERT_EQ(b, a->child("b"));
}

VE_TEST(node_ensure_partial) {
    Node root("root");
    root.append("a");

    Node* b = root.ensure("a/b");
    VE_ASSERT(b != nullptr);
    VE_ASSERT_EQ(b->name(), "b");
    VE_ASSERT_EQ(b->parent(), root.child("a"));
}

VE_TEST(node_ensure_indexed) {
    Node root("root");

    Node* i2 = root.ensure("item#2");
    VE_ASSERT(i2 != nullptr);
    VE_ASSERT_EQ(root.count("item"), 3);
    VE_ASSERT_EQ(root.child("item", 2), i2);
}

VE_TEST(node_ensure_global) {
    Node root("root");

    Node* n3 = root.ensure("#3");
    VE_ASSERT(n3 != nullptr);
    VE_ASSERT_EQ(root.count(""), 4);
    VE_ASSERT_EQ(root.child("", 3), n3);
}

VE_TEST(node_ensure_nested_index) {
    Node root("root");

    Node* val = root.ensure("items/item#1/value");
    VE_ASSERT(val != nullptr);
    VE_ASSERT_EQ(val->name(), "value");

    Node* items = root.child("items");
    VE_ASSERT(items != nullptr);
    VE_ASSERT_EQ(items->count("item"), 2);
    VE_ASSERT_EQ(items->child("item", 1)->child("value"), val);
}

VE_TEST(node_ensure_empty) {
    Node root("root");
    VE_ASSERT_EQ(root.ensure(""), &root);
}

// ============================================================================
// erase
// ============================================================================

VE_TEST(node_erase_simple) {
    Node root("root");
    root.append("a");
    root.append("b");

    VE_ASSERT(root.erase("a"));
    VE_ASSERT_EQ(root.count(), 1);
    VE_ASSERT(root.child("a") == nullptr);
    VE_ASSERT(root.child("b") != nullptr);
}

VE_TEST(node_erase_nested) {
    Node root("root");
    Node* a = root.append("a");
    a->append("b");
    a->append("c");

    VE_ASSERT(root.erase("a/b"));
    VE_ASSERT_EQ(a->count(), 1);
    VE_ASSERT(a->child("b") == nullptr);
    VE_ASSERT(a->child("c") != nullptr);
}

VE_TEST(node_erase_indexed) {
    Node root("root");
    root.append("item");
    root.append("item");
    root.append("item");

    VE_ASSERT(root.erase("item#1"));
    VE_ASSERT_EQ(root.count("item"), 2);
}

VE_TEST(node_erase_nonexistent) {
    Node root("root");
    VE_ASSERT(!root.erase("nope"));
    VE_ASSERT(!root.erase("a/b/c"));
}

VE_TEST(node_erase_root_fails) {
    Node root("root");
    VE_ASSERT(!root.erase(""));
}

VE_TEST(node_erase_no_delete) {
    Node root("root");
    Node* a = root.append("a");

    VE_ASSERT(root.erase("a", false));
    VE_ASSERT(root.child("a") == nullptr);
    VE_ASSERT(a->parent() == nullptr);

    delete a;
}

// ============================================================================
// Shadow (prototype chain)
// ============================================================================

VE_TEST(node_shadow_fallback) {
    Node proto("proto");
    proto.append("default_x");
    proto.append("default_y");

    Node inst("inst");
    inst.append("local_z");
    inst.setShadow(&proto);

    VE_ASSERT(inst.child("local_z") != nullptr);
    VE_ASSERT(inst.child("default_x") != nullptr);
    VE_ASSERT(inst.child("default_y") != nullptr);
    VE_ASSERT(inst.child("nope") == nullptr);
}

VE_TEST(node_shadow_chain) {
    Node base("base");
    base.append("from_base");

    Node mid("mid");
    mid.append("from_mid");
    mid.setShadow(&base);

    Node leaf("leaf");
    leaf.append("from_leaf");
    leaf.setShadow(&mid);

    VE_ASSERT(leaf.child("from_leaf") != nullptr);
    VE_ASSERT(leaf.child("from_mid") != nullptr);
    VE_ASSERT(leaf.child("from_base") != nullptr);
    VE_ASSERT(leaf.child("nope") == nullptr);
}

VE_TEST(node_shadow_has) {
    Node proto("proto");
    proto.append("field");

    Node inst("inst");
    inst.setShadow(&proto);

    VE_ASSERT(inst.has("field"));
    VE_ASSERT(!inst.has("other"));
}

// ============================================================================
// Schema (builds only fields with sub-schemas)
// ============================================================================

VE_TEST(node_schema_nested) {
    auto inner = Schema::create({SchemaField("a"), SchemaField("b")});
    auto outer = Schema::create({
        SchemaField("sub", inner),
    });

    Node root("root");
    outer->build(&root);

    VE_ASSERT_EQ(root.count(), 1);
    Node* sub = root.child("sub");
    VE_ASSERT(sub != nullptr);
    // inner has no sub-schemas, so a and b are NOT created
    VE_ASSERT_EQ(sub->count(), 0);
}

VE_TEST(node_schema_deep) {
    auto leaf = Schema::create({SchemaField("leaf")});
    auto mid  = Schema::create({SchemaField("inner", leaf)});
    auto top  = Schema::create({SchemaField("outer", mid)});

    Node root("root");
    top->build(&root);

    VE_ASSERT(root.child("outer") != nullptr);
    VE_ASSERT(root.child("outer")->child("inner") != nullptr);
    // "leaf" field has no sub → not created
    VE_ASSERT(root.child("outer")->child("inner")->count() == 0);
}

VE_TEST(node_schema_shared) {
    auto leaf = Schema::create({SchemaField("x")});
    auto schema = Schema::create({SchemaField("sub", leaf)});

    Node n1("n1");
    schema->build(&n1);
    Node n2("n2");
    schema->build(&n2);

    VE_ASSERT_EQ(n1.count(), 1);
    VE_ASSERT_EQ(n2.count(), 1);
    VE_ASSERT(n1.child("sub") != n2.child("sub"));
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
// Mutex
// ============================================================================

VE_TEST(node_mutex) {
    Node n("test");
    auto& m = n.mutex();
    m.lock();
    m.unlock();
    VE_ASSERT(true);
}
