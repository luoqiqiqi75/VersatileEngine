#include <ve/rtt/container.h>

#include <cmath>

namespace imol {

constexpr double eps = 0.000001;
constexpr double pi = 3.1415920;
constexpr double deg2rad = pi / 180.0;
constexpr double rad2deg = 180.0 / pi;

Values::Unit Values::unit() const { return m_unit; }
Values& Values::setUnit(Unit u) { if (m_unit != SAME) m_unit = u; return *this; }

Values& Values::add(double d)
{
    std::for_each(begin(), end(), [=](double& it) { it += d; });
    return *this;
}

Values& Values::multiply(double d, Unit new_unit)
{
    std::for_each(begin(), end(), [=](double& it) { it *= d; });
    return setUnit(new_unit);
}

Values& Values::m2mm()       { return multiply(1000.0, MM); }
Values& Values::mm2m()       { return multiply(0.001, M); }
Values& Values::degree2rad() { return multiply(deg2rad, RAD); }
Values& Values::rad2degree() { return multiply(rad2deg, DEGREE); }

bool Values::smallerThan(const Values& other) const
{
    for (int i = 0; i < std::min(sizeAsInt(), other.sizeAsInt()); i++)
        if (at(i) >= other.at(i)) return false;
    return true;
}

bool Values::greaterThan(const Values& other) const
{
    for (int i = 0; i < std::min(sizeAsInt(), other.sizeAsInt()); i++)
        if (at(i) <= other.at(i)) return false;
    return true;
}

bool Values::equals(const Values& other) const
{
    if (sizeAsInt() != other.sizeAsInt()) return false;
    for (int i = 0; i < sizeAsInt(); i++)
        if (std::fabs(at(i) - other.at(i)) > eps) return false;
    return true;
}

} // namespace imol
