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

VE_TEST(values_common_units) {
    Values v;
    v.setUnit(Values::M_PER_S2);
    VE_ASSERT_EQ(static_cast<int>(v.unit()), static_cast<int>(Values::M_PER_S2));
    v.setUnit(Values::RAD_PER_S);
    VE_ASSERT_EQ(static_cast<int>(v.unit()), static_cast<int>(Values::RAD_PER_S));
    v.setUnit(Values::NEWTON_METER);
    VE_ASSERT_EQ(static_cast<int>(v.unit()), static_cast<int>(Values::NEWTON_METER));
    v.setUnit(Values::CELSIUS);
    VE_ASSERT_EQ(static_cast<int>(v.unit()), static_cast<int>(Values::CELSIUS));
}

VE_TEST(values_common_unit_conversions) {
    Values v;
    v.append(1.0);
    v.setUnit(Values::SECOND);
    v.second2millisecond();
    VE_ASSERT_NEAR(v[0], 1000.0, 0.001);
    VE_ASSERT_EQ(static_cast<int>(v.unit()), static_cast<int>(Values::MILLISECOND));
    v.second2millisecond();
    VE_ASSERT_NEAR(v[0], 1000.0, 0.001);

    v.clear();
    v.append(36.0);
    v.setUnit(Values::KM_PER_H);
    v.kmph2mps();
    VE_ASSERT_NEAR(v[0], 10.0, 0.001);
    VE_ASSERT_EQ(static_cast<int>(v.unit()), static_cast<int>(Values::M_PER_S));

    v.clear();
    v.append(0.0);
    v.setUnit(Values::CELSIUS);
    v.celsius2kelvin();
    VE_ASSERT_NEAR(v[0], 273.15, 0.001);
    VE_ASSERT_EQ(static_cast<int>(v.unit()), static_cast<int>(Values::KELVIN));
    v.celsius2kelvin();
    VE_ASSERT_NEAR(v[0], 273.15, 0.001);
}

VE_TEST(values_optimistic_unit_helpers) {
    Values v;
    v.append(2.0);
    v.multiplyToUnit<Values::MM>(1000.0);
    VE_ASSERT_NEAR(v[0], 2000.0, 0.001);
    VE_ASSERT_EQ(static_cast<int>(v.unit()), static_cast<int>(Values::MM));
    v.multiplyToUnit<Values::MM>(1000.0);
    VE_ASSERT_NEAR(v[0], 2000.0, 0.001);

    v.addToUnit<Values::KELVIN>(273.15);
    VE_ASSERT_NEAR(v[0], 2273.15, 0.001);
    VE_ASSERT_EQ(static_cast<int>(v.unit()), static_cast<int>(Values::KELVIN));
    v.addToUnit<Values::KELVIN>(273.15);
    VE_ASSERT_NEAR(v[0], 2273.15, 0.001);
}

VE_TEST(values_conversion_pairs) {
    Values v;
    v.append(1.0);

    v.setUnit(Values::M);
    v.m2cm().cm2m();
    VE_ASSERT_NEAR(v[0], 1.0, 0.001);
    VE_ASSERT_EQ(static_cast<int>(v.unit()), static_cast<int>(Values::M));

    v.setUnit(Values::M_PER_S);
    v.mps2kmph().kmph2mps();
    VE_ASSERT_NEAR(v[0], 1.0, 0.001);

    v.setUnit(Values::M_PER_S2);
    v.mpss2gForce().gForce2mpss();
    VE_ASSERT_NEAR(v[0], 1.0, 0.001);

    v.setUnit(Values::RAD_PER_S);
    v.radPerS2rpm().rpm2radPerS();
    VE_ASSERT_NEAR(v[0], 1.0, 0.001);

    v.setUnit(Values::HZ);
    v.hz2mhz().mhz2hz();
    VE_ASSERT_NEAR(v[0], 1.0, 0.001);

    v.setUnit(Values::RATIO);
    v.ratio2percent().percent2ratio();
    VE_ASSERT_NEAR(v[0], 1.0, 0.001);
}

VE_TEST(values_multiply_preserves_unit) {
    Values v;
    v.append(2.0);
    v.setUnit(Values::NEWTON);
    v.multiply(3.0);
    VE_ASSERT_NEAR(v[0], 6.0, 0.001);
    VE_ASSERT_EQ(static_cast<int>(v.unit()), static_cast<int>(Values::NEWTON));
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

VE_TEST(values_dot) {
    Values a, b;
    a.append(1.0).append(2.0).append(3.0);
    b.append(4.0).append(5.0);
    VE_ASSERT_NEAR(a.dot(b), 14.0, 0.001);
}

VE_TEST(values_normalize) {
    Values v;
    v.append(3.0).append(4.0);
    v.normalize();
    VE_ASSERT_NEAR(v[0], 0.6, 0.001);
    VE_ASSERT_NEAR(v[1], 0.8, 0.001);
    VE_ASSERT_NEAR(v.norm(), 1.0, 0.001);

    Values zero;
    zero.append(0.0).append(0.0);
    zero.normalize();
    VE_ASSERT_NEAR(zero.norm(), 0.0, 0.001);
}

VE_TEST(values_clamp) {
    Values v;
    v.append(-2.0).append(0.5).append(3.0);
    v.clamp(0.0, 1.0);
    VE_ASSERT_NEAR(v[0], 0.0, 0.001);
    VE_ASSERT_NEAR(v[1], 0.5, 0.001);
    VE_ASSERT_NEAR(v[2], 1.0, 0.001);
}
