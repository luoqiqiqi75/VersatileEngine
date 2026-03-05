// ----------------------------------------------------------------------------
// test_hashfuncs.cpp — ve::impl:: hash function correctness
// ----------------------------------------------------------------------------

#include "ve_test.h"
#include "ve/core/impl/hashfuncs.h"

using namespace ve::impl;

VE_TEST(hash_djb2_different_strings) {
    uint32_t h1 = hash_djb2("hello");
    uint32_t h2 = hash_djb2("world");
    VE_ASSERT_NE(h1, h2);
}

VE_TEST(hash_djb2_same_string) {
    uint32_t h1 = hash_djb2("test");
    uint32_t h2 = hash_djb2("test");
    VE_ASSERT_EQ(h1, h2);
}

VE_TEST(hash_djb2_empty_string) {
    uint32_t h = hash_djb2("");
    VE_ASSERT_EQ(h, 5381u);  // DJB2 initial value
}

VE_TEST(hash_fmix32_basic) {
    // fmix32 is a finalizer: 0 maps to 0 (identity for XOR+multiply chain)
    VE_ASSERT_EQ(hash_fmix32(0), 0u);
    // non-zero input should produce a different output (avalanche)
    VE_ASSERT_NE(hash_fmix32(1), 1u);
    // deterministic
    VE_ASSERT_EQ(hash_fmix32(42), hash_fmix32(42));
}

VE_TEST(hash_fmix32_deterministic) {
    VE_ASSERT_EQ(hash_fmix32(42), hash_fmix32(42));
}

VE_TEST(hash_murmur3_one_32_deterministic) {
    uint32_t a = hash_murmur3_one_32(100);
    uint32_t b = hash_murmur3_one_32(100);
    VE_ASSERT_EQ(a, b);
}

VE_TEST(hash_murmur3_one_32_different_input) {
    uint32_t a = hash_murmur3_one_32(1);
    uint32_t b = hash_murmur3_one_32(2);
    VE_ASSERT_NE(a, b);
}

VE_TEST(hash_murmur3_buffer_different_data) {
    const char* d1 = "abc";
    const char* d2 = "xyz";
    uint32_t h1 = hash_murmur3_buffer(d1, 3);
    uint32_t h2 = hash_murmur3_buffer(d2, 3);
    VE_ASSERT_NE(h1, h2);
}

VE_TEST(hash_murmur3_buffer_same_data) {
    const char* d = "test_data";
    uint32_t h1 = hash_murmur3_buffer(d, 9);
    uint32_t h2 = hash_murmur3_buffer(d, 9);
    VE_ASSERT_EQ(h1, h2);
}

VE_TEST(hash_one_uint64_different) {
    uint32_t a = hash_one_uint64(0);
    uint32_t b = hash_one_uint64(1);
    VE_ASSERT_NE(a, b);
}

// --- HashMapHasherDefault specializations ---

VE_TEST(hasher_default_int) {
    uint32_t h1 = HashMapHasherDefault::hash(42);
    uint32_t h2 = HashMapHasherDefault::hash(43);
    VE_ASSERT_NE(h1, h2);
    VE_ASSERT_EQ(HashMapHasherDefault::hash(42), h1);  // deterministic
}

VE_TEST(hasher_default_string) {
    uint32_t h1 = HashMapHasherDefault::hash(std::string("hello"));
    uint32_t h2 = HashMapHasherDefault::hash(std::string("world"));
    VE_ASSERT_NE(h1, h2);
}

VE_TEST(hasher_default_float_zero) {
    // +0.0 and -0.0 should hash the same
    uint32_t hp = HashMapHasherDefault::hash(0.0f);
    uint32_t hn = HashMapHasherDefault::hash(-0.0f);
    VE_ASSERT_EQ(hp, hn);
}

VE_TEST(hasher_default_float_nan) {
    float nan1 = std::nanf("1");
    float nan2 = std::nanf("2");
    uint32_t h1 = HashMapHasherDefault::hash(nan1);
    uint32_t h2 = HashMapHasherDefault::hash(nan2);
    VE_ASSERT_EQ(h1, h2);
}

VE_TEST(hasher_default_double_zero) {
    uint32_t hp = HashMapHasherDefault::hash(0.0);
    uint32_t hn = HashMapHasherDefault::hash(-0.0);
    VE_ASSERT_EQ(hp, hn);
}

// --- HashMapComparatorDefault ---

VE_TEST(comparator_default_int) {
    VE_ASSERT(HashMapComparatorDefault<int>::compare(5, 5));
    VE_ASSERT(!HashMapComparatorDefault<int>::compare(5, 6));
}

VE_TEST(comparator_default_float_tolerance) {
    // should be equal within 1e-6
    VE_ASSERT(HashMapComparatorDefault<float>::compare(1.0f, 1.0f + 1e-7f));
    VE_ASSERT(!HashMapComparatorDefault<float>::compare(1.0f, 1.0f + 1e-5f));
}

VE_TEST(comparator_default_double_tolerance) {
    VE_ASSERT(HashMapComparatorDefault<double>::compare(1.0, 1.0 + 1e-13));
    VE_ASSERT(!HashMapComparatorDefault<double>::compare(1.0, 1.0 + 1e-11));
}

// --- fastmod ---

VE_TEST(fastmod_consistency) {
    // fastmod should give same result as % for prime table sizes
    uint32_t cap = hash_table_size_primes[5];  // 193
    uint64_t cap_inv = hash_table_size_primes_inv[5];
    for (uint32_t i = 0; i < 1000; ++i) {
        VE_ASSERT_EQ(fastmod(i, cap_inv, cap), i % cap);
    }
}
