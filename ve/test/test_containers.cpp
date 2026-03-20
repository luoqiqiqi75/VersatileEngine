// ----------------------------------------------------------------------------
// test_containers.cpp — ve::Vector, List, Map, UnorderedHashMap, Dict
// ----------------------------------------------------------------------------

#include "ve_test.h"
#include "ve/core/base.h"

using namespace ve;

// ==================== Vector ====================

VE_TEST(vector_append) {
    Vector<int> v;
    v.append(1).append(2).append(3);
    VE_ASSERT_EQ(v.sizeAsInt(), 3);
    VE_ASSERT_EQ(v[0], 1);
    VE_ASSERT_EQ(v[2], 3);
}

VE_TEST(vector_prepend) {
    Vector<int> v;
    v.append(2).prepend(1);
    VE_ASSERT_EQ(v[0], 1);
    VE_ASSERT_EQ(v[1], 2);
}

VE_TEST(vector_insertOne) {
    Vector<int> v;
    v.append(1).append(3);
    v.insertOne(1, 2);
    VE_ASSERT_EQ(v.sizeAsInt(), 3);
    VE_ASSERT_EQ(v[1], 2);
}

VE_TEST(vector_has) {
    Vector<int> v;
    v.append(10);
    VE_ASSERT(v.has(0));
    VE_ASSERT(!v.has(1));
    VE_ASSERT(!v.has(-1));
}

VE_TEST(vector_value_default) {
    Vector<int> v;
    VE_ASSERT_EQ(v.value(0), 0);         // out of range → default
    VE_ASSERT_EQ(v.value(0, 42), 42);    // explicit default
}

VE_TEST(vector_every) {
    Vector<int> v;
    v.append(1).append(2).append(3);
    v.every([](int& x) { x *= 10; });
    VE_ASSERT_EQ(v[0], 10);
    VE_ASSERT_EQ(v[1], 20);
    VE_ASSERT_EQ(v[2], 30);
}

VE_TEST(vector_toString) {
    Vector<int> v;
    v.append(1).append(2).append(3);
    VE_ASSERT_EQ(v.toString(", "), "1, 2, 3");
}

VE_TEST(vector_from_std) {
    std::vector<int> sv = {4, 5, 6};
    Vector<int> v(sv);
    VE_ASSERT_EQ(v.sizeAsInt(), 3);
    VE_ASSERT_EQ(v[0], 4);
}

// ==================== List ====================

VE_TEST(list_index_operator) {
    List<int> l;
    l.push_back(10);
    l.push_back(20);
    VE_ASSERT_EQ(l[0], 10);
    VE_ASSERT_EQ(l[1], 20);
}

VE_TEST(list_out_of_range) {
    List<int> l;
    l.push_back(1);
    VE_ASSERT_THROWS(l[5]);
}

// ==================== Map ====================

VE_TEST(map_has_value) {
    Map<int, std::string> m;
    m[1] = "one";
    m[2] = "two";
    VE_ASSERT(m.has(1));
    VE_ASSERT(!m.has(3));
    VE_ASSERT_EQ(m.value(1), "one");
    VE_ASSERT_EQ(m.value(3), "");  // default
}

VE_TEST(map_keys_values) {
    Map<int, int> m;
    m[1] = 10;
    m[2] = 20;
    auto k = m.keys();
    auto v = m.values();
    VE_ASSERT_EQ(k.sizeAsInt(), 2);
    VE_ASSERT_EQ(v.sizeAsInt(), 2);
}

VE_TEST(map_insertOne) {
    Map<std::string, int> m;
    m.insertOne("a", 1);
    VE_ASSERT(m.has("a"));
    VE_ASSERT_EQ(m.value("a"), 1);
}

// ==================== UnorderedHashMap ====================

VE_TEST(hashmap_basic) {
    UnorderedHashMap<std::string, int> h;
    h["x"] = 10;
    h["y"] = 20;
    VE_ASSERT(h.has("x"));
    VE_ASSERT_EQ(h.value("x"), 10);
    VE_ASSERT(!h.has("z"));
}

VE_TEST(hashmap_sizeAsInt) {
    UnorderedHashMap<int, int> h;
    h[1] = 1;
    h[2] = 2;
    VE_ASSERT_EQ(h.sizeAsInt(), 2);
}

// ==================== Dict ====================

VE_TEST(dict_basic) {
    Dict<int> d;
    d["alpha"] = 100;
    VE_ASSERT(d.has("alpha"));
    VE_ASSERT_EQ(d.value("alpha"), 100);
}

VE_TEST(dict_dictlike_trait) {
    VE_ASSERT((Dict<int>::DictLike::value));
    VE_ASSERT(!(Map<int, int>::DictLike::value));
}
