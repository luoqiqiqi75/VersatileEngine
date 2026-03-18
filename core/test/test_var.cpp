// ----------------------------------------------------------------------------
// test_var.cpp — ve::Var 测试
// ----------------------------------------------------------------------------

#include "ve_test.h"
#include "ve/core/var.h"
#include "ve/core/convert.h"

using namespace ve;

// ========== 基础类型构造和转换测试 ==========

VE_TEST(var_construct_null) {
    Var v;
    VE_ASSERT(v.isNull());
    VE_ASSERT(v.type() == Var::NONE);
}

VE_TEST(var_construct_bool) {
    Var v1(true);
    VE_ASSERT(v1.isBool());
    VE_ASSERT(v1.toBool() == true);
    
    Var v2(false);
    VE_ASSERT(v2.isBool());
    VE_ASSERT(v2.toBool() == false);
}

VE_TEST(var_construct_int) {
    Var v1(42);
    VE_ASSERT(v1.isInt());
    VE_ASSERT_EQ(v1.toInt(), 42);
    
    Var v2(-100);
    VE_ASSERT(v2.isInt());
    VE_ASSERT_EQ(v2.toInt(), -100);
}

VE_TEST(var_construct_int64) {
    Var v(static_cast<std::int64_t>(1234567890LL));
    VE_ASSERT(v.isInt());
    VE_ASSERT_EQ(v.toInt(), 1234567890);
}

VE_TEST(var_construct_double) {
    Var v(3.14);
    VE_ASSERT(v.isDouble());
    VE_ASSERT_NEAR(v.toDouble(), 3.14, 0.001);
}

VE_TEST(var_construct_string) {
    Var v1("hello");
    VE_ASSERT(v1.isString());
    VE_ASSERT_EQ(v1.toString(), "hello");
    
    Var v2(std::string("world"));
    VE_ASSERT(v2.isString());
    VE_ASSERT_EQ(v2.toString(), "world");
}

VE_TEST(var_construct_bytes) {
    Bytes bytes = {0x01, 0x02, 0x03, 0xFF};
    Var v(bytes);
    VE_ASSERT(v.isBin());
    Bytes result = v.toBin();
    VE_ASSERT_EQ(result.size(), 4);
    VE_ASSERT_EQ(result[0], 0x01);
    VE_ASSERT_EQ(result[3], 0xFF);
}

VE_TEST(var_construct_pointer) {
    int x = 42;
    void* ptr = &x;
    Var v(ptr);
    VE_ASSERT(v.isPointer());
    VE_ASSERT_EQ(v.toPointer(), ptr);
}

// ========== from 系列方法测试 ==========

VE_TEST(var_from_bool) {
    Var v;
    v.fromBool(true);
    VE_ASSERT(v.isBool());
    VE_ASSERT_EQ(v.toBool(), true);
    
    v.fromBool(false);
    VE_ASSERT(v.isBool());
    VE_ASSERT_EQ(v.toBool(), false);
}

VE_TEST(var_from_int) {
    Var v;
    v.fromInt(100);
    VE_ASSERT(v.isInt());
    VE_ASSERT_EQ(v.toInt(), 100);
    
    v.fromInt(-50);
    VE_ASSERT(v.isInt());
    VE_ASSERT_EQ(v.toInt(), -50);
}

VE_TEST(var_from_int64) {
    Var v;
    v.fromInt64(9876543210LL);
    VE_ASSERT(v.isInt());
    // 注意：toInt() 返回 int，可能截断，这里只测试类型正确性
    VE_ASSERT(v.isInt());
}

VE_TEST(var_from_double) {
    Var v;
    v.fromDouble(2.718);
    VE_ASSERT(v.isDouble());
    VE_ASSERT_NEAR(v.toDouble(), 2.718, 0.001);
}

VE_TEST(var_from_string) {
    Var v;
    v.fromString("test");
    VE_ASSERT(v.isString());
    VE_ASSERT_EQ(v.toString(), "test");
    
    std::string s = "another";
    v.fromString(s);
    VE_ASSERT(v.isString());
    VE_ASSERT_EQ(v.toString(), "another");
    
    v.fromString(std::string("moved"));
    VE_ASSERT(v.isString());
    VE_ASSERT_EQ(v.toString(), "moved");
}

VE_TEST(var_from_bin) {
    Var v;
    Bytes bytes = {0xAA, 0xBB, 0xCC};
    v.fromBin(bytes);
    VE_ASSERT(v.isBin());
    Bytes result = v.toBin();
    VE_ASSERT_EQ(result.size(), 3);
    VE_ASSERT_EQ(result[0], 0xAA);
}

VE_TEST(var_from_list) {
    Var v;
    Var::ListV list;
    list.push_back(Var(1));
    list.push_back(Var(2));
    list.push_back(Var(3));
    
    v.fromList(list);
    VE_ASSERT(v.isList());
    const auto& result = v.toList();
    VE_ASSERT_EQ(result.size(), 3);
    VE_ASSERT_EQ(result[0].toInt(), 1);
    VE_ASSERT_EQ(result[2].toInt(), 3);
}

VE_TEST(var_from_dict) {
    Var v;
    Var::DictV dict;
    dict["a"] = Var(1);
    dict["b"] = Var(2);
    dict["c"] = Var(3);
    
    v.fromDict(dict);
    VE_ASSERT(v.isDict());
    const auto& result = v.toDict();
    VE_ASSERT_EQ(result.size(), 3);
    VE_ASSERT_EQ(result["a"].toInt(), 1);
    VE_ASSERT_EQ(result["b"].toInt(), 2);
    VE_ASSERT_EQ(result["c"].toInt(), 3);
}

VE_TEST(var_from_pointer) {
    Var v;
    int x = 99;
    void* ptr = &x;
    v.fromPointer(ptr);
    VE_ASSERT(v.isPointer());
    VE_ASSERT_EQ(v.toPointer(), ptr);
}

// ========== 类型转换测试 ==========

VE_TEST(var_convert_int_to_string) {
    Var v(42);
    VE_ASSERT_EQ(v.toString(), "42");
}

VE_TEST(var_convert_string_to_int) {
    Var v("123");
    VE_ASSERT_EQ(v.toInt(), 123);
    
    Var v2("abc");
    VE_ASSERT_EQ(v2.toInt(-1), -1); // 转换失败返回默认值
}

VE_TEST(var_convert_double_to_int) {
    Var v(3.14);
    VE_ASSERT_EQ(v.toInt(), 3);
}

VE_TEST(var_convert_int_to_double) {
    Var v(42);
    VE_ASSERT_NEAR(v.toDouble(), 42.0, 0.001);
}

VE_TEST(var_convert_bool_to_int) {
    Var v(true);
    VE_ASSERT_EQ(v.toInt(), 1);
    
    Var v2(false);
    VE_ASSERT_EQ(v2.toInt(), 0);
}

VE_TEST(var_convert_int_to_bool) {
    Var v(0);
    VE_ASSERT_EQ(v.toBool(), false);
    
    Var v2(42);
    VE_ASSERT_EQ(v2.toBool(), true);
}

// ========== 通用转换测试 ==========

VE_TEST(var_to_template) {
    Var v(42);
    int i = v.to<int>();
    VE_ASSERT_EQ(i, 42);
    
    double d = v.to<double>();
    VE_ASSERT_NEAR(d, 42.0, 0.001);
    
    std::string s = v.to<std::string>();
    VE_ASSERT_EQ(s, "42");
}

VE_TEST(var_from_template) {
    Var v;
    v.from(100);
    VE_ASSERT(v.isInt());
    VE_ASSERT_EQ(v.toInt(), 100);
    
    v.from(3.14);
    VE_ASSERT(v.isDouble());
    VE_ASSERT_NEAR(v.toDouble(), 3.14, 0.001);
    
    v.from(std::string("hello"));
    VE_ASSERT(v.isString());
    VE_ASSERT_EQ(v.toString(), "hello");
}

// ========== List 和 Dict 测试 ==========

VE_TEST(var_list_operations) {
    Var::ListV list;
    list.push_back(Var(1));
    list.push_back(Var(2));
    list.push_back(Var(3));
    
    Var v(list);
    VE_ASSERT(v.isList());
    auto& result = v.toList();
    VE_ASSERT_EQ(result.size(), 3);
    
    result.push_back(Var(4));
    VE_ASSERT_EQ(result.size(), 4);
    VE_ASSERT_EQ(result[3].toInt(), 4);
}

VE_TEST(var_dict_operations) {
    Var::DictV dict;
    dict["name"] = Var("test");
    dict["value"] = Var(42);
    
    Var v(dict);
    VE_ASSERT(v.isDict());
    auto& result = v.toDict();
    VE_ASSERT_EQ(result.size(), 2);
    VE_ASSERT_EQ(result["name"].toString(), "test");
    VE_ASSERT_EQ(result["value"].toInt(), 42);
    
    result["new"] = Var("added");
    VE_ASSERT_EQ(result.size(), 3);
}

// ========== 比较操作测试 ==========

VE_TEST(var_compare_equal) {
    Var v1(42);
    Var v2(42);
    VE_ASSERT(v1 == v2);
    
    Var v3("hello");
    Var v4("hello");
    VE_ASSERT(v3 == v4);
}

VE_TEST(var_compare_not_equal) {
    Var v1(42);
    Var v2(43);
    VE_ASSERT(v1 != v2);
    
    Var v3("hello");
    Var v4("world");
    VE_ASSERT(v3 != v4);
}

VE_TEST(var_compare_different_types) {
    Var v1(42);
    Var v2("42");
    VE_ASSERT(v1 != v2); // 类型不同，不相等
}

// ========== swap 测试 ==========

VE_TEST(var_swap_same_type) {
    Var v1(42);
    Var v2(100);
    
    v1.swap(v2);
    VE_ASSERT_EQ(v1.toInt(), 100);
    VE_ASSERT_EQ(v2.toInt(), 42);
}

VE_TEST(var_swap_different_types) {
    Var v1(42);
    Var v2("hello");
    
    v1.swap(v2);
    VE_ASSERT(v1.isString());
    VE_ASSERT_EQ(v1.toString(), "hello");
    VE_ASSERT(v2.isInt());
    VE_ASSERT_EQ(v2.toInt(), 42);
}

VE_TEST(var_swap_list) {
    Var::ListV list1, list2;
    list1.push_back(Var(1));
    list2.push_back(Var(2));
    
    Var v1(list1);
    Var v2(list2);
    
    v1.swap(v2);
    VE_ASSERT_EQ(v1.toList()[0].toInt(), 2);
    VE_ASSERT_EQ(v2.toList()[0].toInt(), 1);
}

// ========== 拷贝和移动测试 ==========

VE_TEST(var_copy) {
    Var v1(42);
    Var v2(v1);
    VE_ASSERT_EQ(v1.toInt(), 42);
    VE_ASSERT_EQ(v2.toInt(), 42);
    
    v2.fromInt(100);
    VE_ASSERT_EQ(v1.toInt(), 42); // 原对象不受影响
    VE_ASSERT_EQ(v2.toInt(), 100);
}

VE_TEST(var_move) {
    Var v1(42);
    Var v2(std::move(v1));
    VE_ASSERT(v1.isNull()); // 移动后原对象为 Null
    VE_ASSERT_EQ(v2.toInt(), 42);
}

VE_TEST(var_copy_assignment) {
    Var v1(42);
    Var v2;
    v2 = v1;
    VE_ASSERT_EQ(v1.toInt(), 42);
    VE_ASSERT_EQ(v2.toInt(), 42);
}

VE_TEST(var_move_assignment) {
    Var v1(42);
    Var v2;
    v2 = std::move(v1);
    VE_ASSERT(v1.isNull());
    VE_ASSERT_EQ(v2.toInt(), 42);
}

// ========== 链式调用测试 ==========

VE_TEST(var_chaining) {
    Var v;
    v.fromInt(10).fromDouble(3.14).fromString("test");
    VE_ASSERT(v.isString());
    VE_ASSERT_EQ(v.toString(), "test");
}

// ========== 边界情况测试 ==========

VE_TEST(var_empty_string) {
    Var v("");
    VE_ASSERT(v.isString());
    VE_ASSERT_EQ(v.toString(), "");
    VE_ASSERT_EQ(v.toBool(), false);
}

VE_TEST(var_zero_values) {
    Var v1(0);
    VE_ASSERT_EQ(v1.toBool(), false);
    VE_ASSERT_EQ(v1.toInt(), 0);
    
    Var v2(0.0);
    VE_ASSERT_EQ(v2.toBool(), false);
    VE_ASSERT_NEAR(v2.toDouble(), 0.0, 0.001);
}

VE_TEST(var_invalid_conversion) {
    Var v("not_a_number");
    VE_ASSERT_EQ(v.toInt(-1), -1); // 转换失败返回默认值
    VE_ASSERT_NEAR(v.toDouble(-1.0), -1.0, 0.001);
}

// ========== sizeof 验证（堆指针优化后）==========

VE_TEST(var_sizeof_optimized) {
    // Type (uint8_t) + padding + union (max pointer/int64/double = 8 bytes)
    // Expected: 16 bytes on 64-bit platforms
    VE_ASSERT(sizeof(Var) <= 24);
    std::cout << "        sizeof(Var) = " << sizeof(Var) << " bytes\n";
}

// ========== Custom 类型测试 ==========

VE_TEST(var_custom_type) {
    struct Point { double x, y; };
    Var v = Var::custom(Point{1.0, 2.0});
    VE_ASSERT(v.isCustom());
    VE_ASSERT(v.customIs<Point>());
    auto* p = v.customPtr<Point>();
    VE_ASSERT(p != nullptr);
    VE_ASSERT_NEAR(p->x, 1.0, 0.001);
    VE_ASSERT_NEAR(p->y, 2.0, 0.001);
}

VE_TEST(var_custom_copy) {
    Var v1 = Var::custom(std::string("custom_data"));
    Var v2 = v1;
    VE_ASSERT(v2.isCustom());
    auto* p = v2.customPtr<std::string>();
    VE_ASSERT(p != nullptr);
    VE_ASSERT_EQ(*p, "custom_data");
}

VE_TEST(var_custom_move) {
    Var v1 = Var::custom(42);
    Var v2 = std::move(v1);
    VE_ASSERT(v1.isNull());
    VE_ASSERT(v2.isCustom());
    VE_ASSERT_EQ(*v2.customPtr<int>(), 42);
}

// ========== as<T>() 模板取值测试 ==========

VE_TEST(var_as_basic_types) {
    VE_ASSERT_EQ(Var(42).as<int>(), 42);
    VE_ASSERT_EQ(Var(true).as<bool>(), true);
    VE_ASSERT_NEAR(Var(3.14).as<double>(), 3.14, 0.001);
    VE_ASSERT_EQ(Var("hello").as<std::string>(), "hello");
}

VE_TEST(var_as_pointer) {
    int x = 0;
    Var v(static_cast<void*>(&x));
    VE_ASSERT_EQ(v.as<int*>(), &x);
}

VE_TEST(var_as_var) {
    Var v(42);
    Var v2 = v.as<Var>();
    VE_ASSERT_EQ(v2.toInt(), 42);
}
