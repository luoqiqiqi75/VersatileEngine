// test_node_serialize.cpp — Node copy vs schema Json/Bin import-export accuracy
#include "ve_test.h"
#include "ve/core/node.h"
#include "ve/core/var.h"
#include "ve/core/schema.h"
#include "ve/core/impl/xml.h"

using namespace ve;

namespace {

bool nodeStructEqual(const Node* a, const Node* b)
{
    if (!a || !b) {
        return a == b;
    }
    if (a->get() != b->get()) {
        return false;
    }
    if (a->count() != b->count()) {
        return false;
    }
    for (int i = 0; i < a->count(); ++i) {
        if (a->child(i)->name() != b->child(i)->name()) {
            return false;
        }
        if (!nodeStructEqual(a->child(i), b->child(i))) {
            return false;
        }
    }
    return true;
}

void buildJsonTree(Node& root)
{
    root.set(7);
    root.append("alpha")->set(1);
    root.append("beta")->append("nested")->set("x");
    root.append("")->set(99);
}

void buildFullTree(Node& root)
{
    buildJsonTree(root);
    root.append("dup")->set(10);
    root.append("dup")->set(20);
}

} // namespace

// ============================================================================
// Json roundtrip
// ============================================================================

VE_TEST(node_serialize_json_roundtrip_default) {
    Node src("r");
    buildJsonTree(src);

    schema::ExportOptions ex;
    ex.indent = 2;
    std::string json = schema::exportAs<schema::Json>(&src, ex);

    Node dst("r");
    schema::ImportOptions im;
    VE_ASSERT(schema::importAs<schema::Json>(&dst, json, im));
    VE_ASSERT(nodeStructEqual(&src, &dst));
}

VE_TEST(node_serialize_json_merge_preserves_extra_child) {
    Node src("r");
    src.set(1);
    src.append("a")->set(2);

    std::string json = schema::exportAs<schema::Json>(&src, schema::ExportOptions{});

    Node dst("r");
    dst.append("extra")->set(42);

    schema::ImportOptions im;
    im.auto_remove = false;
    VE_ASSERT(schema::importAs<schema::Json>(&dst, json, im));
    VE_ASSERT(dst.has("extra"));
    VE_ASSERT_EQ(dst.child("a")->getInt(), 2);
    VE_ASSERT_EQ(dst.getInt(), 1);
}

VE_TEST(node_serialize_json_ignores_duplicate_named_children) {
    Node src("r");
    src.append("a")->set(1);
    src.append("a")->set(2);
    src.append("b")->set(3);

    std::string json = schema::exportAs<schema::Json>(&src, schema::ExportOptions{});

    Node dst("r");
    VE_ASSERT(schema::importAs<schema::Json>(&dst, json, schema::ImportOptions{}));
    VE_ASSERT_EQ(dst.count("a"), 1);
    VE_ASSERT_EQ(dst.child("a")->getInt(), 1);
    VE_ASSERT_EQ(dst.child("b")->getInt(), 3);
}

VE_TEST(node_serialize_json_duplicate_key_last_wins) {
    Node dst("r");
    VE_ASSERT(schema::importAs<schema::Json>(&dst, "{\"a\":1,\"a\":2}", schema::ImportOptions{}));
    VE_ASSERT_EQ(dst.count("a"), 1);
    VE_ASSERT_EQ(dst.child("a")->getInt(), 2);
}

VE_TEST(node_serialize_json_auto_remove_prunes_extra_duplicates) {
    Node dst("r");
    dst.append("a")->set(1);
    dst.append("a")->set(2);
    dst.append("extra")->set(3);

    schema::ImportOptions im;
    im.auto_remove = true;
    VE_ASSERT(schema::importAs<schema::Json>(&dst, "{\"a\":5}", im));
    VE_ASSERT_EQ(dst.count("a"), 1);
    VE_ASSERT_EQ(dst.child("a")->getInt(), 5);
    VE_ASSERT(!dst.has("extra"));
}

// ============================================================================
// auto_ignore on export
// ============================================================================

VE_TEST(node_serialize_json_export_auto_ignore_roundtrip) {
    Node src("r");
    src.append("pub")->set(1);
    src.append("_hid")->set(2);

    schema::ExportOptions ex;
    ex.auto_ignore = true;
    std::string json = schema::exportAs<schema::Json>(&src, ex);

    Node dst("r");
    VE_ASSERT(schema::importAs<schema::Json>(&dst, json, schema::ImportOptions{}));
    VE_ASSERT(dst.has("pub"));
    VE_ASSERT(!dst.has("_hid"));
}

VE_TEST(node_serialize_json_export_no_ignore_includes_underscore_child) {
    Node src("r");
    src.append("pub")->set(1);
    src.append("_hid")->set(2);

    schema::ExportOptions ex;
    ex.auto_ignore = false;
    std::string json = schema::exportAs<schema::Json>(&src, ex);

    Node dst("r");
    VE_ASSERT(schema::importAs<schema::Json>(&dst, json, schema::ImportOptions{}));
    VE_ASSERT(dst.has("pub"));
    VE_ASSERT(dst.has("_hid"));
    VE_ASSERT_EQ(dst.child("_hid")->getInt(), 2);
}

// ============================================================================
// Bin roundtrip + Json vs Bin equivalence
// ============================================================================

VE_TEST(node_serialize_bin_roundtrip_default) {
    Node src("r");
    buildFullTree(src);

    auto bytes = schema::exportAs<schema::Bin>(&src, schema::ExportOptions{});

    Node dst("r");
    VE_ASSERT(schema::importAs<schema::Bin>(&dst, bytes.data(), bytes.size(), schema::ImportOptions{}));
    VE_ASSERT(nodeStructEqual(&src, &dst));
}

VE_TEST(node_serialize_json_bin_import_equivalent) {
    Node src("r");
    buildJsonTree(src);

    schema::ExportOptions ex;
    ex.auto_ignore = false;
    std::string json = schema::exportAs<schema::Json>(&src, ex);
    auto          bin  = schema::exportAs<schema::Bin>(&src, ex);

    Node fromJson("r");
    Node fromBin("r");
    schema::ImportOptions im;
    im.auto_insert  = true;
    im.auto_remove  = true;
    im.auto_update  = true;
    VE_ASSERT(schema::importAs<schema::Json>(&fromJson, json, im));
    VE_ASSERT(schema::importAs<schema::Bin>(&fromBin, bin.data(), bin.size(), im));
    VE_ASSERT(nodeStructEqual(&fromJson, &fromBin));
}

VE_TEST(node_serialize_json_bin_duplicate_named_children_differ) {
    Node src("r");
    buildFullTree(src);

    std::string json = schema::exportAs<schema::Json>(&src, schema::ExportOptions{});
    auto        bin  = schema::exportAs<schema::Bin>(&src, schema::ExportOptions{});

    Node fromJson("r");
    Node fromBin("r");
    VE_ASSERT(schema::importAs<schema::Json>(&fromJson, json, schema::ImportOptions{}));
    VE_ASSERT(schema::importAs<schema::Bin>(&fromBin, bin.data(), bin.size(), schema::ImportOptions{}));

    VE_ASSERT_EQ(fromJson.count("dup"), 1);
    VE_ASSERT_EQ(fromBin.count("dup"), 2);
    VE_ASSERT(!nodeStructEqual(&fromJson, &fromBin));
}

VE_TEST(node_serialize_bin_auto_ignore_matches_json) {
    Node src("r");
    src.append("a")->set(1);
    src.append("_b")->set(2);

    schema::ExportOptions ex;
    ex.auto_ignore = true;
    std::string json = schema::exportAs<schema::Json>(&src, ex);
    auto        bin  = schema::exportAs<schema::Bin>(&src, ex);

    schema::ImportOptions im;
    im.auto_remove = true;

    Node j("r");
    Node b("r");
    VE_ASSERT(schema::importAs<schema::Json>(&j, json, im));
    VE_ASSERT(schema::importAs<schema::Bin>(&b, bin.data(), bin.size(), im));
    VE_ASSERT(nodeStructEqual(&j, &b));
}

// ============================================================================
// copy vs export-import (serializable subtree)
// ============================================================================

VE_TEST(node_serialize_copy_matches_bin_import_full_tree) {
    Node src("r");
    buildFullTree(src);

    Node byCopy("r");
    byCopy.copy(&src, true, true, true);

    auto bytes = schema::exportAs<schema::Bin>(&src, schema::ExportOptions{});
    Node byBin("r");
    schema::ImportOptions im;
    im.auto_insert  = true;
    im.auto_remove  = true;
    im.auto_update  = true;
    VE_ASSERT(schema::importAs<schema::Bin>(&byBin, bytes.data(), bytes.size(), im));

    VE_ASSERT(nodeStructEqual(&byCopy, &byBin));
}

// ============================================================================
// Invalid input — merge import must not modify dst on failure
// ============================================================================

VE_TEST(node_serialize_json_invalid_preserves_dst) {
    Node dst("dst");
    dst.append("marker")->set(123);
    Node* marker = dst.child("marker");

    schema::ImportOptions im;
    VE_ASSERT(!schema::importAs<schema::Json>(&dst, "{ not json", im));
    VE_ASSERT(dst.has("marker"));
    VE_ASSERT_EQ(dst.child("marker"), marker);
    VE_ASSERT_EQ(marker->getInt(), 123);
}

VE_TEST(node_serialize_json_invalid_unclosed_preserves_dst) {
    Node dst("dst");
    dst.set(1);
    schema::ImportOptions im;
    VE_ASSERT(!schema::importAs<schema::Json>(&dst, "{\"a\":", im));
    VE_ASSERT_EQ(dst.count(), 0);
    VE_ASSERT_EQ(dst.getInt(), 1);
}

VE_TEST(node_serialize_bin_truncated_merge_preserves_dst) {
    Node src("s");
    src.append("x")->set(5);
    auto full = schema::exportAs<schema::Bin>(&src, schema::ExportOptions{});

    Node dst("dst");
    dst.append("marker")->set(7);
    if (full.size() < 4) {
        VE_ASSERT(true);
        return;
    }
    Bytes truncated(full.begin(), full.begin() + full.size() / 2);

    schema::ImportOptions im;
    VE_ASSERT(!schema::importAs<schema::Bin>(&dst, truncated.data(), truncated.size(), im));
    VE_ASSERT(dst.has("marker"));
    VE_ASSERT_EQ(dst.child("marker")->getInt(), 7);
}

// ============================================================================
// Repeated merge — stable identities where names match
// ============================================================================

VE_TEST(node_serialize_json_double_import_preserves_named_child_identity) {
    Node dst("r");
    Node* a = dst.append("a");
    a->set(0);

    std::string json = "{\"a\": 1}";

    schema::ImportOptions im;
    VE_ASSERT(schema::importAs<schema::Json>(&dst, json, im));
    VE_ASSERT_EQ(dst.child("a"), a);
    VE_ASSERT_EQ(a->getInt(), 1);

    VE_ASSERT(schema::importAs<schema::Json>(&dst, json, im));
    VE_ASSERT_EQ(dst.child("a"), a);
    VE_ASSERT_EQ(a->getInt(), 1);
}

// ============================================================================
// Signals — merge import with auto_remove
// ============================================================================

VE_TEST(node_serialize_json_import_signals_auto_remove) {
    Node root("r");
    root.append("stay")->set(1);
    root.append("gone")->set(2);

    Vector<std::string> events;

    root.connect<Node::NODE_REMOVED>(&root, [&](const std::string& key, int) {
        events.push_back("removed:" + key);
    });

    schema::ImportOptions im;
    im.auto_remove = true;
    VE_ASSERT(schema::importAs<schema::Json>(&root, "{\"stay\":3}", im));

    VE_ASSERT_EQ(root.count(), 1);
    VE_ASSERT_EQ(root.child("stay")->getInt(), 3);
    VE_ASSERT_EQ(events.sizeAsInt(), 1);
    VE_ASSERT_EQ(events[0], "removed:gone");
}

// ============================================================================
// XML support (pugixml + Dict attrs only NODE_CHANGED)
// ============================================================================

VE_TEST(node_serialize_xml_basic) {
    Node src("root");
    src.set(42);
    src.append("child")->set("value");
    auto xml = schema::exportAs<schema::Xml>(&src);
    VE_ASSERT(!xml.empty());

    Node dst("dst");
    VE_ASSERT(schema::importAs<schema::Xml>(&dst, xml));
    VE_ASSERT_EQ(dst.getInt(), 42);
    VE_ASSERT_EQ(dst.count(), 1);
}

// ============================================================================
// Var support
// ============================================================================

VE_TEST(node_serialize_var_roundtrip_default) {
    Node src("r");
    buildJsonTree(src);

    schema::ExportOptions ex;
    auto var = schema::exportAs<schema::Var>(&src, ex);

    Node dst("r");
    schema::ImportOptions im;
    VE_ASSERT(schema::importAs<schema::Var>(&dst, var, im));
    VE_ASSERT(nodeStructEqual(&src, &dst));
}
