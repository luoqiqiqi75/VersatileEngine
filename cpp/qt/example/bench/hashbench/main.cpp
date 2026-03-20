// =============================================================================
// hashbench — Hash container + linked-list benchmark
//
// Compares three child-container strategies used / considered for tree nodes:
//
//   A) QHash<QString, T*> + manual prev/next doubly-linked list
//      → real strategy used by imol::ModuleObject
//
//   B) std::unordered_map<string, T*> + manual prev/next doubly-linked list
//      → hypothetical: "what if Godot used stdlib instead of custom hash?"
//
//   C) ve::impl::InsertionOrderedHashMap<string, T*>  (Godot Robin Hood)
//      → real strategy used by ve::Node (via Dict<SmallVector<Node*,1>>)
//
// Operations benchmarked:
//   1. Insert    — sequential named keys
//   2. Lookup    — random-order key lookup
//   3. Traverse  — iterate all entries in insertion order
//   4. Remove    — random-order key removal
//   5. Mixed     — interleaved insert / remove (churn)
//
// Output: markdown table written to stdout AND hashbench_report.md
// =============================================================================

#include <QCoreApplication>
#include <QHash>
#include <QString>
#include <QDebug>
#include <QFile>
#include <QTextStream>

#include <unordered_map>
#include <string>
#include <vector>
#include <chrono>
#include <functional>
#include <random>
#include <algorithm>
#include <numeric>
#include <cstdio>
#include <cstring>

#include "ve/core/impl/ordered_hashmap.h"

// ============================================================================
// Timing helpers
// ============================================================================
using Clock = std::chrono::high_resolution_clock;

static double elapsed_ms(Clock::time_point t0, Clock::time_point t1)
{
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

// ============================================================================
// Pre-generate keys
// ============================================================================
static std::vector<std::string> genStdKeys(int n)
{
    std::vector<std::string> keys(n);
    for (int i = 0; i < n; ++i)
        keys[i] = "key" + std::to_string(i);
    return keys;
}

static std::vector<QString> genQKeys(int n)
{
    std::vector<QString> keys(n);
    for (int i = 0; i < n; ++i)
        keys[i] = QStringLiteral("key") + QString::number(i);
    return keys;
}

static std::vector<int> shuffledIndices(int n, unsigned seed = 42)
{
    std::vector<int> idx(n);
    std::iota(idx.begin(), idx.end(), 0);
    std::mt19937 rng(seed);
    std::shuffle(idx.begin(), idx.end(), rng);
    return idx;
}

// ============================================================================
// Strategy A: QHash<QString, Node*> + manual doubly-linked list
// ============================================================================
struct QHNode {
    QHNode* prev = nullptr;
    QHNode* next = nullptr;
};

struct QHashLL {
    QHash<QString, QHNode*> hash;
    QHNode* head = nullptr;
    QHNode* tail = nullptr;
    int count = 0;

    void insert(const QString& key) {
        auto* node = new QHNode();
        hash.insert(key, node);
        node->prev = tail;
        if (tail) tail->next = node;
        else head = node;
        tail = node;
        ++count;
    }

    bool remove(const QString& key) {
        auto it = hash.find(key);
        if (it == hash.end()) return false;
        QHNode* node = it.value();
        hash.erase(it);
        if (node->prev) node->prev->next = node->next;
        else head = node->next;
        if (node->next) node->next->prev = node->prev;
        else tail = node->prev;
        delete node;
        --count;
        return true;
    }

    QHNode* find(const QString& key) const {
        return hash.value(key, nullptr);
    }

    int traverse() const {
        int cnt = 0;
        for (auto* n = head; n; n = n->next) ++cnt;
        return cnt;
    }

    void clear() {
        auto* cur = head;
        while (cur) { auto* nx = cur->next; delete cur; cur = nx; }
        hash.clear();
        head = tail = nullptr;
        count = 0;
    }

    ~QHashLL() { clear(); }
};

// ============================================================================
// Strategy B: std::unordered_map<string, Node*> + manual doubly-linked list
// ============================================================================
struct StdNode {
    StdNode* prev = nullptr;
    StdNode* next = nullptr;
};

struct StdHashLL {
    std::unordered_map<std::string, StdNode*> hash;
    StdNode* head = nullptr;
    StdNode* tail = nullptr;
    int count = 0;

    void insert(const std::string& key) {
        auto* node = new StdNode();
        hash.emplace(key, node);
        node->prev = tail;
        if (tail) tail->next = node;
        else head = node;
        tail = node;
        ++count;
    }

    bool remove(const std::string& key) {
        auto it = hash.find(key);
        if (it == hash.end()) return false;
        StdNode* node = it->second;
        hash.erase(it);
        if (node->prev) node->prev->next = node->next;
        else head = node->next;
        if (node->next) node->next->prev = node->prev;
        else tail = node->prev;
        delete node;
        --count;
        return true;
    }

    StdNode* find(const std::string& key) const {
        auto it = hash.find(key);
        return it != hash.end() ? it->second : nullptr;
    }

    int traverse() const {
        int cnt = 0;
        for (auto* n = head; n; n = n->next) ++cnt;
        return cnt;
    }

    void clear() {
        auto* cur = head;
        while (cur) { auto* nx = cur->next; delete cur; cur = nx; }
        hash.clear();
        head = tail = nullptr;
        count = 0;
    }

    ~StdHashLL() { clear(); }
};

// ============================================================================
// Strategy C: Godot InsertionOrderedHashMap  (Robin Hood + integrated list)
// Value is a dummy int; iteration traverses the built-in linked list.
// ============================================================================
using GodotMap = ve::impl::InsertionOrderedHashMap<std::string, int>;

// ============================================================================
// Benchmark runner
// ============================================================================
struct Row {
    const char* op;
    int    N;
    double ms_A;   // QHash + LL
    double ms_B;   // std::unordered_map + LL
    double ms_C;   // Godot InsertionOrderedHashMap
};

static std::vector<Row> g_results;

static void record(const char* op, int N, double a, double b, double c)
{
    g_results.push_back({op, N, a, b, c});
    printf("  %-36s  N=%-8d  A=%8.2f ms  B=%8.2f ms  C=%8.2f ms\n", op, N, a, b, c);
}

// ---- Insert (sequential) ---------------------------------------------------
static void bench_insert(int N)
{
    auto qkeys  = genQKeys(N);
    auto skeys  = genStdKeys(N);

    // A: QHash + LL
    double ms_a;
    {
        QHashLL container;
        auto t0 = Clock::now();
        for (int i = 0; i < N; ++i)
            container.insert(qkeys[i]);
        ms_a = elapsed_ms(t0, Clock::now());
    }

    // B: std::unordered_map + LL
    double ms_b;
    {
        StdHashLL container;
        auto t0 = Clock::now();
        for (int i = 0; i < N; ++i)
            container.insert(skeys[i]);
        ms_b = elapsed_ms(t0, Clock::now());
    }

    // C: Godot
    double ms_c;
    {
        GodotMap container;
        auto t0 = Clock::now();
        for (int i = 0; i < N; ++i)
            container.insert(skeys[i], i);
        ms_c = elapsed_ms(t0, Clock::now());
    }

    record("insert (sequential)", N, ms_a, ms_b, ms_c);
}

// ---- Insert (with reserve) -------------------------------------------------
static void bench_insert_reserved(int N)
{
    auto qkeys  = genQKeys(N);
    auto skeys  = genStdKeys(N);

    double ms_a;
    {
        QHashLL container;
        container.hash.reserve(N);
        auto t0 = Clock::now();
        for (int i = 0; i < N; ++i)
            container.insert(qkeys[i]);
        ms_a = elapsed_ms(t0, Clock::now());
    }

    double ms_b;
    {
        StdHashLL container;
        container.hash.reserve(N);
        auto t0 = Clock::now();
        for (int i = 0; i < N; ++i)
            container.insert(skeys[i]);
        ms_b = elapsed_ms(t0, Clock::now());
    }

    double ms_c;
    {
        GodotMap container;
        container.reserve(N);
        auto t0 = Clock::now();
        for (int i = 0; i < N; ++i)
            container.insert(skeys[i], i);
        ms_c = elapsed_ms(t0, Clock::now());
    }

    record("insert (reserved)", N, ms_a, ms_b, ms_c);
}

// ---- Lookup (random order) -------------------------------------------------
static void bench_lookup(int N)
{
    auto qkeys = genQKeys(N);
    auto skeys = genStdKeys(N);
    auto order = shuffledIndices(N);

    // Prepare containers
    QHashLL ca;
    for (int i = 0; i < N; ++i) ca.insert(qkeys[i]);

    StdHashLL cb;
    for (int i = 0; i < N; ++i) cb.insert(skeys[i]);

    GodotMap cc;
    for (int i = 0; i < N; ++i) cc.insert(skeys[i], i);

    const int REPS = 5;

    double ms_a;
    {
        volatile void* sink;
        auto t0 = Clock::now();
        for (int r = 0; r < REPS; ++r)
            for (int idx : order)
                sink = ca.find(qkeys[idx]);
        ms_a = elapsed_ms(t0, Clock::now());
        (void)sink;
    }

    double ms_b;
    {
        volatile void* sink;
        auto t0 = Clock::now();
        for (int r = 0; r < REPS; ++r)
            for (int idx : order)
                sink = cb.find(skeys[idx]);
        ms_b = elapsed_ms(t0, Clock::now());
        (void)sink;
    }

    double ms_c;
    {
        volatile const int* sink;
        auto t0 = Clock::now();
        for (int r = 0; r < REPS; ++r)
            for (int idx : order)
                sink = cc.getptr(skeys[idx]);
        ms_c = elapsed_ms(t0, Clock::now());
        (void)sink;
    }

    char label[64];
    snprintf(label, sizeof(label), "lookup (random x%d)", REPS);
    record(label, N, ms_a, ms_b, ms_c);
}

// ---- Traverse (full iteration) ---------------------------------------------
static void bench_traverse(int N)
{
    auto qkeys = genQKeys(N);
    auto skeys = genStdKeys(N);

    QHashLL ca;
    for (int i = 0; i < N; ++i) ca.insert(qkeys[i]);

    StdHashLL cb;
    for (int i = 0; i < N; ++i) cb.insert(skeys[i]);

    GodotMap cc;
    for (int i = 0; i < N; ++i) cc.insert(skeys[i], i);

    const int REPS = 100;

    double ms_a;
    {
        volatile int sink;
        auto t0 = Clock::now();
        for (int r = 0; r < REPS; ++r)
            sink = ca.traverse();
        ms_a = elapsed_ms(t0, Clock::now());
        (void)sink;
    }

    double ms_b;
    {
        volatile int sink;
        auto t0 = Clock::now();
        for (int r = 0; r < REPS; ++r)
            sink = cb.traverse();
        ms_b = elapsed_ms(t0, Clock::now());
        (void)sink;
    }

    double ms_c;
    {
        volatile int sink;
        auto t0 = Clock::now();
        for (int r = 0; r < REPS; ++r) {
            int cnt = 0;
            for (auto it = cc.begin(); it != cc.end(); ++it) ++cnt;
            sink = cnt;
        }
        ms_c = elapsed_ms(t0, Clock::now());
        (void)sink;
    }

    char label[64];
    snprintf(label, sizeof(label), "traverse (x%d)", REPS);
    record(label, N, ms_a, ms_b, ms_c);
}

// ---- Remove (random half) --------------------------------------------------
static void bench_remove_half(int N)
{
    auto qkeys = genQKeys(N);
    auto skeys = genStdKeys(N);
    auto order = shuffledIndices(N);

    int half = N / 2;

    // A
    double ms_a;
    {
        QHashLL ca;
        for (int i = 0; i < N; ++i) ca.insert(qkeys[i]);
        auto t0 = Clock::now();
        for (int i = 0; i < half; ++i)
            ca.remove(qkeys[order[i]]);
        ms_a = elapsed_ms(t0, Clock::now());
    }

    // B
    double ms_b;
    {
        StdHashLL cb;
        for (int i = 0; i < N; ++i) cb.insert(skeys[i]);
        auto t0 = Clock::now();
        for (int i = 0; i < half; ++i)
            cb.remove(skeys[order[i]]);
        ms_b = elapsed_ms(t0, Clock::now());
    }

    // C
    double ms_c;
    {
        GodotMap cc;
        for (int i = 0; i < N; ++i) cc.insert(skeys[i], i);
        auto t0 = Clock::now();
        for (int i = 0; i < half; ++i)
            cc.erase(skeys[order[i]]);
        ms_c = elapsed_ms(t0, Clock::now());
    }

    record("remove (random N/2)", N, ms_a, ms_b, ms_c);
}

// ---- Remove (all, sequential) -----------------------------------------------
static void bench_remove_all(int N)
{
    auto qkeys = genQKeys(N);
    auto skeys = genStdKeys(N);

    double ms_a;
    {
        QHashLL ca;
        for (int i = 0; i < N; ++i) ca.insert(qkeys[i]);
        auto t0 = Clock::now();
        for (int i = 0; i < N; ++i)
            ca.remove(qkeys[i]);
        ms_a = elapsed_ms(t0, Clock::now());
    }

    double ms_b;
    {
        StdHashLL cb;
        for (int i = 0; i < N; ++i) cb.insert(skeys[i]);
        auto t0 = Clock::now();
        for (int i = 0; i < N; ++i)
            cb.remove(skeys[i]);
        ms_b = elapsed_ms(t0, Clock::now());
    }

    double ms_c;
    {
        GodotMap cc;
        for (int i = 0; i < N; ++i) cc.insert(skeys[i], i);
        auto t0 = Clock::now();
        for (int i = 0; i < N; ++i)
            cc.erase(skeys[i]);
        ms_c = elapsed_ms(t0, Clock::now());
    }

    record("remove (all sequential)", N, ms_a, ms_b, ms_c);
}

// ---- Mixed: insert N then remove N/2 then insert N/4 (churn) ---------------
static void bench_churn(int N)
{
    auto qkeys  = genQKeys(N);
    auto skeys  = genStdKeys(N);
    auto order  = shuffledIndices(N);

    int half    = N / 2;
    int quarter = N / 4;

    // Generate extra keys for re-insert phase
    std::vector<QString> qextra(quarter);
    std::vector<std::string> sextra(quarter);
    for (int i = 0; i < quarter; ++i) {
        sextra[i] = "extra" + std::to_string(i);
        qextra[i] = QStringLiteral("extra") + QString::number(i);
    }

    // A
    double ms_a;
    {
        QHashLL ca;
        auto t0 = Clock::now();
        for (int i = 0; i < N; ++i)
            ca.insert(qkeys[i]);
        for (int i = 0; i < half; ++i)
            ca.remove(qkeys[order[i]]);
        for (int i = 0; i < quarter; ++i)
            ca.insert(qextra[i]);
        ms_a = elapsed_ms(t0, Clock::now());
    }

    // B
    double ms_b;
    {
        StdHashLL cb;
        auto t0 = Clock::now();
        for (int i = 0; i < N; ++i)
            cb.insert(skeys[i]);
        for (int i = 0; i < half; ++i)
            cb.remove(skeys[order[i]]);
        for (int i = 0; i < quarter; ++i)
            cb.insert(sextra[i]);
        ms_b = elapsed_ms(t0, Clock::now());
    }

    // C
    double ms_c;
    {
        GodotMap cc;
        auto t0 = Clock::now();
        for (int i = 0; i < N; ++i)
            cc.insert(skeys[i], i);
        for (int i = 0; i < half; ++i)
            cc.erase(skeys[order[i]]);
        for (int i = 0; i < quarter; ++i)
            cc.insert(sextra[i], i);
        ms_c = elapsed_ms(t0, Clock::now());
    }

    record("churn (ins+rm+ins)", N, ms_a, ms_b, ms_c);
}

// ============================================================================
// Report generation
// ============================================================================
static void writeReport(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Cannot write report to" << path;
        return;
    }
    QTextStream out(&file);

    out << "# Hash Container Benchmark Report\n\n";
    out << "Comparing three child-container strategies for ordered tree nodes.\n\n";
    out << "| Strategy | Description |\n";
    out << "|----------|-------------|\n";
    out << "| **A** | `QHash<QString, T*>` + manual prev/next linked list (imol::ModuleObject) |\n";
    out << "| **B** | `std::unordered_map<string, T*>` + manual prev/next linked list |\n";
    out << "| **C** | `ve::impl::InsertionOrderedHashMap<string, T*>` — Godot Robin Hood (ve::Node) |\n";
    out << "\n";
    out << "## Results\n\n";
    out << "| Operation | N | A (ms) | B (ms) | C (ms) | Fastest |\n";
    out << "|-----------|---|--------|--------|--------|---------|\n";

    for (auto& r : g_results) {
        const char* fastest = "A";
        double best = r.ms_A;
        if (r.ms_B < best) { best = r.ms_B; fastest = "B"; }
        if (r.ms_C < best) { best = r.ms_C; fastest = "C"; }

        out << QString("| %1 | %2 | %3 | %4 | %5 | **%6** |\n")
               .arg(r.op)
               .arg(r.N)
               .arg(r.ms_A, 0, 'f', 2)
               .arg(r.ms_B, 0, 'f', 2)
               .arg(r.ms_C, 0, 'f', 2)
               .arg(fastest);
    }

    out << "\n## Notes\n\n";
    out << "- **A** uses `QString` keys (UTF-16, Qt hash). **B** and **C** use `std::string` keys.\n";
    out << "- **A** and **B** maintain a *separate* doubly-linked list alongside the hash table.\n";
    out << "- **C** integrates the linked list inside the hash map elements (Robin Hood open addressing).\n";
    out << "- Traverse for **A**/**B** walks the external linked list; for **C** walks the integrated list.\n";
    out << "- All times are wall-clock, single-threaded, Release build recommended.\n";

    file.close();
    qDebug() << "Report written to:" << path;
}

// ============================================================================
// Main
// ============================================================================
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    printf("==========================================================\n");
    printf(" hashbench — Hash Container Benchmark\n");
    printf("==========================================================\n");
    printf("\n");
    printf(" A = QHash<QString>     + prev/next linked list\n");
    printf(" B = std::unordered_map + prev/next linked list\n");
    printf(" C = Godot InsertionOrderedHashMap (Robin Hood + list)\n");
    printf("\n");

    // Test sizes
    const int sizes[] = { 1000, 10000, 100000, 500000 };

    for (int N : sizes) {
        printf("\n--- N = %d ---\n", N);
        bench_insert(N);
        bench_insert_reserved(N);
        bench_lookup(N);
        bench_traverse(N);
        bench_remove_half(N);
        bench_remove_all(N);
        bench_churn(N);
    }

    printf("\n==========================================================\n");
    printf(" Done. Writing report...\n");
    printf("==========================================================\n");

    writeReport("hashbench_report.md");

    return 0;
}
