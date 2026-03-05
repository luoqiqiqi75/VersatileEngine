// ----------------------------------------------------------------------------
// test_values.cpp — ve::Values unit conversion
// ----------------------------------------------------------------------------

#include "ve_test.h"
#include "ve/core/base.h"

using namespace ve;

VE_TEST(values_m2mm) {
    Values v;
    v.append(1.0).append(2.0);
    v.setUnit(Values::M);
    v.m2mm();
    VE_ASSERT_NEAR(v[0], 1000.0, 0.001);
    VE_ASSERT_NEAR(v[1], 2000.0, 0.001);
    VE_ASSERT_EQ(static_cast<int>(v.unit()), static_cast<int>(Values::MM));
}

VE_TEST(values_mm2m) {
    Values v;
    v.append(1000.0);
    v.setUnit(Values::MM);
    v.mm2m();
    VE_ASSERT_NEAR(v[0], 1.0, 0.001);
    VE_ASSERT_EQ(static_cast<int>(v.unit()), static_cast<int>(Values::M));
}

VE_TEST(values_degree2rad) {
    Values v;
    v.append(180.0);
    v.setUnit(Values::DEGREE);
    v.degree2rad();
    VE_ASSERT_NEAR(v[0], 3.14159, 0.01);
    VE_ASSERT_EQ(static_cast<int>(v.unit()), static_cast<int>(Values::RAD));
}

VE_TEST(values_rad2degree) {
    Values v;
    v.append(3.14159);
    v.setUnit(Values::RAD);
    v.rad2degree();
    VE_ASSERT_NEAR(v[0], 180.0, 0.1);
    VE_ASSERT_EQ(static_cast<int>(v.unit()), static_cast<int>(Values::DEGREE));
}

VE_TEST(values_add) {
    Values v;
    v.append(1.0).append(2.0);
    v.add(10.0);
    VE_ASSERT_NEAR(v[0], 11.0, 0.001);
    VE_ASSERT_NEAR(v[1], 12.0, 0.001);
}

VE_TEST(values_multiply) {
    Values v;
    v.append(3.0).append(4.0);
    v.multiply(2.0);
    VE_ASSERT_NEAR(v[0], 6.0, 0.001);
    VE_ASSERT_NEAR(v[1], 8.0, 0.001);
}

VE_TEST(values_unit_getset) {
    Values v;
    VE_ASSERT_EQ(static_cast<int>(v.unit()), static_cast<int>(Values::NONE));
    v.setUnit(Values::M);
    VE_ASSERT_EQ(static_cast<int>(v.unit()), static_cast<int>(Values::M));
}

VE_TEST(values_equals) {
    Values a, b;
    a.append(1.0).append(2.0);
    b.append(1.0).append(2.0);
    VE_ASSERT(a == b);
}

VE_TEST(values_not_equals) {
    Values a, b;
    a.append(1.0);
    b.append(2.0);
    VE_ASSERT(!(a == b));
}

VE_TEST(values_smaller_greater) {
    Values a, b;
    a.append(1.0).append(2.0);
    b.append(3.0).append(4.0);
    VE_ASSERT(a < b);
    VE_ASSERT(b > a);
}
