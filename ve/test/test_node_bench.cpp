// test_node_bench.cpp — stress tests, complex structures, benchmarks
//
// Optional large JSON roundtrip: configure with -DVE_NODE_BENCH_LARGE=ON (see ve/test/CMakeLists.txt).
// ASan: configure with -DCMAKE_CXX_FLAGS="-fsanitize=address" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address".
// Fuzzing importTree/simdjson is not part of this target; use a dedicated harness if needed.
//
#include "ve_test.h"
#include "ve/core/node.h"
#include "ve/core/var.h"
#include "ve/core/convert.h"
#include "ve/core/log.h"
#include "ve/core/schema.h"
#include <chrono>

#ifndef VE_NODE_BENCH_LARGE
#define VE_NODE_BENCH_LARGE 0
#endif

using namespace ve;

// ============================================================================
// Helpers
// ============================================================================

static double elapsed_ms(std::chrono::steady_clock::time_point t0)
{
    auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

#define BENCH_BEGIN auto _t0 = std::chrono::steady_clock::now()
#define BENCH_END(label) veLogI << "[bench] " << (label) << ": " << elapsed_ms(_t0) << " ms"

// ============================================================================
// Stress tests
// ============================================================================

VE_TEST(node_stress_10k_anon) {
    Node root("root");
    for (int i = 0; i < 10000; ++i) root.append("");

    VE_ASSERT_EQ(root.count(), 10000);
    VE_ASSERT(root.child(0) != nullptr);
    VE_ASSERT(root.child(5000) != nullptr);
    VE_ASSERT(root.child(9999) != nullptr);
    VE_ASSERT(root.child(10000) == nullptr);

    auto* mid = root.child(5000);
    root.remove(mid);
    VE_ASSERT_EQ(root.count(), 9999);
}

VE_TEST(node_stress_10k_named) {
    Node root("root");
    for (int i = 0; i < 10000; ++i)
        root.append("n" + std::to_string(i));

    VE_ASSERT_EQ(root.count(), 10000);
    VE_ASSERT(root.child("n0") != nullptr);
    VE_ASSERT(root.child("n9999") != nullptr);
    VE_ASSERT(root.child("n10000") == nullptr);

    root.remove("n5000");
    VE_ASSERT_EQ(root.count(), 9999);
    VE_ASSERT(root.child("n5000") == nullptr);
}

VE_TEST(node_stress_reparent) {
    Node p1("p1");
    Node p2("p2");

    for (int i = 0; i < 100; ++i)
        p1.append("c" + std::to_string(i));

    VE_ASSERT_EQ(p1.count(), 100);

    while (p1.count() > 0) {
        auto* c = p1.first();
        p2.insert(c);
    }

    VE_ASSERT_EQ(p1.count(), 0);
    VE_ASSERT_EQ(p2.count(), 100);

    for (int i = 0; i < 100; ++i) {
        auto* c = p2.child("c" + std::to_string(i));
        VE_ASSERT(c != nullptr);
        VE_ASSERT(c->parent() == &p2);
    }
}

// ============================================================================
// Complex structure tests
// ============================================================================

VE_TEST(node_complex_robot) {
    Node robot("robot");

    auto* base = robot.append("base_link");
    auto* j1 = base->append("joint");
    auto* l1 = j1->append("link");
    auto* j2 = l1->append("joint");
    auto* l2 = j2->append("link");

    for (auto* link : {base, l1, l2}) {
        link->append("visual");
        link->append("collision");
        link->append("inertial");
    }
    for (auto* joint : {j1, j2}) {
        joint->append("axis");
        joint->append("limit");
        joint->append("dynamics");
    }

    veLogI << "=== Robot tree ===\n" << robot.dump();

    VE_ASSERT_EQ(robot.count(), 1);
    VE_ASSERT(robot.find("base_link/visual") != nullptr);
    VE_ASSERT(robot.find("base_link/joint/link/joint/link/visual") != nullptr);

    auto p = l2->path(&robot);
    veLogI << "l2 path: " << p;
    VE_ASSERT_EQ(robot.find(p), l2);
}

VE_TEST(node_complex_xml_list) {
    Node config("config");

    auto* settings = config.append("settings");
    settings->append("theme");
    settings->append("lang");

    auto* items = config.append("items");
    for (int i = 0; i < 20; ++i) {
        auto* item = items->append("item");
        item->append("id");
        item->append("name");
        item->append("value");
    }

    veLogI << "=== XML-like config ===\n" << config.dump();

    VE_ASSERT_EQ(items->count("item"), 20);
    VE_ASSERT_EQ(items->child("item", 5)->count(), 3);

    auto* v10 = config.find("items/item#10/value");
    VE_ASSERT(v10 != nullptr);
    VE_ASSERT_EQ(v10->name(), "value");

    auto* v25 = config.at("items/item#25/extra");
    VE_ASSERT(v25 != nullptr);
    // After at("items/item#25/extra"), item#20-25 should be created
    VE_ASSERT(items->count("item") >= 20);  // at least original 20 items

    veLogI << "item count after ensure: " << items->count("item");
}

VE_TEST(node_complex_deep_tree) {
    Node root("root");
    Node* cur = &root;
    for (int i = 0; i < 100; ++i)
        cur = cur->append("level");

    VE_ASSERT_EQ(cur->name(), "level");
    VE_ASSERT(cur->parent(99) == &root);
    VE_ASSERT(cur->parent(100) == nullptr);

    auto p = cur->path(&root);
    VE_ASSERT_EQ(root.find(p), cur);
    VE_ASSERT(root.isAncestorOf(cur));
    VE_ASSERT(!cur->isAncestorOf(&root));

    veLogI << "deep tree path length: " << p.size() << " chars";
}

VE_TEST(node_complex_shadow_mixed) {
    Node proto("proto");
    proto.append("camera");
    proto.append("lidar");
    proto.append("imu");

    Node inst("inst");
    inst.setShadow(&proto);
    inst.append("camera");  // override
    inst.append("gps");

    for (int i = 0; i < 5; ++i) inst.append("");

    veLogI << "=== Shadow mixed ===\n" << inst.dump();

    // child() only sees local children
    VE_ASSERT(inst.child("camera") != nullptr);
    VE_ASSERT(inst.child("camera") != proto.child("camera"));
    VE_ASSERT(inst.child("gps") != nullptr);
    VE_ASSERT(inst.child("lidar") == nullptr);   // not local

    // find() uses shadow fallback
    VE_ASSERT(inst.find("lidar") == proto.child("lidar"));
    VE_ASSERT(inst.find("imu") == proto.child("imu"));
    VE_ASSERT(inst.find("nonexistent") == nullptr);

    // count("") = count() = total local children (camera + gps + 5 anon = 7)
    VE_ASSERT_EQ(inst.count(), 7);
}

VE_TEST(node_complex_wide_tree) {
    Node root("root");
    for (int g = 0; g < 1000; ++g) {
        std::string nm = "g" + std::to_string(g);
        for (int c = 0; c < 3; ++c) root.append(nm);
    }

    VE_ASSERT_EQ(root.count(), 3000);
    VE_ASSERT_EQ(root.count("g500"), 3);
    VE_ASSERT(root.child("g999", 2) != nullptr);
    // Note: child(name, overlap) with out-of-range overlap has undefined behavior in Release mode
    VE_ASSERT(root.child("g999", 3) == nullptr);

    auto* target = root.find("g500#1");
    VE_ASSERT(target != nullptr);
    VE_ASSERT_EQ(target->name(), "g500");

    VE_ASSERT(root.erase("g500#1"));
    VE_ASSERT_EQ(root.count("g500"), 2);
    VE_ASSERT_EQ(root.count(), 2999);

    veLogI << "wide tree: " << root.childNames().sizeAsInt() << " distinct names";
}

VE_TEST(node_complex_ensure_erase) {
    Node root("root");

    auto* leaf = root.at("a/b/c/d/e/f");
    VE_ASSERT(leaf != nullptr);
    VE_ASSERT_EQ(leaf->name(), "f");
    VE_ASSERT_EQ(leaf->path(&root), "a/b/c/d/e/f");

    VE_ASSERT(root.erase("a/b/c/d/e/f"));
    VE_ASSERT(root.find("a/b/c/d/e/f") == nullptr);
    VE_ASSERT(root.find("a/b/c/d/e") != nullptr);

    auto* leaf2 = root.at("a/b/c/d/e/f");
    VE_ASSERT(leaf2 != nullptr);
    VE_ASSERT_EQ(root.find("a/b/c/d/e/f"), leaf2);

    veLogI << "ensure/erase roundtrip OK";
}

// ============================================================================
// Benchmarks — Small (baseline, same as before)
// ============================================================================

VE_TEST(node_bench_insert_100k_named) {
    Node root("root");
    BENCH_BEGIN;
    for (int i = 0; i < 100000; ++i)
        root.append("n" + std::to_string(i));
    BENCH_END("insert 100k named");
    VE_ASSERT_EQ(root.count(), 100000);
}

VE_TEST(node_bench_insert_100k_anon) {
    Node root("root");
    BENCH_BEGIN;
    for (int i = 0; i < 100000; ++i)
        root.append("");
    BENCH_END("insert 100k anon");
    VE_ASSERT_EQ(root.count(), 100000);
}

// ============================================================================
// Benchmarks — Large scale (500k / 1M)
// ============================================================================

VE_TEST(node_bench_insert_500k_named) {
    Node root("root");
    BENCH_BEGIN;
    for (int i = 0; i < 500000; ++i)
        root.append("n" + std::to_string(i));
    BENCH_END("insert 500k named");
    VE_ASSERT_EQ(root.count(), 500000);
}

VE_TEST(node_bench_insert_500k_anon) {
    Node root("root");
    BENCH_BEGIN;
    for (int i = 0; i < 500000; ++i)
        root.append("");
    BENCH_END("insert 500k anon");
    VE_ASSERT_EQ(root.count(), 500000);
}

VE_TEST(node_bench_insert_1m_named) {
    Node root("root");
    BENCH_BEGIN;
    for (int i = 0; i < 1000000; ++i)
        root.append("n" + std::to_string(i));
    BENCH_END("insert 1M named");
    VE_ASSERT_EQ(root.count(), 1000000);
}

VE_TEST(node_bench_insert_1m_anon) {
    Node root("root");
    BENCH_BEGIN;
    for (int i = 0; i < 1000000; ++i)
        root.append("");
    BENCH_END("insert 1M anon");
    VE_ASSERT_EQ(root.count(), 1000000);
}

// ============================================================================
// Benchmarks — Batch insert comparison
// ============================================================================

VE_TEST(node_bench_batch_insert_100k_named) {
    Node root("root");
    Node::Nodes batch;
    batch.reserve(100000);
    for (int i = 0; i < 100000; ++i)
        batch.push_back(new Node("n" + std::to_string(i)));
    BENCH_BEGIN;
    root.insert(batch);
    BENCH_END("batch insert 100k named");
    VE_ASSERT_EQ(root.count(), 100000);
}

VE_TEST(node_bench_batch_insert_100k_anon) {
    Node root("root");
    Node::Nodes batch;
    batch.reserve(100000);
    for (int i = 0; i < 100000; ++i)
        batch.push_back(new Node(""));
    BENCH_BEGIN;
    root.insert(batch);
    BENCH_END("batch insert 100k anon");
    VE_ASSERT_EQ(root.count(), 100000);
}

VE_TEST(node_bench_batch_insert_500k_named) {
    Node root("root");
    Node::Nodes batch;
    batch.reserve(500000);
    for (int i = 0; i < 500000; ++i)
        batch.push_back(new Node("n" + std::to_string(i)));
    BENCH_BEGIN;
    root.insert(batch);
    BENCH_END("batch insert 500k named");
    VE_ASSERT_EQ(root.count(), 500000);
}

VE_TEST(node_bench_batch_insert_500k_anon) {
    Node root("root");
    Node::Nodes batch;
    batch.reserve(500000);
    for (int i = 0; i < 500000; ++i)
        batch.push_back(new Node(""));
    BENCH_BEGIN;
    root.insert(batch);
    BENCH_END("batch insert 500k anon");
    VE_ASSERT_EQ(root.count(), 500000);
}

VE_TEST(node_bench_batch_insert_1m_named) {
    Node root("root");
    Node::Nodes batch;
    batch.reserve(1000000);
    for (int i = 0; i < 1000000; ++i)
        batch.push_back(new Node("n" + std::to_string(i)));
    BENCH_BEGIN;
    root.insert(batch);
    BENCH_END("batch insert 1M named");
    VE_ASSERT_EQ(root.count(), 1000000);
}

VE_TEST(node_bench_batch_insert_1m_anon) {
    Node root("root");
    Node::Nodes batch;
    batch.reserve(1000000);
    for (int i = 0; i < 1000000; ++i)
        batch.push_back(new Node(""));
    BENCH_BEGIN;
    root.insert(batch);
    BENCH_END("batch insert 1M anon");
    VE_ASSERT_EQ(root.count(), 1000000);
}

// ============================================================================
// Benchmarks — Lookup
// ============================================================================

VE_TEST(node_bench_lookup_name) {
    Node root("root");
    for (int i = 0; i < 10000; ++i)
        root.append("n" + std::to_string(i));

    BENCH_BEGIN;
    for (int rep = 0; rep < 10; ++rep)
        for (int i = 0; i < 10000; ++i)
            (void)root.child("n" + std::to_string(i));
    BENCH_END("lookup 10k names x10");
}

VE_TEST(node_bench_lookup_name_100k) {
    Node root("root");
    for (int i = 0; i < 100000; ++i)
        root.append("n" + std::to_string(i));

    BENCH_BEGIN;
    for (int i = 0; i < 100000; ++i)
        (void)root.child("n" + std::to_string(i));
    BENCH_END("lookup 100k names x1");
}

VE_TEST(node_bench_lookup_global) {
    Node root("root");
    for (int i = 0; i < 10000; ++i) root.append("");

    BENCH_BEGIN;
    for (int rep = 0; rep < 100; ++rep)
        for (int i = 0; i < 10000; ++i)
            (void)root.child(i);
    BENCH_END("lookup 10k global x100");
}

VE_TEST(node_bench_lookup_global_100k) {
    Node root("root");
    for (int i = 0; i < 100000; ++i) root.append("");

    BENCH_BEGIN;
    for (int i = 0; i < 100000; ++i)
        (void)root.child(i);
    BENCH_END("lookup 100k global x1");
}

VE_TEST(node_bench_resolve_path) {
    Node root("root");
    Node* cur = &root;
    for (int i = 0; i < 10; ++i)
        cur = cur->append("l" + std::to_string(i));

    std::string p = cur->path(&root);  // "l0/l1/l2/.../l9"

    BENCH_BEGIN;
    for (int i = 0; i < 100000; ++i)
        (void)root.find(p);
    BENCH_END("find 10-deep path x100k");

    VE_ASSERT_EQ(root.find(p), cur);
}

VE_TEST(node_bench_resolve_path_deep) {
    Node root("root");
    Node* cur = &root;
    for (int i = 0; i < 50; ++i)
        cur = cur->append("l" + std::to_string(i));

    std::string p = cur->path(&root);  // "l0/l1/.../l49"

    BENCH_BEGIN;
    for (int i = 0; i < 100000; ++i)
        (void)root.find(p);
    BENCH_END("find 50-deep path x100k");

    VE_ASSERT_EQ(root.find(p), cur);
}

VE_TEST(node_bench_indexOf_global) {
    Node root("root");
    Vector<Node*> nodes;
    for (int i = 0; i < 1000; ++i)
        nodes.push_back(root.append("n" + std::to_string(i)));

    BENCH_BEGIN;
    for (int rep = 0; rep < 100; ++rep)
        for (auto* n : nodes)
            (void)root.indexOf(n);
    BENCH_END("indexOf<global> 1k nodes x100");
}

VE_TEST(node_bench_indexOf_local) {
    Node root("root");
    Vector<Node*> nodes;
    for (int i = 0; i < 1000; ++i)
        nodes.push_back(root.append("item"));  // all same name

    BENCH_BEGIN;
    for (int rep = 0; rep < 100; ++rep)
        for (auto* n : nodes)
            (void)root.indexOf(n);
    BENCH_END("indexOf 1k same-name x100");
}

// ============================================================================
// Benchmarks — Traversal
// ============================================================================

// --- Scenario: JSON dict (10k unique names) — traverse ---
VE_TEST(node_bench_next_dict) {
    Node root("root");
    for (int i = 0; i < 10000; ++i)
        root.append("k" + std::to_string(i));

    BENCH_BEGIN;
    int cnt = 0;
    for (auto* n = root.first(); n; n = n->next()) ++cnt;
    BENCH_END("next() dict 10k unique");

    VE_ASSERT_EQ(cnt, 10000);
}

VE_TEST(node_bench_iter_dict) {
    Node root("root");
    for (int i = 0; i < 10000; ++i)
        root.append("k" + std::to_string(i));

    BENCH_BEGIN;
    int cnt = 0;
    for (auto* n : root) { (void)n; ++cnt; }
    BENCH_END("iterator dict 10k unique");

    VE_ASSERT_EQ(cnt, 10000);
}

VE_TEST(node_bench_riter_dict) {
    Node root("root");
    for (int i = 0; i < 10000; ++i)
        root.append("k" + std::to_string(i));

    BENCH_BEGIN;
    int cnt = 0;
    for (auto it = root.rbegin(); it != root.rend(); ++it) { (void)*it; ++cnt; }
    BENCH_END("reverse-iter dict 10k unique");

    VE_ASSERT_EQ(cnt, 10000);
}

VE_TEST(node_bench_iter_100k) {
    Node root("root");
    for (int i = 0; i < 100000; ++i)
        root.append("k" + std::to_string(i));

    BENCH_BEGIN;
    int cnt = 0;
    for (auto* n : root) { (void)n; ++cnt; }
    BENCH_END("iterator 100k named");

    VE_ASSERT_EQ(cnt, 100000);
}

VE_TEST(node_bench_iter_500k) {
    Node root("root");
    for (int i = 0; i < 500000; ++i) root.append("");

    BENCH_BEGIN;
    int cnt = 0;
    for (auto* n : root) { (void)n; ++cnt; }
    BENCH_END("iterator 500k anon");

    VE_ASSERT_EQ(cnt, 500000);
}

// --- Scenario: XML (100 groups x 10 items) — traverse ---
VE_TEST(node_bench_next_xml) {
    Node root("root");
    for (int g = 0; g < 100; ++g)
        for (int i = 0; i < 10; ++i)
            root.append("tag" + std::to_string(g));

    BENCH_BEGIN;
    int cnt = 0;
    for (auto* n = root.first(); n; n = n->next()) ++cnt;
    BENCH_END("next() xml 100x10");

    VE_ASSERT_EQ(cnt, 1000);
}

VE_TEST(node_bench_iter_xml) {
    Node root("root");
    for (int g = 0; g < 100; ++g)
        for (int i = 0; i < 10; ++i)
            root.append("tag" + std::to_string(g));

    BENCH_BEGIN;
    int cnt = 0;
    for (auto* n : root) { (void)n; ++cnt; }
    BENCH_END("iterator xml 100x10");

    VE_ASSERT_EQ(cnt, 1000);
}

// --- Scenario: JSON list (10k anon) — traverse ---
VE_TEST(node_bench_list_sequential) {
    Node root("root");
    for (int i = 0; i < 10000; ++i) root.append("");

    BENCH_BEGIN;
    for (int rep = 0; rep < 10; ++rep)
        for (int i = 0; i < 10000; ++i)
            (void)root.child(i);
    BENCH_END("child(idx) list 10k x10");
}

VE_TEST(node_bench_iter_list) {
    Node root("root");
    for (int i = 0; i < 10000; ++i) root.append("");

    BENCH_BEGIN;
    int cnt = 0;
    for (auto* n : root) { (void)n; ++cnt; }
    BENCH_END("iterator list 10k anon");

    VE_ASSERT_EQ(cnt, 10000);
}

// ============================================================================
// Benchmarks — Structural operations
// ============================================================================

VE_TEST(node_bench_ensure_deep) {
    Node root("root");

    BENCH_BEGIN;
    for (int i = 0; i < 1000; ++i) {
        std::string p = "a/b/c/d/e/f" + std::to_string(i);
        root.at(p);
    }
    BENCH_END("ensure 1k deep paths");

    // a/b/c/d/e should exist, with 1000 different f* children
    auto* e = root.find("a/b/c/d/e");
    VE_ASSERT(e != nullptr);
    VE_ASSERT_EQ(e->count(), 1000);
}

VE_TEST(node_bench_ensure_10k_deep) {
    Node root("root");

    BENCH_BEGIN;
    for (int i = 0; i < 10000; ++i) {
        std::string p = "a/b/c/d/e/f" + std::to_string(i);
        root.at(p);
    }
    BENCH_END("ensure 10k deep paths");

    auto* e = root.find("a/b/c/d/e");
    VE_ASSERT(e != nullptr);
    VE_ASSERT_EQ(e->count(), 10000);
}

VE_TEST(node_bench_clear_100k) {
    Node root("root");
    for (int i = 0; i < 100000; ++i) root.append("");

    BENCH_BEGIN;
    root.clear();
    BENCH_END("clear 100k");

    VE_ASSERT_EQ(root.count(), 0);
}

VE_TEST(node_bench_clear_500k) {
    Node root("root");
    for (int i = 0; i < 500000; ++i) root.append("");

    BENCH_BEGIN;
    root.clear();
    BENCH_END("clear 500k");

    VE_ASSERT_EQ(root.count(), 0);
}

VE_TEST(node_bench_clear_1m) {
    Node root("root");
    for (int i = 0; i < 1000000; ++i) root.append("");

    BENCH_BEGIN;
    root.clear();
    BENCH_END("clear 1M");

    VE_ASSERT_EQ(root.count(), 0);
}

VE_TEST(node_bench_path_build) {
    Node root("root");
    Node* cur = &root;
    for (int i = 0; i < 50; ++i)
        cur = cur->append("level");

    BENCH_BEGIN;
    for (int i = 0; i < 10000; ++i)
        (void)cur->path(&root);
    BENCH_END("path() 50-deep x10k");
}

VE_TEST(node_bench_path_build_100) {
    Node root("root");
    Node* cur = &root;
    for (int i = 0; i < 100; ++i)
        cur = cur->append("level");

    BENCH_BEGIN;
    for (int i = 0; i < 10000; ++i)
        (void)cur->path(&root);
    BENCH_END("path() 100-deep x10k");
}

VE_TEST(node_bench_keyOf) {
    Node root("root");
    Vector<Node*> nodes;
    for (int i = 0; i < 100; ++i) {
        nodes.push_back(root.append("item"));
    }

    BENCH_BEGIN;
    for (int rep = 0; rep < 10000; ++rep)
        for (auto* n : nodes)
            (void)root.keyOf(n);
    BENCH_END("keyOf 100 dups x10k");
}

VE_TEST(node_bench_childAt_key) {
    Node root("root");
    for (int i = 0; i < 100; ++i) root.append("item");

    BENCH_BEGIN;
    for (int rep = 0; rep < 10000; ++rep) {
        for (int i = 0; i < 100; ++i)
            (void)root.atKey("item#" + std::to_string(i));
    }
    BENCH_END("childAt(key) 100 dups x10k");
}

// ============================================================================
// Benchmarks — Remove / Take
// ============================================================================

VE_TEST(node_bench_remove_100k_from_back) {
    Node root("root");
    for (int i = 0; i < 100000; ++i) root.append("");

    BENCH_BEGIN;
    while (root.count() > 0) root.remove(root.last());
    BENCH_END("remove 100k from back (one by one)");

    VE_ASSERT_EQ(root.count(), 0);
}

VE_TEST(node_bench_remove_100k_from_front) {
    Node root("root");
    for (int i = 0; i < 100000; ++i) root.append("");

    BENCH_BEGIN;
    while (root.count() > 0) root.remove(root.first());
    BENCH_END("remove 100k from front (one by one)");

    VE_ASSERT_EQ(root.count(), 0);
}

// ============================================================================
// Benchmarks — Full lifecycle (create + build + destroy)
// ============================================================================

VE_TEST(node_bench_lifecycle_deep_tree) {
    // Build a deep tree: 10 levels, 10 children per level → ~10^10... too much
    // Use: 5 levels x 10 children = 100,000 leaves (actually 10^5 = 100k)
    BENCH_BEGIN;
    for (int rep = 0; rep < 10; ++rep) {
        Node root("root");
        // 4 levels, 10 children each → ~10000 nodes
        std::function<void(Node*, int)> build = [&](Node* parent, int depth) {
            if (depth == 0) return;
            for (int i = 0; i < 10; ++i) {
                auto* c = parent->append("n" + std::to_string(i));
                build(c, depth - 1);
            }
        };
        build(&root, 4);  // 10+100+1000+10000 = 11110 nodes
    }
    BENCH_END("lifecycle: build+destroy 11110-node tree x10");
}

VE_TEST(node_bench_lifecycle_wide_tree) {
    BENCH_BEGIN;
    for (int rep = 0; rep < 10; ++rep) {
        Node root("root");
        for (int i = 0; i < 100000; ++i)
            root.append("n" + std::to_string(i));
    }
    BENCH_END("lifecycle: build+destroy 100k-wide tree x10");
}

// ============================================================================
// Benchmarks — Silent mode (signal OFF, fair comparison with MObj quiet)
// ============================================================================

VE_TEST(node_bench_silent_insert_100k_named) {
    Node root("root");
    root.silent(true);
    BENCH_BEGIN;
    for (int i = 0; i < 100000; ++i)
        root.append("n" + std::to_string(i));
    BENCH_END("silent: insert 100k named");
    VE_ASSERT_EQ(root.count(), 100000);
}

VE_TEST(node_bench_silent_insert_100k_anon) {
    Node root("root");
    root.silent(true);
    BENCH_BEGIN;
    for (int i = 0; i < 100000; ++i)
        root.append("");
    BENCH_END("silent: insert 100k anon");
    VE_ASSERT_EQ(root.count(), 100000);
}

VE_TEST(node_bench_silent_clear_100k) {
    Node root("root");
    root.silent(true);
    for (int i = 0; i < 100000; ++i) root.append("");

    BENCH_BEGIN;
    root.clear();
    BENCH_END("silent: clear 100k");
    VE_ASSERT_EQ(root.count(), 0);
}

VE_TEST(node_bench_silent_lifecycle_100k) {
    BENCH_BEGIN;
    for (int rep = 0; rep < 10; ++rep) {
        Node root("root");
        root.silent(true);
        for (int i = 0; i < 100000; ++i)
            root.append("n" + std::to_string(i));
    }
    BENCH_END("silent: lifecycle 100k x10");
}

// ============================================================================
// Benchmarks — indexOf with guess
// ============================================================================

VE_TEST(node_bench_indexOf_guess) {
    Node root("root");
    root.silent(true);
    Vector<Node*> nodes;
    for (int i = 0; i < 1000; ++i)
        nodes.push_back(root.append(""));

    // with guess (exact hit)
    BENCH_BEGIN;
    for (int rep = 0; rep < 100; ++rep)
        for (int i = 0; i < 1000; ++i)
            (void)root.indexOf(nodes[i], i);
    BENCH_END("indexOf(guess=exact) 1k anon x100");
}

VE_TEST(node_bench_indexOf_guess_offset) {
    Node root("root");
    root.silent(true);
    Vector<Node*> nodes;
    for (int i = 0; i < 1000; ++i)
        nodes.push_back(root.append(""));

    // with guess (off by ~5)
    BENCH_BEGIN;
    for (int rep = 0; rep < 100; ++rep)
        for (int i = 0; i < 1000; ++i)
            (void)root.indexOf(nodes[i], std::max(0, i - 5));
    BENCH_END("indexOf(guess=off5) 1k anon x100");
}

VE_TEST(node_bench_indexOf_no_guess) {
    Node root("root");
    root.silent(true);
    Vector<Node*> nodes;
    for (int i = 0; i < 1000; ++i)
        nodes.push_back(root.append(""));

    // no guess (baseline)
    BENCH_BEGIN;
    for (int rep = 0; rep < 100; ++rep)
        for (int i = 0; i < 1000; ++i)
            (void)root.indexOf(nodes[i]);
    BENCH_END("indexOf(no guess) 1k anon x100");
}

// ============================================================================
// Benchmarks — JSON scenarios (tree + value)
// ============================================================================

// --- Build: JSON dict { "k0": 0, "k1": 1, ... } ---

VE_TEST(node_bench_json_dict_10k) {
    Node root("root");
    root.silent(true);
    BENCH_BEGIN;
    for (int i = 0; i < 10000; ++i) {
        auto* c = root.append("k" + std::to_string(i));
        c->set(Var(i));
    }
    BENCH_END("json dict 10k (insert+set int)");
    VE_ASSERT_EQ(root.count(), 10000);
    VE_ASSERT_EQ(root.child("k5000")->getInt(), 5000);
}

VE_TEST(node_bench_json_dict_100k) {
    Node root("root");
    root.silent(true);
    BENCH_BEGIN;
    for (int i = 0; i < 100000; ++i) {
        auto* c = root.append("k" + std::to_string(i));
        c->set(Var(i));
    }
    BENCH_END("json dict 100k (insert+set int)");
    VE_ASSERT_EQ(root.count(), 100000);
}

VE_TEST(node_bench_json_dict_str_10k) {
    Node root("root");
    root.silent(true);
    BENCH_BEGIN;
    for (int i = 0; i < 10000; ++i) {
        auto* c = root.append("k" + std::to_string(i));
        c->set(Var("value_" + std::to_string(i)));
    }
    BENCH_END("json dict 10k (insert+set string)");
    VE_ASSERT_EQ(root.count(), 10000);
}

VE_TEST(node_bench_json_dict_str_100k) {
    Node root("root");
    root.silent(true);
    BENCH_BEGIN;
    for (int i = 0; i < 100000; ++i) {
        auto* c = root.append("k" + std::to_string(i));
        c->set(Var("value_" + std::to_string(i)));
    }
    BENCH_END("json dict 100k (insert+set string)");
    VE_ASSERT_EQ(root.count(), 100000);
}

// --- Build: JSON array [ 0, 1, 2, ... ] ---

VE_TEST(node_bench_json_array_10k) {
    Node root("root");
    root.silent(true);
    BENCH_BEGIN;
    for (int i = 0; i < 10000; ++i)
        root.append("")->set(Var(i));
    BENCH_END("json array 10k (insert+set int)");
    VE_ASSERT_EQ(root.count(), 10000);
    VE_ASSERT_EQ(root.child(5000)->getInt(), 5000);
}

VE_TEST(node_bench_json_array_100k) {
    Node root("root");
    root.silent(true);
    BENCH_BEGIN;
    for (int i = 0; i < 100000; ++i)
        root.append("")->set(Var(i));
    BENCH_END("json array 100k (insert+set int)");
    VE_ASSERT_EQ(root.count(), 100000);
}

// --- Set values only (tree already built) ---

VE_TEST(node_bench_set_int_100k) {
    Node root("root");
    root.silent(true);
    for (int i = 0; i < 100000; ++i) root.append("");

    BENCH_BEGIN;
    int idx = 0;
    for (auto* n : root) n->set(Var(idx++));
    BENCH_END("set 100k values (int)");
    VE_ASSERT_EQ(root.child(50000)->getInt(), 50000);
}

VE_TEST(node_bench_set_string_100k) {
    Node root("root");
    root.silent(true);
    for (int i = 0; i < 100000; ++i) root.append("");

    BENCH_BEGIN;
    for (int i = 0; i < 100000; ++i)
        root.child(i)->set(Var("val_" + std::to_string(i)));
    BENCH_END("set 100k values (string)");
}

VE_TEST(node_bench_set_double_100k) {
    Node root("root");
    root.silent(true);
    for (int i = 0; i < 100000; ++i) root.append("");

    BENCH_BEGIN;
    for (int i = 0; i < 100000; ++i)
        root.child(i)->set(Var(i * 0.001));
    BENCH_END("set 100k values (double)");
}

// --- Read values (iterate + get) ---

VE_TEST(node_bench_read_int_100k) {
    Node root("root");
    root.silent(true);
    for (int i = 0; i < 100000; ++i)
        root.append("")->set(Var(i));

    BENCH_BEGIN;
    long long sum = 0;
    for (auto* n : root) sum += n->getInt();
    BENCH_END("read 100k int values");
    VE_ASSERT_EQ(sum, (long long)100000 * 99999 / 2);
}

VE_TEST(node_bench_read_string_100k) {
    Node root("root");
    root.silent(true);
    for (int i = 0; i < 100000; ++i)
        root.append("")->set(Var("val_" + std::to_string(i)));

    BENCH_BEGIN;
    int total_len = 0;
    for (auto* n : root) total_len += (int)n->getString().size();
    BENCH_END("read 100k string values");
    VE_ASSERT(total_len > 0);
}

// --- Update values (conditional set, all changed) ---

VE_TEST(node_bench_update_int_100k) {
    Node root("root");
    root.silent(true);
    for (int i = 0; i < 100000; ++i)
        root.append("")->set(Var(i));

    BENCH_BEGIN;
    int idx = 0;
    for (auto* n : root) { n->update(Var(idx + 100000)); ++idx; }
    BENCH_END("update 100k values (all changed)");
    VE_ASSERT_EQ(root.child(0)->getInt(), 100000);
}

// --- Realistic JSON: array of objects ---
// [ {"id":0, "name":"user_0", "email":"u0@test.com", "score":0.5}, ... ]

VE_TEST(node_bench_json_aoo_1k) {
    Node root("root");
    root.silent(true);
    BENCH_BEGIN;
    for (int i = 0; i < 1000; ++i) {
        auto* obj = root.append("");
        obj->append("id")->set(Var(i));
        obj->append("name")->set(Var("user_" + std::to_string(i)));
        obj->append("email")->set(Var("u" + std::to_string(i) + "@test.com"));
        obj->append("score")->set(Var(i * 0.5));
    }
    BENCH_END("json array-of-objects 1k x4 fields");
    VE_ASSERT_EQ(root.count(), 1000);
    VE_ASSERT_EQ(root.child(500)->child("id")->getInt(), 500);
}

VE_TEST(node_bench_json_aoo_10k) {
    Node root("root");
    root.silent(true);
    BENCH_BEGIN;
    for (int i = 0; i < 10000; ++i) {
        auto* obj = root.append("");
        obj->append("id")->set(Var(i));
        obj->append("name")->set(Var("user_" + std::to_string(i)));
        obj->append("email")->set(Var("u" + std::to_string(i) + "@test.com"));
        obj->append("score")->set(Var(i * 0.5));
    }
    BENCH_END("json array-of-objects 10k x4 fields");
    VE_ASSERT_EQ(root.count(), 10000);
}

// --- JSON nested config: 100 groups x 10 fields ---
// { "group0": { "name": "...", "enabled": true, "count": N, "ratio": 0.1, "items": [0..5] }, ... }

VE_TEST(node_bench_json_config_100g) {
    BENCH_BEGIN;
    for (int rep = 0; rep < 10; ++rep) {
        Node root("root");
        root.silent(true);
        for (int g = 0; g < 100; ++g) {
            auto* group = root.append("g" + std::to_string(g));
            group->append("name")->set(Var("Group " + std::to_string(g)));
            group->append("enabled")->set(Var(true));
            group->append("count")->set(Var(g * 10));
            group->append("ratio")->set(Var(g * 0.1));
            for (int c = 0; c < 6; ++c)
                group->append("item")->set(Var(c));
        }
    }
    BENCH_END("json config 100g x10 fields lifecycle x10");
}

// --- Full JSON dict lifecycle: build + read + destroy ---

VE_TEST(node_bench_json_lifecycle_dict_10k) {
    BENCH_BEGIN;
    for (int rep = 0; rep < 10; ++rep) {
        Node root("root");
        root.silent(true);
        for (int i = 0; i < 10000; ++i)
            root.append("k" + std::to_string(i))->set(Var("value_" + std::to_string(i)));
        for (auto* n : root) (void)n->getString();
    }
    BENCH_END("json dict 10k lifecycle (build+read+destroy) x10");
}

VE_TEST(node_bench_json_lifecycle_aoo_1k) {
    BENCH_BEGIN;
    for (int rep = 0; rep < 10; ++rep) {
        Node root("root");
        root.silent(true);
        for (int i = 0; i < 1000; ++i) {
            auto* obj = root.append("");
            obj->append("id")->set(Var(i));
            obj->append("name")->set(Var("user_" + std::to_string(i)));
            obj->append("email")->set(Var("u" + std::to_string(i) + "@test.com"));
            obj->append("score")->set(Var(i * 0.5));
        }
        for (auto* obj : root) {
            (void)obj->child("id")->getInt();
            (void)obj->child("name")->getString();
            (void)obj->child("email")->getString();
            (void)obj->child("score")->getDouble();
        }
    }
    BENCH_END("json aoo 1k lifecycle (build+read+destroy) x10");
}

// ============================================================================
// Benchmarks — copy + schema Json/Bin (compare with qt/example/bench veBench)
// ============================================================================

static void buildWideNamedTree(Node& root, int n)
{
    root.silent(true);
    for (int i = 0; i < n; ++i)
        root.append("n" + std::to_string(i))->set(i);
}

VE_TEST(node_bench_copy_wide_10k) {
    Node src("src");
    buildWideNamedTree(src, 10000);
    Node dst("dst");
    dst.silent(true);

    BENCH_BEGIN;
    dst.copy(&src, true, false, true);
    BENCH_END("copy: wide 10k named children");

    VE_ASSERT_EQ(dst.count(), 10000);
    VE_ASSERT_EQ(dst.child("n5000")->getInt(), 5000);
}

VE_TEST(node_bench_copy_wide_100k) {
    Node src("src");
    buildWideNamedTree(src, 100000);
    Node dst("dst");
    dst.silent(true);

    BENCH_BEGIN;
    dst.copy(&src, true, false, true);
    BENCH_END("copy: wide 100k named children");

    VE_ASSERT_EQ(dst.count(), 100000);
}

VE_TEST(node_bench_schema_json_roundtrip_10k) {
    Node src("src");
    buildWideNamedTree(src, 10000);

    schema::ExportOptions ex;
    ex.indent = 0;

    BENCH_BEGIN;
    std::string json = schema::exportAs<schema::JsonS>(&src, ex);
    Node        dst("dst");
    dst.silent(true);
    schema::ImportOptions im;
    VE_ASSERT(schema::importAs<schema::JsonS>(&dst, json, im));
    BENCH_END("schema: json export+import merge 10k wide");

    VE_ASSERT_EQ(dst.count(), 10000);
}

VE_TEST(node_bench_schema_bin_roundtrip_10k) {
    Node src("src");
    buildWideNamedTree(src, 10000);

    schema::ExportOptions ex;

    BENCH_BEGIN;
    Bytes       bytes = schema::exportAs<schema::BinS>(&src, ex);
    Node        dst("dst");
    dst.silent(true);
    schema::ImportOptions im;
    VE_ASSERT(schema::importAs<schema::BinS>(&dst, bytes.data(), bytes.size(), im));
    BENCH_END("schema: bin export+import merge 10k wide");

    VE_ASSERT_EQ(dst.count(), 10000);
}

#if VE_NODE_BENCH_LARGE
VE_TEST(node_bench_schema_json_roundtrip_100k) {
    Node src("src");
    buildWideNamedTree(src, 100000);

    schema::ExportOptions ex;
    ex.indent = 0;

    BENCH_BEGIN;
    std::string json = schema::exportAs<schema::JsonS>(&src, ex);
    Node        dst("dst");
    dst.silent(true);
    schema::ImportOptions im;
    VE_ASSERT(schema::importAs<schema::JsonS>(&dst, json, im));
    BENCH_END("schema: json export+import merge 100k wide (VE_NODE_BENCH_LARGE)");

    VE_ASSERT_EQ(dst.count(), 100000);
}
#endif
