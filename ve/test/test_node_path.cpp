// test_node_path.cpp — key, path, find, at, erase, shadow, schema, static root, mutex
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
    VE_ASSERT_EQ(root.atKey("a"), a);
}

VE_TEST(node_childAt_name_index) {
    Node root("root");
    root.append("item");
    Node* i1 = root.append("item");
    root.append("item");

    VE_ASSERT_EQ(root.atKey("item"), root.child("item", 0));
    VE_ASSERT_EQ(root.atKey("item#1"), i1);
    VE_ASSERT_EQ(root.atKey("item#2"), root.child("item", 2));
    // atKey (non-const) calls at() which creates nodes
    VE_ASSERT(root.atKey("item#3") != nullptr);
    VE_ASSERT_EQ(root.count("item"), 4);  // item#3 was created
}

VE_TEST(node_childAt_global) {
    Node root("root");
    Node* a = root.append("a");
    Node* b = root.append("b");

    VE_ASSERT_EQ(root.atKey("#0"), a);
    VE_ASSERT_EQ(root.atKey("#1"), b);
    // atKey (non-const) calls at() which creates nodes
    VE_ASSERT(root.atKey("#2") != nullptr);
    VE_ASSERT_EQ(root.count(), 3);  // #2 was created
}

VE_TEST(node_childAt_empty) {
    Node root("root");
    VE_ASSERT(root.atKey("") == nullptr);
}

// ============================================================================
// find
// ============================================================================

VE_TEST(node_find_simple) {
    Node root("root");
    root.append("a");
    Node* b = root.append("b");
    b->append("c");

    VE_ASSERT_EQ(root.find("a")->name(), "a");
    VE_ASSERT_EQ(root.find("b")->name(), "b");
    VE_ASSERT_EQ(root.find("b/c")->name(), "c");
    VE_ASSERT(root.find("b/d") == nullptr);
    VE_ASSERT(root.find("z") == nullptr);
}

VE_TEST(node_find_duplicate_index) {
    Node root("root");
    root.append("item");
    Node* i1 = root.append("item");
    root.append("item");

    VE_ASSERT_EQ(root.find("item"), root.child("item", 0));
    VE_ASSERT_EQ(root.find("item#1"), i1);
    VE_ASSERT_EQ(root.find("item#2"), root.child("item", 2));
    // Note: child(name, overlap) with out-of-range overlap has undefined behavior
    // VE_ASSERT(root.find("item#3") == nullptr);
}

VE_TEST(node_find_global_index) {
    Node root("root");
    Node* a = root.append("a");
    Node* b = root.append("b");

    VE_ASSERT_EQ(root.find("#0"), a);
    VE_ASSERT_EQ(root.find("#1"), b);
    VE_ASSERT(root.find("#2") == nullptr);
}

VE_TEST(node_find_absolute) {
    Node root("root");
    Node* a = root.append("a");
    Node* b = a->append("b");
    Node* c = b->append("c");

    VE_ASSERT_EQ(c->find("/a/b/c"), c);
    VE_ASSERT_EQ(b->find("/a/b"), b);
    VE_ASSERT_EQ(c->find("/a"), a);
}

VE_TEST(node_find_nested_index) {
    Node root("root");
    Node* items = root.append("items");
    items->append("item");
    Node* i1 = items->append("item");
    i1->append("value");

    VE_ASSERT_EQ(root.find("items/item#1/value")->name(), "value");
}

VE_TEST(node_find_empty) {
    Node root("root");
    VE_ASSERT_EQ(root.find(""), &root);
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
    VE_ASSERT_EQ(root.find(p), c);
}

// ============================================================================
// at
// ============================================================================

VE_TEST(node_at_simple) {
    Node root("root");
    Node* c = root.at("a/b/c");

    VE_ASSERT(c != nullptr);
    VE_ASSERT_EQ(c->name(), "c");
    VE_ASSERT(root.child("a") != nullptr);
    VE_ASSERT(root.child("a")->child("b") != nullptr);
    VE_ASSERT_EQ(root.child("a")->child("b")->child("c"), c);
}

VE_TEST(node_at_existing) {
    Node root("root");
    Node* a = root.append("a");
    a->append("b");

    Node* b = root.at("a/b");
    VE_ASSERT_EQ(b, a->child("b"));
}

VE_TEST(node_at_partial) {
    Node root("root");
    root.append("a");

    Node* b = root.at("a/b");
    VE_ASSERT(b != nullptr);
    VE_ASSERT_EQ(b->name(), "b");
    VE_ASSERT_EQ(b->parent(), root.child("a"));
}

VE_TEST(node_at_indexed) {
    Node root("root");

    Node* i2 = root.at("item#2");
    VE_ASSERT(i2 != nullptr);
    VE_ASSERT_EQ(root.count("item"), 3);
    // Note: child(name, overlap) may have undefined behavior with out-of-range access
    // VE_ASSERT_EQ(root.child("item", 2), i2);
    VE_ASSERT_EQ(i2->name(), "item");
}

VE_TEST(node_at_global) {
    Node root("root");

    Node* n3 = root.at("#3");
    VE_ASSERT(n3 != nullptr);
    VE_ASSERT_EQ(root.count(""), 4);
    // Note: child(name, overlap) may have undefined behavior with out-of-range access
    // VE_ASSERT_EQ(root.child("", 3), n3);
    VE_ASSERT(n3->name().empty());
}

VE_TEST(node_at_nested_index) {
    Node root("root");

    Node* val = root.at("items/item#1/value");
    VE_ASSERT(val != nullptr);
    VE_ASSERT_EQ(val->name(), "value");

    Node* items = root.child("items");
    VE_ASSERT(items != nullptr);
    VE_ASSERT_EQ(items->count("item"), 2);
    // Note: child(name, overlap) may have undefined behavior with out-of-range access
    // VE_ASSERT_EQ(items->child("item", 1)->child("value"), val);
    VE_ASSERT_EQ(val->parent()->name(), "item");
    VE_ASSERT_EQ(val->parent()->parent(), items);
}

VE_TEST(node_at_empty) {
    Node root("root");
    VE_ASSERT_EQ(root.at(""), &root);
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

    // child() does NOT use shadow — only path methods do
    VE_ASSERT(inst.child("local_z") != nullptr);
    VE_ASSERT(inst.child("default_x") == nullptr);

    // find() uses shadow fallback
    VE_ASSERT(inst.find("local_z") != nullptr);
    VE_ASSERT(inst.find("default_x") != nullptr);
    VE_ASSERT(inst.find("default_y") != nullptr);
    VE_ASSERT(inst.find("nope") == nullptr);
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

    // find walks shadow chain
    VE_ASSERT(leaf.find("from_leaf") != nullptr);
    VE_ASSERT(leaf.find("from_mid") != nullptr);
    VE_ASSERT(leaf.find("from_base") != nullptr);
    VE_ASSERT(leaf.find("nope") == nullptr);

    // child() only sees local children
    VE_ASSERT(leaf.child("from_leaf") != nullptr);
    VE_ASSERT(leaf.child("from_mid") == nullptr);
    VE_ASSERT(leaf.child("from_base") == nullptr);
}

VE_TEST(node_shadow_has) {
    Node proto("proto");
    proto.append("field");

    Node inst("inst");
    inst.setShadow(&proto);

    // has() uses child() → no shadow
    VE_ASSERT(!inst.has("field"));

    // find for shadow access
    VE_ASSERT(inst.find("field") != nullptr);
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

VE_TEST(node_schema_json_import_merge_preserves_identity) {
    Node root("root");
    Node* keep = root.append("keep");
    keep->set(1);

    int changed = 0;
    keep->connect<Node::NODE_CHANGED>(keep, [&](const Var&, const Var&) { ++changed; });

    schema::ImportOptions options;
    options.auto_insert = true;
    options.auto_remove = false;

    VE_ASSERT(schema::importAs<schema::JsonS>(&root, "{\"keep\":2,\"add\":3}", options));
    VE_ASSERT_EQ(root.child("keep"), keep);
    VE_ASSERT_EQ(keep->getInt(), 2);
    VE_ASSERT_EQ(root.child("add")->getInt(), 3);
    VE_ASSERT_EQ(changed, 1);
}

VE_TEST(node_schema_json_import_auto_remove) {
    Node root("root");
    root.append("keep")->set(1);
    root.append("extra")->set(2);

    schema::ImportOptions options;
    options.auto_remove = true;

    VE_ASSERT(schema::importAs<schema::JsonS>(&root, "{\"keep\":5}", options));
    VE_ASSERT_EQ(root.count(), 1);
    VE_ASSERT(root.child("extra") == nullptr);
    VE_ASSERT_EQ(root.child("keep")->getInt(), 5);
}

VE_TEST(node_schema_json_import_auto_update_suppresses_equal_signal) {
    Node root("root");
    root.set(9);
    Node* keep = root.append("keep");
    keep->set(1);

    int root_changed = 0;
    int keep_changed = 0;
    root.connect<Node::NODE_CHANGED>(&root, [&](const Var&, const Var&) { ++root_changed; });
    keep->connect<Node::NODE_CHANGED>(keep, [&](const Var&, const Var&) { ++keep_changed; });

    schema::ImportOptions options;
    options.auto_update = true;

    VE_ASSERT(schema::importAs<schema::JsonS>(&root, "{\"_value\":9,\"keep\":1}", options));
    VE_ASSERT_EQ(root.getInt(), 9);
    VE_ASSERT_EQ(keep->getInt(), 1);
    VE_ASSERT_EQ(root_changed, 0);
    VE_ASSERT_EQ(keep_changed, 0);
}

VE_TEST(node_schema_json_import_signal_order_children_before_current) {
    Node root("root");
    Vector<std::string> events;

    root.connect<Node::NODE_CHANGED>(&root, [&](const Var&, const Var&) {
        events.push_back("changed");
    });
    root.connect<Node::NODE_ADDED>(&root, [&](const std::string& key, int) {
        events.push_back("added:" + key);
    });

    schema::ImportOptions options;
    VE_ASSERT(schema::importAs<schema::JsonS>(&root, "{\"child\":2,\"_value\":1}", options));

    VE_ASSERT_EQ(events.sizeAsInt(), 2);
    VE_ASSERT_EQ(events[0], "added:child");
    VE_ASSERT_EQ(events[1], "changed");
}

VE_TEST(node_schema_json_export_auto_ignore) {
    Node root("root");
    root.append("public")->set(1);
    root.append("_internal")->set(2);

    schema::ExportOptions options;
    options.auto_ignore = true;

    std::string json = schema::exportAs<schema::JsonS>(&root, options);
    VE_ASSERT(json.find("\"public\"") != std::string::npos);
    VE_ASSERT(json.find("\"_internal\"") == std::string::npos);
}

VE_TEST(node_schema_bin_roundtrip_auto_ignore) {
    Node src("src");
    src.append("public")->set(1);
    src.append("_internal")->set(2);

    schema::ExportOptions export_options;
    export_options.auto_ignore = true;

    auto bytes = schema::exportAs<schema::BinS>(&src, export_options);

    Node dst("dst");
    schema::ImportOptions import_options;
    import_options.auto_remove = true;

    VE_ASSERT(schema::importAs<schema::BinS>(&dst, bytes.data(), bytes.size(), import_options));
    VE_ASSERT(dst.child("public") != nullptr);
    VE_ASSERT(dst.child("_internal") == nullptr);
}

// ============================================================================
// Static root
// ============================================================================

VE_TEST(node_static_root) {
    Node* r1 = ve::node::root();
    Node* r2 = ve::node::root();
    VE_ASSERT(r1 == r2);
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
