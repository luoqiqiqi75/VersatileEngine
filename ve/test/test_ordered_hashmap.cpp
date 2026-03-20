// ----------------------------------------------------------------------------
// test_ordered_hashmap.cpp — ve::OrderedHashMap (Godot Robin Hood impl)
// ----------------------------------------------------------------------------

#include "ve_test.h"
#include "ve/core/base.h"

using namespace ve;

VE_TEST(ohm_insert_order) {
    OrderedHashMap<std::string, int> m;
    m["c"] = 3;
    m["a"] = 1;
    m["b"] = 2;

    Vector<std::string> keys;
    for (auto& kv : m) keys.append(std::string(kv.key));
    VE_ASSERT_EQ(keys.sizeAsInt(), 3);
    VE_ASSERT_EQ(keys[0], "c");
    VE_ASSERT_EQ(keys[1], "a");
    VE_ASSERT_EQ(keys[2], "b");
}

VE_TEST(ohm_auto_insert) {
    OrderedHashMap<std::string, int> m;
    m["new_key"];  // auto-insert with default value
    VE_ASSERT(m.has("new_key"));
    VE_ASSERT_EQ(m["new_key"], 0);
}

VE_TEST(ohm_erase_preserves_order) {
    OrderedHashMap<std::string, int> m;
    m["a"] = 1;
    m["b"] = 2;
    m["c"] = 3;
    m.erase("b");

    VE_ASSERT_EQ(m.size(), 2u);
    VE_ASSERT(!m.has("b"));

    auto keys = m.keys();
    VE_ASSERT_EQ(keys[0], "a");
    VE_ASSERT_EQ(keys[1], "c");
}

VE_TEST(ohm_has_getptr) {
    OrderedHashMap<int, std::string> m;
    m[42] = "hello";
    VE_ASSERT(m.has(42));
    VE_ASSERT(!m.has(99));
    VE_ASSERT(m.ptr(42) != nullptr);
    VE_ASSERT_EQ(*m.ptr(42), "hello");
    VE_ASSERT(m.ptr(99) == nullptr);
}

VE_TEST(ohm_stress_1000) {
    OrderedHashMap<int, int> m;
    for (int i = 0; i < 1000; ++i) m[i] = i * i;

    VE_ASSERT_EQ(m.size(), 1000u);

    // check insertion order
    int idx = 0;
    for (auto& kv : m) {
        VE_ASSERT_EQ(kv.key, idx);
        VE_ASSERT_EQ(kv.value, idx * idx);
        idx++;
    }

    // erase every other
    for (int i = 0; i < 1000; i += 2) m.erase(i);
    VE_ASSERT_EQ(m.size(), 500u);

    // remaining should be odd numbers in order
    idx = 1;
    for (auto& kv : m) {
        VE_ASSERT_EQ(kv.key, idx);
        idx += 2;
    }
}

VE_TEST(ohm_copy) {
    OrderedHashMap<std::string, int> m;
    m["x"] = 1;
    m["y"] = 2;

    OrderedHashMap<std::string, int> copy(m);
    VE_ASSERT_EQ(copy.size(), 2u);
    VE_ASSERT_EQ(copy["x"], 1);
    VE_ASSERT_EQ(copy["y"], 2);
}

VE_TEST(ohm_move) {
    OrderedHashMap<std::string, int> m;
    m["a"] = 10;

    OrderedHashMap<std::string, int> moved(std::move(m));
    VE_ASSERT_EQ(moved.size(), 1u);
    VE_ASSERT_EQ(moved["a"], 10);
    VE_ASSERT(m.is_empty());
}

VE_TEST(ohm_initializer_list) {
    OrderedHashMap<std::string, int> m({{"x", 1}, {"y", 2}});
    VE_ASSERT_EQ(m.size(), 2u);
    VE_ASSERT_EQ(m["x"], 1);

    auto keys = m.keys();
    VE_ASSERT_EQ(keys[0], "x");
    VE_ASSERT_EQ(keys[1], "y");
}

VE_TEST(ohm_keys_values) {
    OrderedHashMap<std::string, int> m;
    m["b"] = 2;
    m["a"] = 1;
    auto k = m.keys();
    auto v = m.values();
    VE_ASSERT_EQ(k[0], "b");
    VE_ASSERT_EQ(k[1], "a");
    VE_ASSERT_EQ(v[0], 2);
    VE_ASSERT_EQ(v[1], 1);
}

VE_TEST(ohm_clear) {
    OrderedHashMap<int, int> m;
    m[1] = 1;
    m[2] = 2;
    m.clear();
    VE_ASSERT(m.is_empty());
    VE_ASSERT_EQ(m.size(), 0u);
}

VE_TEST(ohm_value_with_default) {
    OrderedHashMap<std::string, int> m;
    m["a"] = 10;
    VE_ASSERT_EQ(m.value("a"), 10);
    VE_ASSERT_EQ(m.value("missing"), 0);
    VE_ASSERT_EQ(m.value("missing", -1), -1);
}

VE_TEST(ohm_ordered_dict) {
    Dict<int> d;
    d["first"] = 1;
    d["second"] = 2;
    VE_ASSERT((Dict<int>::DictLike::value));
    VE_ASSERT_EQ(d.size(), 2u);
}
