// ----------------------------------------------------------------------------
// test_small_vector.cpp — ve::SmallVector<T, N> tests
// ----------------------------------------------------------------------------

#include "ve_test.h"
#include "ve/core/base.h"

#include <string>

using namespace ve;

// ---- basic inline storage ----

VE_TEST(sv_default_empty) {
    SmallVector<int, 2> v;
    VE_ASSERT(v.empty());
    VE_ASSERT_EQ(v.size(), 0u);
    VE_ASSERT_EQ(v.capacity(), 2u);
}

VE_TEST(sv_push_inline) {
    SmallVector<int, 4> v;
    v.push_back(10);
    v.push_back(20);
    v.push_back(30);
    VE_ASSERT_EQ(v.size(), 3u);
    VE_ASSERT_EQ(v[0], 10);
    VE_ASSERT_EQ(v[1], 20);
    VE_ASSERT_EQ(v[2], 30);
    VE_ASSERT_EQ(v.capacity(), 4u); // still inline
}

VE_TEST(sv_push_overflow_to_heap) {
    SmallVector<int, 2> v;
    v.push_back(1);
    v.push_back(2);
    VE_ASSERT_EQ(v.capacity(), 2u); // inline
    v.push_back(3); // triggers heap
    VE_ASSERT(v.capacity() > 2u);
    VE_ASSERT_EQ(v.size(), 3u);
    VE_ASSERT_EQ(v[0], 1);
    VE_ASSERT_EQ(v[1], 2);
    VE_ASSERT_EQ(v[2], 3);
}

VE_TEST(sv_front_back) {
    SmallVector<int, 2> v;
    v.push_back(5);
    v.push_back(9);
    VE_ASSERT_EQ(v.front(), 5);
    VE_ASSERT_EQ(v.back(), 9);
}

// ---- erase ----

VE_TEST(sv_erase_middle) {
    SmallVector<int, 4> v;
    v.push_back(10);
    v.push_back(20);
    v.push_back(30);
    v.push_back(40);
    v.erase(1); // remove 20
    VE_ASSERT_EQ(v.size(), 3u);
    VE_ASSERT_EQ(v[0], 10);
    VE_ASSERT_EQ(v[1], 30);
    VE_ASSERT_EQ(v[2], 40);
}

VE_TEST(sv_erase_swap) {
    SmallVector<int, 4> v;
    v.push_back(10);
    v.push_back(20);
    v.push_back(30);
    v.erase_swap(0); // swap with last → [30, 20]
    VE_ASSERT_EQ(v.size(), 2u);
    VE_ASSERT_EQ(v[0], 30);
    VE_ASSERT_EQ(v[1], 20);
}

VE_TEST(sv_pop_back) {
    SmallVector<int, 2> v;
    v.push_back(1);
    v.push_back(2);
    v.pop_back();
    VE_ASSERT_EQ(v.size(), 1u);
    VE_ASSERT_EQ(v[0], 1);
}

// ---- clear / resize / reserve ----

VE_TEST(sv_clear) {
    SmallVector<int, 2> v;
    v.push_back(1);
    v.push_back(2);
    v.clear();
    VE_ASSERT(v.empty());
    VE_ASSERT_EQ(v.size(), 0u);
}

VE_TEST(sv_resize_grow) {
    SmallVector<int, 2> v;
    v.resize(5, 42);
    VE_ASSERT_EQ(v.size(), 5u);
    VE_ASSERT_EQ(v[0], 42);
    VE_ASSERT_EQ(v[4], 42);
}

VE_TEST(sv_resize_shrink) {
    SmallVector<int, 4> v;
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);
    v.resize(1);
    VE_ASSERT_EQ(v.size(), 1u);
    VE_ASSERT_EQ(v[0], 1);
}

VE_TEST(sv_reserve) {
    SmallVector<int, 1> v;
    v.reserve(100);
    VE_ASSERT(v.capacity() >= 100u);
    VE_ASSERT_EQ(v.size(), 0u);
}

// ---- copy / move ----

VE_TEST(sv_copy_inline) {
    SmallVector<int, 4> a;
    a.push_back(1);
    a.push_back(2);
    SmallVector<int, 4> b(a);
    VE_ASSERT_EQ(b.size(), 2u);
    VE_ASSERT_EQ(b[0], 1);
    VE_ASSERT_EQ(b[1], 2);
    // original unchanged
    VE_ASSERT_EQ(a.size(), 2u);
}

VE_TEST(sv_copy_heap) {
    SmallVector<int, 1> a;
    a.push_back(1);
    a.push_back(2); // goes to heap
    a.push_back(3);
    SmallVector<int, 1> b(a);
    VE_ASSERT_EQ(b.size(), 3u);
    VE_ASSERT_EQ(b[0], 1);
    VE_ASSERT_EQ(b[2], 3);
}

VE_TEST(sv_move_inline) {
    SmallVector<int, 4> a;
    a.push_back(10);
    SmallVector<int, 4> b(std::move(a));
    VE_ASSERT_EQ(b.size(), 1u);
    VE_ASSERT_EQ(b[0], 10);
    VE_ASSERT_EQ(a.size(), 0u);
}

VE_TEST(sv_move_heap) {
    SmallVector<int, 1> a;
    for (int i = 0; i < 10; ++i) a.push_back(i);
    SmallVector<int, 1> b(std::move(a));
    VE_ASSERT_EQ(b.size(), 10u);
    VE_ASSERT_EQ(b[9], 9);
    VE_ASSERT_EQ(a.size(), 0u);
}

VE_TEST(sv_copy_assign) {
    SmallVector<int, 2> a;
    a.push_back(1);
    a.push_back(2);
    SmallVector<int, 2> b;
    b.push_back(99);
    b = a;
    VE_ASSERT_EQ(b.size(), 2u);
    VE_ASSERT_EQ(b[0], 1);
    VE_ASSERT_EQ(b[1], 2);
}

VE_TEST(sv_move_assign) {
    SmallVector<int, 2> a;
    a.push_back(1);
    a.push_back(2);
    SmallVector<int, 2> b;
    b = std::move(a);
    VE_ASSERT_EQ(b.size(), 2u);
    VE_ASSERT_EQ(b[0], 1);
    VE_ASSERT_EQ(a.size(), 0u);
}

// ---- initializer list ----

VE_TEST(sv_init_list) {
    SmallVector<int, 2> v = {10, 20, 30};
    VE_ASSERT_EQ(v.size(), 3u);
    VE_ASSERT_EQ(v[0], 10);
    VE_ASSERT_EQ(v[2], 30);
}

// ---- iterators ----

VE_TEST(sv_range_for) {
    SmallVector<int, 4> v = {1, 2, 3};
    int sum = 0;
    for (int x : v) sum += x;
    VE_ASSERT_EQ(sum, 6);
}

// ---- non-trivial types ----

VE_TEST(sv_string) {
    SmallVector<std::string, 2> v;
    v.push_back("hello");
    v.push_back("world");
    v.push_back("overflow"); // heap
    VE_ASSERT_EQ(v.size(), 3u);
    VE_ASSERT_EQ(v[0], "hello");
    VE_ASSERT_EQ(v[2], "overflow");
    v.erase(1);
    VE_ASSERT_EQ(v.size(), 2u);
    VE_ASSERT_EQ(v[0], "hello");
    VE_ASSERT_EQ(v[1], "overflow");
}

VE_TEST(sv_string_copy_move) {
    SmallVector<std::string, 1> a;
    a.push_back("test");
    a.push_back("data");
    SmallVector<std::string, 1> b(a); // copy
    VE_ASSERT_EQ(b[0], "test");
    VE_ASSERT_EQ(b[1], "data");
    SmallVector<std::string, 1> c(std::move(b)); // move
    VE_ASSERT_EQ(c[0], "test");
    VE_ASSERT(b.empty());
}

// ---- pointer type (primary use case: Node*) ----

VE_TEST(sv_pointer_single) {
    int x = 42;
    SmallVector<int*, 1> v;
    v.push_back(&x);
    VE_ASSERT_EQ(v.size(), 1u);
    VE_ASSERT_EQ(*v[0], 42);
    // should be inline (capacity == 1, no heap alloc)
    VE_ASSERT_EQ(v.capacity(), 1u);
}

VE_TEST(sv_pointer_grow) {
    int a = 1, b = 2, c = 3;
    SmallVector<int*, 1> v;
    v.push_back(&a);
    v.push_back(&b); // overflow to heap
    v.push_back(&c);
    VE_ASSERT_EQ(v.size(), 3u);
    VE_ASSERT_EQ(*v[0], 1);
    VE_ASSERT_EQ(*v[1], 2);
    VE_ASSERT_EQ(*v[2], 3);
}

// ---- emplace_back ----

VE_TEST(sv_emplace_back) {
    SmallVector<std::string, 2> v;
    v.emplace_back("hello");
    v.emplace_back(5, 'x'); // string of 5 'x' chars
    VE_ASSERT_EQ(v[0], "hello");
    VE_ASSERT_EQ(v[1], "xxxxx");
}

// ---- insert (positional) ----

VE_TEST(sv_insert_begin) {
    SmallVector<int, 4> v = {2, 3, 4};
    v.insert(v.begin(), 1);
    VE_ASSERT_EQ(v.size(), 4u);
    VE_ASSERT_EQ(v[0], 1);
    VE_ASSERT_EQ(v[1], 2);
    VE_ASSERT_EQ(v[3], 4);
}

VE_TEST(sv_insert_middle) {
    SmallVector<int, 4> v = {1, 3};
    v.insert(v.begin() + 1, 2);
    VE_ASSERT_EQ(v.size(), 3u);
    VE_ASSERT_EQ(v[0], 1);
    VE_ASSERT_EQ(v[1], 2);
    VE_ASSERT_EQ(v[2], 3);
}

VE_TEST(sv_insert_end) {
    SmallVector<int, 4> v = {1, 2};
    v.insert(v.end(), 3);
    VE_ASSERT_EQ(v.size(), 3u);
    VE_ASSERT_EQ(v[2], 3);
}

VE_TEST(sv_insert_triggers_grow) {
    SmallVector<int, 1> v = {10};
    v.insert(v.begin(), 5); // triggers heap growth
    VE_ASSERT_EQ(v.size(), 2u);
    VE_ASSERT_EQ(v[0], 5);
    VE_ASSERT_EQ(v[1], 10);
}

VE_TEST(sv_insert_string) {
    SmallVector<std::string, 2> v;
    v.push_back("a");
    v.push_back("c");
    v.insert(v.begin() + 1, "b");
    VE_ASSERT_EQ(v.size(), 3u);
    VE_ASSERT_EQ(v[0], "a");
    VE_ASSERT_EQ(v[1], "b");
    VE_ASSERT_EQ(v[2], "c");
}

VE_TEST(sv_erase_iterator) {
    SmallVector<int, 4> v = {10, 20, 30, 40};
    v.erase(v.begin() + 1); // erase 20
    VE_ASSERT_EQ(v.size(), 3u);
    VE_ASSERT_EQ(v[0], 10);
    VE_ASSERT_EQ(v[1], 30);
}

// ---- shrink_to_fit ----

VE_TEST(sv_shrink_to_fit) {
    SmallVector<int, 2> v;
    for (int i = 0; i < 100; ++i) v.push_back(i);
    VE_ASSERT(v.capacity() >= 100u);
    v.clear();
    v.push_back(1);
    v.shrink_to_fit();
    // should be back to inline storage
    VE_ASSERT(v.is_inline());
    VE_ASSERT_EQ(v[0], 1);
}

// ---- comparison ----

VE_TEST(sv_equality) {
    SmallVector<int, 2> a = {1, 2, 3};
    SmallVector<int, 2> b = {1, 2, 3};
    SmallVector<int, 2> c = {1, 2, 4};
    VE_ASSERT(a == b);
    VE_ASSERT(a != c);
}

// ---- PrivateTContainerBase mixin ----

VE_TEST(sv_mixin_sizeAsInt) {
    SmallVector<int, 2> v = {1, 2, 3};
    VE_ASSERT_EQ(v.sizeAsInt(), 3);
}

VE_TEST(sv_mixin_has) {
    SmallVector<int, 2> v = {10, 20};
    VE_ASSERT(v.has(0));
    VE_ASSERT(v.has(1));
    VE_ASSERT(!v.has(2));
    VE_ASSERT(!v.has(-1));
}

VE_TEST(sv_mixin_value) {
    SmallVector<int, 2> v = {10, 20};
    VE_ASSERT_EQ(v.value(0), 10);
    VE_ASSERT_EQ(v.value(1), 20);
    VE_ASSERT_EQ(v.value(5), 0);  // out of range → default
    VE_ASSERT_EQ(v.value(5, -1), -1);  // out of range → explicit default
}

VE_TEST(sv_mixin_insertOne) {
    SmallVector<int, 4> v = {1, 3, 4};
    v.insertOne(1, 2);  // insert 2 at position 1
    VE_ASSERT_EQ(v.sizeAsInt(), 4);
    VE_ASSERT_EQ(v[0], 1);
    VE_ASSERT_EQ(v[1], 2);
    VE_ASSERT_EQ(v[2], 3);
    VE_ASSERT_EQ(v[3], 4);
}

VE_TEST(sv_mixin_prepend) {
    SmallVector<int, 4> v = {2, 3};
    v.prepend(1);
    VE_ASSERT_EQ(v[0], 1);
    VE_ASSERT_EQ(v[1], 2);
    VE_ASSERT_EQ(v[2], 3);
}

VE_TEST(sv_mixin_append) {
    SmallVector<int, 2> v;
    v.append(1).append(2).append(3);
    VE_ASSERT_EQ(v.sizeAsInt(), 3);
    VE_ASSERT_EQ(v[2], 3);
}

VE_TEST(sv_mixin_every) {
    SmallVector<int, 4> v = {1, 2, 3};
    int sum = 0;
    v.every([&](int x) { sum += x; });
    VE_ASSERT_EQ(sum, 6);
}

VE_TEST(sv_mixin_toString) {
    SmallVector<int, 4> v = {1, 2, 3};
    VE_ASSERT_EQ(v.toString(","), "1,2,3");
    VE_ASSERT_EQ(v.toString(" "), "1 2 3");
}

// ---- ListLike trait ----

VE_TEST(sv_listlike_trait) {
    VE_ASSERT((SmallVector<int, 1>::ListLike::value));
}

// ---- stress ----

VE_TEST(sv_stress_10k) {
    SmallVector<int, 1> v;
    for (int i = 0; i < 10000; ++i) v.push_back(i);
    VE_ASSERT_EQ(v.size(), 10000u);
    VE_ASSERT_EQ(v[0], 0);
    VE_ASSERT_EQ(v[9999], 9999);
    // erase from middle
    v.erase(5000);
    VE_ASSERT_EQ(v.size(), 9999u);
    VE_ASSERT_EQ(v[5000], 5001);
}

VE_TEST(sv_stress_insert_front) {
    SmallVector<int, 1> v;
    for (int i = 0; i < 1000; ++i) v.prepend(i);
    VE_ASSERT_EQ(v.sizeAsInt(), 1000);
    // prepend inserts at front → reversed order
    VE_ASSERT_EQ(v[0], 999);
    VE_ASSERT_EQ(v[999], 0);
}
