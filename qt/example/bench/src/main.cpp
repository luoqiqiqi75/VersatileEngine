// =============================================================================
// veBench — imol::ModuleObject benchmark (QCoreApplication, no GUI)
//
// Mirrors core/test/test_node_bench.cpp so that results can be compared
// side-by-side with ve::Node benchmarks.
//
// Compare with ve/test/test_node_bench.cpp — tags: "copy:", "schema: json", "schema: bin".
// API differences vs ve::Node::copy / schema import:
//   • copyFrom(context, other, auto_insert, auto_remove) has no auto_update; ve::Node::copy has
//     auto_update as third flag (false=set always emit changed, true=update suppress unchanged signals).
//   • importFromJson / importFromBin use (auto_insert, auto_replace, auto_remove); ve uses
//     (auto_insert, auto_remove, auto_update), so roundtrip comparisons should align semantics manually.
//   • Duplicate child names are not supported here; wide trees use unique "n"+i keys only.
//
// API mapping (ve::Node → imol::ModuleObject):
//   Node("name")             →  ModuleObject("name")
//   node.append("x")         →  mobj->append(ctx, "x")
//   node.count()             →  mobj->cmobjCount()  /  mobj->size()
//   node.child("x")          →  mobj->cmobj("x")        (nullptr ↔ isNull())
//   node.child(idx)          →  mobj->cmobj(idx)
//   node.first()             →  mobj->first()
//   node.next()              →  mobj->next()             (nextBmobj)
//   node.parent()            →  mobj->pmobj()
//   node.parent(lv)          →  mobj->pmobj(lv)
//   node.resolve("a/b/c")   →  mobj->rmobj("a.b.c")    (sep: '.' vs '/')
//   node.path(ancestor)      →  mobj->fullName(ancestor)
//   node.isAncestorOf(n)     →  n->isRmobjOf(mobj)      (reversed)
//   node.clear()             →  mobj->clear(ctx)
//   node.insert(child)       →  mobj->insert(ctx, child)
//   node.remove(child)       →  mobj->remove(ctx, child)
//   node.ensure("a/b/c")    →  mobj->insert(ctx, "a.b.c")  (path-based)
//
// Notes:
//   • ModuleObject does NOT support duplicate named children (unlike ve::Node).
//     Tests involving same-name siblings are adapted or skipped.
//   • Anonymous append("") generates a UUID-based ID (overhead included).
//   • Signal emission (with no connected slots) is included in timing.
//     ve::Node has no signal mechanism, so this is additional overhead.
// =============================================================================

#include <QCoreApplication>
#include <QString>
#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <chrono>

#include "imol/modulemanager.h"

using namespace imol;

// ============================================================================
// Helpers
// ============================================================================

static double elapsed_ms(std::chrono::steady_clock::time_point t0)
{
    auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

#define BENCH_BEGIN auto _t0 = std::chrono::steady_clock::now()
#define BENCH_END(label) qDebug().nospace() << "[bench] " << (label) << ": " << elapsed_ms(_t0) << " ms"

static int g_pass = 0;
static int g_fail = 0;

#define ASSERT_TRUE(expr)                                                       \
    do {                                                                        \
        if (!(expr)) {                                                          \
            qCritical() << "ASSERT FAILED:" << #expr << "at" << __FILE__        \
                        << ":" << __LINE__;                                     \
            ++g_fail;                                                           \
        } else { ++g_pass; }                                                    \
    } while (0)

#define ASSERT_EQ(a, b)                                                         \
    do {                                                                        \
        if ((a) != (b)) {                                                       \
            qCritical() << "ASSERT_EQ FAILED:" << #a << "==" << #b             \
                        << " | actual:" << (a) << "vs" << (b)                  \
                        << "at" << __FILE__ << ":" << __LINE__;                \
            ++g_fail;                                                           \
        } else { ++g_pass; }                                                    \
    } while (0)

#define RUN_TEST(func)                                                          \
    do {                                                                        \
        qDebug() << "";                                                         \
        qDebug().nospace() << "--- " << #func << " ---";                       \
        func();                                                                 \
    } while (0)

// ============================================================================
// Stress tests
// ============================================================================

// Mirrors: node_stress_10k_anon
// ModuleObject: empty name → auto-generated UUID-based ID
static void stress_10k_anon()
{
    ModuleObject root("root");
    root.quiet(true);

    for (int i = 0; i < 10000; ++i)
        root.append(&root, "");

    ASSERT_EQ(root.cmobjCount(), 10000);
    ASSERT_TRUE(!root.cmobj(0)->isNull());
    ASSERT_TRUE(!root.cmobj(5000)->isNull());
    ASSERT_TRUE(!root.cmobj(9999)->isNull());
    ASSERT_TRUE(root.cmobj(10000)->isNull());

    auto* mid = root.cmobj(5000);
    root.remove(&root, mid);
    ASSERT_EQ(root.cmobjCount(), 9999);
}

// Mirrors: node_stress_10k_named
static void stress_10k_named()
{
    ModuleObject root("root");
    root.quiet(true);

    for (int i = 0; i < 10000; ++i)
        root.append(&root, "n" + QString::number(i));

    ASSERT_EQ(root.cmobjCount(), 10000);
    ASSERT_TRUE(!root.cmobj("n0")->isNull());
    ASSERT_TRUE(!root.cmobj("n9999")->isNull());
    ASSERT_TRUE(root.cmobj("n10000")->isNull());

    root.remove(&root, "n5000");
    ASSERT_EQ(root.cmobjCount(), 9999);
    ASSERT_TRUE(root.cmobj("n5000")->isNull());
}

// Mirrors: node_stress_reparent
static void stress_reparent()
{
    ModuleObject p1("p1");
    ModuleObject p2("p2");
    p1.quiet(true);
    p2.quiet(true);

    for (int i = 0; i < 100; ++i)
        p1.append(&p1, "c" + QString::number(i));

    ASSERT_EQ(p1.cmobjCount(), 100);

    while (p1.cmobjCount() > 0) {
        auto* c = p1.first();
        p1.remove(&p1, c, false);   // remove without delete
        p2.insert(&p2, c);
    }

    ASSERT_EQ(p1.cmobjCount(), 0);
    ASSERT_EQ(p2.cmobjCount(), 100);

    for (int i = 0; i < 100; ++i) {
        auto* c = p2.cmobj("c" + QString::number(i));
        ASSERT_TRUE(!c->isNull());
        ASSERT_TRUE(c->pmobj() == &p2);
    }
}

// ============================================================================
// Complex structure tests
// ============================================================================

// Mirrors: node_complex_robot
static void complex_robot()
{
    ModuleObject robot("robot");
    robot.quiet(true);

    auto* base = robot.append(&robot, "base_link");
    auto* j1   = base->append(base, "joint1");
    auto* l1   = j1->append(j1, "link1");
    auto* j2   = l1->append(l1, "joint2");
    auto* l2   = j2->append(j2, "link2");

    for (auto* link : {base, l1, l2}) {
        link->append(link, "visual");
        link->append(link, "collision");
        link->append(link, "inertial");
    }
    for (auto* joint : {j1, j2}) {
        joint->append(joint, "axis");
        joint->append(joint, "limit");
        joint->append(joint, "dynamics");
    }

    ASSERT_EQ(robot.cmobjCount(), 1);
    ASSERT_TRUE(!robot.rmobj("base_link.visual")->isNull());
    ASSERT_TRUE(!robot.rmobj("base_link.joint1.link1.joint2.link2.visual")->isNull());

    auto p = l2->fullName(&robot);
    qDebug() << "l2 path:" << p;
    ASSERT_EQ(robot.rmobj(p), l2);
}

// Mirrors: node_complex_deep_tree
// Adapted: use unique names "l0".."l99" (ModuleObject forbids duplicate names)
static void complex_deep_tree()
{
    ModuleObject root("root");
    root.quiet(true);

    ModuleObject* cur = &root;
    for (int i = 0; i < 100; ++i)
        cur = cur->append(cur, "l" + QString::number(i));

    ASSERT_EQ(cur->name(), "l99");
    ASSERT_TRUE(cur->pmobj(99) == &root);
    ASSERT_TRUE(cur->pmobj(100)->isNull());

    auto p = cur->fullName(&root);
    ASSERT_EQ(root.rmobj(p), cur);
    ASSERT_TRUE(cur->isRmobjOf(&root));
    ASSERT_TRUE(!root.isRmobjOf(cur));

    qDebug() << "deep tree path length:" << p.size() << "chars";
}

// Mirrors: node_complex_ensure_erase
// Adapted: insert(ctx, rpath) replaces ensure(); remove(ctx, rpath) replaces erase()
static void complex_ensure_erase()
{
    ModuleObject root("root");
    root.quiet(true);

    root.insert(&root, "a.b.c.d.e.f");
    auto* leaf = root.rmobj("a.b.c.d.e.f");
    ASSERT_TRUE(!leaf->isNull());
    ASSERT_EQ(leaf->name(), "f");
    ASSERT_EQ(leaf->fullName(&root), QString("a.b.c.d.e.f"));

    root.remove(&root, "a.b.c.d.e.f");
    ASSERT_TRUE(root.rmobj("a.b.c.d.e.f")->isNull());
    ASSERT_TRUE(!root.rmobj("a.b.c.d.e")->isNull());

    root.insert(&root, "a.b.c.d.e.f");
    auto* leaf2 = root.rmobj("a.b.c.d.e.f");
    ASSERT_TRUE(!leaf2->isNull());
    ASSERT_EQ(root.rmobj("a.b.c.d.e.f"), leaf2);

    qDebug() << "ensure/erase roundtrip OK";
}

// ============================================================================
// Benchmarks
// ============================================================================

// Mirrors: node_bench_insert_100k_named
static void bench_insert_100k_named()
{
    ModuleObject root("root");
    root.quiet(true);

    BENCH_BEGIN;
    for (int i = 0; i < 100000; ++i)
        root.append(&root, "n" + QString::number(i));
    BENCH_END("insert 100k named");

    ASSERT_EQ(root.cmobjCount(), 100000);
}

// Mirrors: node_bench_insert_100k_anon
// Note: ModuleObject generates UUID for each anonymous node (extra overhead)
static void bench_insert_100k_anon()
{
    ModuleObject root("root");
    root.quiet(true);

    BENCH_BEGIN;
    for (int i = 0; i < 100000; ++i)
        root.append(&root, "");
    BENCH_END("insert 100k anon (UUID gen)");

    ASSERT_EQ(root.cmobjCount(), 100000);
}

// Large scale: 500k named
static void bench_insert_500k_named()
{
    ModuleObject root("root");
    root.quiet(true);

    BENCH_BEGIN;
    for (int i = 0; i < 500000; ++i)
        root.append(&root, "n" + QString::number(i));
    BENCH_END("insert 500k named");

    ASSERT_EQ(root.cmobjCount(), 500000);
}

// Large scale: 500k anon
static void bench_insert_500k_anon()
{
    ModuleObject root("root");
    root.quiet(true);

    BENCH_BEGIN;
    for (int i = 0; i < 500000; ++i)
        root.append(&root, "");
    BENCH_END("insert 500k anon (UUID gen)");

    ASSERT_EQ(root.cmobjCount(), 500000);
}

// Large scale: 1M named
static void bench_insert_1m_named()
{
    ModuleObject root("root");
    root.quiet(true);

    BENCH_BEGIN;
    for (int i = 0; i < 1000000; ++i)
        root.append(&root, "n" + QString::number(i));
    BENCH_END("insert 1M named");

    ASSERT_EQ(root.cmobjCount(), 1000000);
}

// Large scale: 1M anon
static void bench_insert_1m_anon()
{
    ModuleObject root("root");
    root.quiet(true);

    BENCH_BEGIN;
    for (int i = 0; i < 1000000; ++i)
        root.append(&root, "");
    BENCH_END("insert 1M anon (UUID gen)");

    ASSERT_EQ(root.cmobjCount(), 1000000);
}

// Clear 500k
static void bench_clear_500k()
{
    ModuleObject root("root");
    root.quiet(true);

    for (int i = 0; i < 500000; ++i)
        root.append(&root, "");

    BENCH_BEGIN;
    root.clear(&root);
    BENCH_END("clear 500k");

    ASSERT_EQ(root.cmobjCount(), 0);
}

// Clear 1M
static void bench_clear_1m()
{
    ModuleObject root("root");
    root.quiet(true);

    for (int i = 0; i < 1000000; ++i)
        root.append(&root, "");

    BENCH_BEGIN;
    root.clear(&root);
    BENCH_END("clear 1M");

    ASSERT_EQ(root.cmobjCount(), 0);
}

// Iterator 100k
static void bench_iter_100k()
{
    ModuleObject root("root");
    root.quiet(true);
    for (int i = 0; i < 100000; ++i)
        root.append(&root, "k" + QString::number(i));

    BENCH_BEGIN;
    int cnt = 0;
    for (auto* n = root.first(); !n->isNull(); n = n->next()) ++cnt;
    BENCH_END("next() 100k named");

    ASSERT_EQ(cnt, 100000);
}

// Lifecycle: build+destroy 100k
static void bench_lifecycle_100k()
{
    BENCH_BEGIN;
    for (int rep = 0; rep < 10; ++rep) {
        ModuleObject root("root");
        root.quiet(true);
        for (int i = 0; i < 100000; ++i)
            root.append(&root, "n" + QString::number(i));
        root.clear(&root);
    }
    BENCH_END("lifecycle: build+clear 100k x10");
}

// Mirrors: node_bench_lookup_name
static void bench_lookup_name()
{
    ModuleObject root("root");
    root.quiet(true);

    for (int i = 0; i < 10000; ++i)
        root.append(&root, "n" + QString::number(i));

    BENCH_BEGIN;
    for (int rep = 0; rep < 10; ++rep)
        for (int i = 0; i < 10000; ++i)
            (void)root.cmobj("n" + QString::number(i));
    BENCH_END("lookup 10k names x10");
}

// Mirrors: node_bench_lookup_global
static void bench_lookup_global()
{
    ModuleObject root("root");
    root.quiet(true);

    for (int i = 0; i < 10000; ++i)
        root.append(&root, "");

    BENCH_BEGIN;
    for (int rep = 0; rep < 100; ++rep)
        for (int i = 0; i < 10000; ++i)
            (void)root.cmobj(i);
    BENCH_END("lookup 10k global x100");
}

// Mirrors: node_bench_resolve_path
// Uses unique names since duplicate names not supported
static void bench_resolve_path()
{
    ModuleObject root("root");
    root.quiet(true);

    ModuleObject* cur = &root;
    for (int i = 0; i < 10; ++i)
        cur = cur->append(cur, "l" + QString::number(i));

    QString p = cur->fullName(&root);  // "l0.l1.l2...l9"

    BENCH_BEGIN;
    for (int i = 0; i < 100000; ++i)
        (void)root.rmobj(p);
    BENCH_END("resolve 10-deep path x100k");

    ASSERT_EQ(root.rmobj(p), cur);
}

// Mirrors: node_bench_indexOf_global
static void bench_indexOf_global()
{
    ModuleObject root("root");
    root.quiet(true);

    QList<ModuleObject*> nodes;
    for (int i = 0; i < 1000; ++i)
        nodes.append(root.append(&root, "n" + QString::number(i)));

    BENCH_BEGIN;
    for (int rep = 0; rep < 100; ++rep)
        for (auto* n : nodes)
            (void)n->indexInPmobj();
    BENCH_END("indexOf(global) 1k nodes x100");
}

// --- Scenario: JSON dict (10k unique names) — next() traverse ---
// Mirrors: node_bench_next_dict
static void bench_next_dict()
{
    ModuleObject root("root");
    root.quiet(true);

    for (int i = 0; i < 10000; ++i)
        root.append(&root, "k" + QString::number(i));

    BENCH_BEGIN;
    int cnt = 0;
    for (auto* n = root.first(); !n->isNull(); n = n->next()) ++cnt;
    BENCH_END("next() dict 10k unique");

    ASSERT_EQ(cnt, 10000);
}

// --- Scenario: XML-like (100 groups x 10 items) — next() traverse ---
// Mirrors: node_bench_next_xml
// Adapted: ModuleObject forbids duplicate names, so use "tagG_I" unique names
static void bench_next_xml()
{
    ModuleObject root("root");
    root.quiet(true);

    for (int g = 0; g < 100; ++g)
        for (int i = 0; i < 10; ++i)
            root.append(&root, "tag" + QString::number(g) + "_" + QString::number(i));

    BENCH_BEGIN;
    int cnt = 0;
    for (auto* n = root.first(); !n->isNull(); n = n->next()) ++cnt;
    BENCH_END("next() xml-like 100x10");

    ASSERT_EQ(cnt, 1000);
}

// --- Scenario: JSON list (10k anon) — cmobj(index) sequential ---
// Mirrors: node_bench_list_sequential
static void bench_list_sequential()
{
    ModuleObject root("root");
    root.quiet(true);

    for (int i = 0; i < 10000; ++i)
        root.append(&root, "");

    BENCH_BEGIN;
    for (int rep = 0; rep < 10; ++rep)
        for (int i = 0; i < 10000; ++i)
            (void)root.cmobj(i);
    BENCH_END("cmobj(idx) list 10k x10");
}

// Mirrors: node_bench_ensure_deep
// Uses insert(ctx, rpath) to create deep paths
static void bench_ensure_deep()
{
    ModuleObject root("root");
    root.quiet(true);

    BENCH_BEGIN;
    for (int i = 0; i < 1000; ++i) {
        QString p = "a.b.c.d.e.f" + QString::number(i);
        root.insert(&root, p);
    }
    BENCH_END("ensure(insert rpath) 1k deep paths");

    // a.b.c.d.e should exist, with 1000 different f* children
    auto* e = root.rmobj("a.b.c.d.e");
    ASSERT_TRUE(!e->isNull());
    ASSERT_EQ(e->cmobjCount(), 1000);
}

// Mirrors: node_bench_clear_100k
static void bench_clear_100k()
{
    ModuleObject root("root");
    root.quiet(true);

    for (int i = 0; i < 100000; ++i)
        root.append(&root, "");

    BENCH_BEGIN;
    root.clear(&root);
    BENCH_END("clear 100k");

    ASSERT_EQ(root.cmobjCount(), 0);
}

// Mirrors: node_bench_path_build
// Adapted: 50 unique level names
static void bench_path_build()
{
    ModuleObject root("root");
    root.quiet(true);

    ModuleObject* cur = &root;
    for (int i = 0; i < 50; ++i)
        cur = cur->append(cur, "l" + QString::number(i));

    BENCH_BEGIN;
    for (int i = 0; i < 10000; ++i)
        (void)cur->fullName(&root);
    BENCH_END("fullName(path) 50-deep x10k");
}

// Mirrors: node_bench_keyOf
// rname() on anonymous (UUID) nodes → calls indexInPmobj() which traverses the
// linked list backwards. Equivalent to Node::keyOf which calls indexOf.
static void bench_rname()
{
    ModuleObject root("root");
    root.quiet(true);

    QList<ModuleObject*> nodes;
    for (int i = 0; i < 100; ++i)
        nodes.append(root.append(&root, ""));

    BENCH_BEGIN;
    for (int rep = 0; rep < 10000; ++rep)
        for (auto* n : nodes)
            (void)n->rname();
    BENCH_END("rname() 100 anon x10k");
}

// Mirrors: node_bench_childAt_key
// Uses cmobj("#N") which parses index from string → traversal
static void bench_childAt_key()
{
    ModuleObject root("root");
    root.quiet(true);

    for (int i = 0; i < 100; ++i)
        root.append(&root, "");

    BENCH_BEGIN;
    for (int rep = 0; rep < 10000; ++rep) {
        for (int i = 0; i < 100; ++i)
            (void)root.cmobj("#" + QString::number(i));
    }
    BENCH_END("cmobj(\"#N\") 100 nodes x10k");
}

// ============================================================================
// Benchmarks — Signal enabled (fair comparison with ve::Node)
// ============================================================================

// ve::Node always triggers signals on insert/remove/clear.
// These benchmarks run with quiet(false) to match that behavior.

static void bench_signal_insert_100k_named()
{
    ModuleObject root("root");
    // quiet(false) — default, signals enabled

    BENCH_BEGIN;
    for (int i = 0; i < 100000; ++i)
        root.append(&root, "n" + QString::number(i));
    BENCH_END("signal ON: insert 100k named");

    ASSERT_EQ(root.cmobjCount(), 100000);
}

static void bench_signal_insert_100k_anon()
{
    ModuleObject root("root");

    BENCH_BEGIN;
    for (int i = 0; i < 100000; ++i)
        root.append(&root, "");
    BENCH_END("signal ON: insert 100k anon (UUID)");

    ASSERT_EQ(root.cmobjCount(), 100000);
}

static void bench_signal_clear_100k()
{
    ModuleObject root("root");
    root.quiet(true);
    for (int i = 0; i < 100000; ++i) root.append(&root, "");
    root.quiet(false);

    BENCH_BEGIN;
    root.clear(&root);
    BENCH_END("signal ON: clear 100k");

    ASSERT_EQ(root.cmobjCount(), 0);
}

static void bench_signal_lifecycle_100k()
{
    BENCH_BEGIN;
    for (int rep = 0; rep < 10; ++rep) {
        ModuleObject root("root");
        // signals enabled
        for (int i = 0; i < 100000; ++i)
            root.append(&root, "n" + QString::number(i));
        root.clear(&root);
    }
    BENCH_END("signal ON: lifecycle 100k x10");
}

// ============================================================================
// Benchmarks — Remove (mirrors ve::Node remove benchmarks)
// ============================================================================

static void bench_remove_100k_from_back()
{
    ModuleObject root("root");
    root.quiet(true);
    for (int i = 0; i < 100000; ++i) root.append(&root, "");

    BENCH_BEGIN;
    while (root.cmobjCount() > 0) {
        auto* last = root.cmobj(root.cmobjCount() - 1);
        root.remove(&root, last);
    }
    BENCH_END("remove 100k from back (one by one)");

    ASSERT_EQ(root.cmobjCount(), 0);
}

static void bench_remove_100k_from_front()
{
    ModuleObject root("root");
    root.quiet(true);
    for (int i = 0; i < 100000; ++i) root.append(&root, "");

    BENCH_BEGIN;
    while (root.cmobjCount() > 0) root.remove(&root, root.first());
    BENCH_END("remove 100k from front (one by one)");

    ASSERT_EQ(root.cmobjCount(), 0);
}

// ============================================================================
// XML serialization benchmarks (ModuleObject's own XML format)
// ============================================================================

// Helper: build a realistic tree for XML benchmarks
// Structure: 50 named groups, each with 20 named children → 1050 nodes total
static ModuleObject* buildXmlTree()
{
    auto* root = new ModuleObject("root");
    root->quiet(true);
    for (int g = 0; g < 50; ++g) {
        auto* group = root->append(root, "group" + QString::number(g));
        for (int c = 0; c < 20; ++c)
            group->append(group, "child" + QString::number(c));
    }
    return root;
}

// Benchmark: export tree → XML string
static void bench_xml_export()
{
    auto* root = buildXmlTree();

    BENCH_BEGIN;
    QString xml;
    for (int rep = 0; rep < 100; ++rep)
        xml = root->exportToXmlStr(0);  // no indent for speed
    BENCH_END("exportToXmlStr 1050 nodes x100");

    qDebug() << "XML size:" << xml.size() << "chars";
    delete root;
}

// Benchmark: import tree ← XML string
static void bench_xml_import()
{
    auto* src = buildXmlTree();
    QString xml = src->exportToXmlStr(0);
    delete src;

    BENCH_BEGIN;
    for (int rep = 0; rep < 100; ++rep) {
        ModuleObject target("root");
        target.quiet(true);
        target.importFromXmlStr(&target, xml);
    }
    BENCH_END("importFromXmlStr 1050 nodes x100");
}

// ============================================================================
// Copy + JSON + Bin in-memory (mirrors ve_node_bench schema benchmarks)
// ============================================================================

static void bench_copy_wide_10k()
{
    ModuleObject src("src");
    src.quiet(true);
    for (int i = 0; i < 10000; ++i) {
        auto* c = src.append(&src, "n" + QString::number(i));
        c->set(QVariant(i));
    }
    ModuleObject dst("dst");
    dst.quiet(true);
    BENCH_BEGIN;
    dst.copyFrom(&dst, &src, true, false);
    BENCH_END("module: copyFrom wide 10k");
    ASSERT_EQ(dst.cmobjCount(), 10000);
}

static void bench_copy_wide_100k()
{
    ModuleObject src("src");
    src.quiet(true);
    for (int i = 0; i < 100000; ++i) {
        auto* c = src.append(&src, "n" + QString::number(i));
        c->set(QVariant(i));
    }
    ModuleObject dst("dst");
    dst.quiet(true);
    BENCH_BEGIN;
    dst.copyFrom(&dst, &src, true, false);
    BENCH_END("module: copyFrom wide 100k");
    ASSERT_EQ(dst.cmobjCount(), 100000);
}

static void bench_json_roundtrip_10k()
{
    ModuleObject src("src");
    src.quiet(true);
    for (int i = 0; i < 10000; ++i) {
        auto* c = src.append(&src, "n" + QString::number(i));
        c->set(QVariant(i));
    }
    BENCH_BEGIN;
    QJsonValue jv = src.exportToJson(false);
    ModuleObject dst("dst");
    dst.quiet(true);
    dst.importFromJson(&dst, jv, true, true, false);
    BENCH_END("module: json export+import 10k wide");
    ASSERT_EQ(dst.cmobjCount(), 10000);
}

static void bench_bin_roundtrip_10k()
{
    ModuleObject src("src");
    src.quiet(true);
    for (int i = 0; i < 10000; ++i) {
        auto* c = src.append(&src, "n" + QString::number(i));
        c->set(QVariant(i));
    }
    BENCH_BEGIN;
    QByteArray bin = src.exportToBin(false);
    ModuleObject dst("dst");
    dst.quiet(true);
    dst.importFromBin(&dst, bin, true, true, false);
    BENCH_END("module: bin export+import 10k wide");
    ASSERT_EQ(dst.cmobjCount(), 10000);
}

// ============================================================================
// JSON import benchmark (file path from argv[1] or default "d:/a.json")
// ============================================================================

static QString g_json_path;

static int countAllMobj(ModuleObject* mobj)
{
    int c = 1;
    for (auto* child : mobj->cmobjs(true))
        c += countAllMobj(child);
    return c;
}

static void bench_json_import_file()
{
    if (g_json_path.isEmpty()) {
        qDebug() << "  (skipped, no JSON file specified)";
        return;
    }

    // Phase 1: file read + Qt JSON parse (once)
    QFile file(g_json_path);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "  cannot open:" << g_json_path;
        return;
    }
    QByteArray raw = file.readAll();
    file.close();

    QJsonValue jval;
    {
        BENCH_BEGIN;
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
        if (err.error != QJsonParseError::NoError) {
            qDebug() << "  JSON parse error:" << err.errorString();
            return;
        }
        jval = doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(doc.array());
        BENCH_END("Qt JSON parse");
    }

    // Phase 2: importFromJson N times (pure ModuleObject construction)
    const int reps = 10;
    {
        int topLevel = 0;
        BENCH_BEGIN;
        for (int r = 0; r < reps; ++r) {
            ModuleObject root("bench_root");
            root.quiet(true);
            root.importFromJson(&root, jval, true, true, false);
            topLevel = root.cmobjCount();
            root.clear(&root);
        }
        BENCH_END("ModuleObject importFromJson+clear x10");
        qDebug().nospace() << "  top-level children: " << topLevel;
    }

    // Phase 3: build once and count total
    {
        ModuleObject root("bench_root");
        root.quiet(true);
        root.importFromJson(&root, jval, true, true, false);
        int total = countAllMobj(&root) - 1;
        qDebug().nospace() << "  total nodes: " << total;
    }
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    // Accept optional JSON path: veBench [json_file]
    if (argc > 1) g_json_path = QString::fromLocal8Bit(argv[1]);

    qDebug() << "==========================================================";
    qDebug() << " veBench — imol::ModuleObject benchmark";
    qDebug() << " Compare with ve::Node benchmarks (test_node_bench)";
    qDebug() << "==========================================================";
    qDebug() << "";
    qDebug() << "Note: ModuleObject quiet(true) is used to skip signal emission.";
    qDebug() << "      Anonymous nodes use UUID generation (extra overhead vs ve::Node).";
    qDebug() << "      Path separator is '.' (ModuleObject) vs '/' (ve::Node).";
    if (!g_json_path.isEmpty())
        qDebug().nospace() << "      JSON bench file: " << g_json_path;
    qDebug() << "";

    // --- Stress tests ---
    qDebug() << "==================== Stress tests ====================";
    RUN_TEST(stress_10k_anon);
    RUN_TEST(stress_10k_named);
    RUN_TEST(stress_reparent);

    // --- Complex structure tests ---
    qDebug() << "";
    qDebug() << "==================== Complex structures ====================";
    RUN_TEST(complex_robot);
    RUN_TEST(complex_deep_tree);
    RUN_TEST(complex_ensure_erase);

    // --- Benchmarks ---
    qDebug() << "";
    qDebug() << "==================== Benchmarks ====================";
    RUN_TEST(bench_insert_100k_named);
    RUN_TEST(bench_insert_100k_anon);
    RUN_TEST(bench_insert_500k_named);
    RUN_TEST(bench_insert_500k_anon);
    RUN_TEST(bench_insert_1m_named);
    RUN_TEST(bench_insert_1m_anon);
    RUN_TEST(bench_lookup_name);
    RUN_TEST(bench_lookup_global);
    RUN_TEST(bench_resolve_path);
    RUN_TEST(bench_indexOf_global);
    RUN_TEST(bench_next_dict);
    RUN_TEST(bench_next_xml);
    RUN_TEST(bench_iter_100k);
    RUN_TEST(bench_list_sequential);
    RUN_TEST(bench_ensure_deep);
    RUN_TEST(bench_clear_100k);
    RUN_TEST(bench_clear_500k);
    RUN_TEST(bench_clear_1m);
    RUN_TEST(bench_path_build);
    RUN_TEST(bench_rname);
    RUN_TEST(bench_childAt_key);
    RUN_TEST(bench_lifecycle_100k);

    // --- Signal enabled (fair comparison) ---
    qDebug() << "";
    qDebug() << "==================== Signal enabled ====================";
    RUN_TEST(bench_signal_insert_100k_named);
    RUN_TEST(bench_signal_insert_100k_anon);
    RUN_TEST(bench_signal_clear_100k);
    RUN_TEST(bench_signal_lifecycle_100k);

    // --- Remove ---
    qDebug() << "";
    qDebug() << "==================== Remove ====================";
    RUN_TEST(bench_remove_100k_from_back);
    RUN_TEST(bench_remove_100k_from_front);

    // --- XML serialization ---
    qDebug() << "";
    qDebug() << "==================== XML serialization ====================";
    RUN_TEST(bench_xml_export);
    RUN_TEST(bench_xml_import);

    // --- Copy / JSON / Bin (mirror ve::Node schema benches) ---
    qDebug() << "";
    qDebug() << "==================== Copy + JSON + Bin (in-memory) ====================";
    RUN_TEST(bench_copy_wide_10k);
    RUN_TEST(bench_copy_wide_100k);
    RUN_TEST(bench_json_roundtrip_10k);
    RUN_TEST(bench_bin_roundtrip_10k);

    // --- JSON file import ---
    if (!g_json_path.isEmpty()) {
        qDebug() << "";
        qDebug() << "==================== JSON file import ====================";
        RUN_TEST(bench_json_import_file);
    }

    // --- Summary ---
    qDebug() << "";
    qDebug() << "==========================================================";
    qDebug().nospace() << " Result: " << g_pass << " passed, " << g_fail << " failed";
    qDebug() << "==========================================================";

    return g_fail > 0 ? 1 : 0;
}
