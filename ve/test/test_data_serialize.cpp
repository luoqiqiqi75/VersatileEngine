// ----------------------------------------------------------------------------
// test_data_serialize.cpp — string/yaml serialization
// ----------------------------------------------------------------------------

#include "ve_test.h"
#include "ve/core/data.h"

using namespace ve;

// ==================== string serialization ====================

VE_TEST(serialize_int_toString) {
    std::string s;
    VE_ASSERT(serialize::toString(42, s));
    VE_ASSERT_EQ(s, "42");
}

VE_TEST(serialize_int_fromString) {
    int v = 0;
    VE_ASSERT(serialize::fromString("123", v));
    VE_ASSERT_EQ(v, 123);
}

VE_TEST(serialize_double_toString) {
    std::string s;
    VE_ASSERT(serialize::toString(3.14, s));
    VE_ASSERT(!s.empty());
}

VE_TEST(serialize_string_roundtrip) {
    std::string in = "hello world";
    std::string out;
    VE_ASSERT(serialize::toString(in, out));
    VE_ASSERT_EQ(out, "hello world");

    std::string back;
    VE_ASSERT(serialize::fromString(out, back));
    VE_ASSERT_EQ(back, in);
}

VE_TEST(serialize_vector_int_toString) {
    Vector<int> v;
    v.append(1).append(2).append(3);
    std::string s;
    VE_ASSERT(serialize::toString(v, s));
    VE_ASSERT_EQ(s, "[1, 2, 3]");
}

VE_TEST(serialize_dict_toString) {
    // Note: unordered_map iteration order is not guaranteed,
    // so we just check it starts with { and ends with }
    Dict<int> d;
    d["a"] = 1;
    std::string s;
    VE_ASSERT(serialize::toString(d, s));
    VE_ASSERT(s.front() == '{');
    VE_ASSERT(s.back() == '}');
}

// ==================== AnyData string serialization ====================

VE_TEST(anydata_int_fromString) {
    AnyData<int> d(0);
    d.control(AbstractData::CHANGEABLE);
    VE_ASSERT(d.fromString("42"));
    VE_ASSERT_EQ(d.get(), 42);
}

VE_TEST(anydata_int_toString) {
    AnyData<int> d(99);
    VE_ASSERT_EQ(d.toString(), "99");
}

VE_TEST(anydata_void_fromString) {
    AnyData<void> d;
    d.control(AbstractData::CHANGEABLE);
    VE_ASSERT(d.fromString("void"));
    VE_ASSERT(d.fromString(""));
    VE_ASSERT(!d.fromString("something"));
}

VE_TEST(anydata_void_toString) {
    AnyData<void> d;
    VE_ASSERT_EQ(d.toString(), "void");
}

