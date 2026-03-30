#include "ve/core/base.h"

#if defined(__clang__) && defined(__has_include)
#if __has_include(<cxxabi.h>)
#define PRIVATE_HAS_CXXABI
#endif
#elif defined(__GLIBCXX__) || defined(__GLIBCPP__)
#define PRIVATE_HAS_CXXABI
#endif

#ifdef PRIVATE_HAS_CXXABI
#include <cxxabi.h>
#include <cstdlib>
#include <cstddef>
#endif

#include <cmath>

namespace ve {

constexpr double eps = 0.000001;
const double pi = 3.1415920;
const double deg2rad = pi / 180;
const double rad2deg = 180 / pi;

namespace basic {
std::string _t_demangle(const char *type_name)
{
#ifdef PRIVATE_HAS_CXXABI
    int status = 0;
    std::size_t size = 0;
    const char* demangle_name = abi::__cxa_demangle(type_name, NULL, &size, &status);
#else
    const char* demangle_name = type_name;
#endif
    std::string s(demangle_name);
#ifdef PRIVATE_HAS_CXXABI
    std::free((void*)demangle_name);
#endif
    return s;
}
}

Values::Unit Values::unit() const { return m_unit; }
Values& Values::setUnit(Unit unit) { if (m_unit != SAME) m_unit = unit; return *this; }

Values& Values::add(double d)
{
    std::for_each(begin(), end(), [=] (double& it) { it += d; });
    return *this;
}

Values& Values::multiply(double d, Values::Unit new_unit)
{
    std::for_each(begin(), end(), [=] (double& it) { it *= d; });
    return setUnit(new_unit);
}

Values& Values::m2mm() { return multiply(1000.0, MM); }
Values& Values::mm2m() { return multiply(0.001, M); }
Values& Values::degree2rad() { return multiply(deg2rad, RAD); }
Values& Values::rad2degree() { return multiply(rad2deg, DEGREE); }

bool Values::smallerThan(const Values& other) const
{
    for (int i = 0; i < std::min(sizeAsInt(), other.sizeAsInt()); i++) {
        if (at(i) >= other.at(i)) return false;
    }
    return true;
}

bool Values::greaterThan(const Values &other) const
{
    for (int i = 0; i < std::min(sizeAsInt(), other.sizeAsInt()); i++) {
        if (at(i) <= other.at(i)) return false;
    }
    return true;
}

Values& Values::operator+=(const Values& o)
{
    const int n = std::min(sizeAsInt(), o.sizeAsInt());
    for (int i = 0; i < n; ++i) {
        at(i) += o.at(i);
    }
    return *this;
}

Values& Values::operator-=(const Values& o)
{
    const int n = std::min(sizeAsInt(), o.sizeAsInt());
    for (int i = 0; i < n; ++i) {
        at(i) -= o.at(i);
    }
    return *this;
}

Values& Values::append(const Values& o)
{
    insert(end(), o.begin(), o.end());
    return *this;
}

double Values::sum() const
{
    double result = 0.0;
    for (const auto& v : *this) {
        result += v;
    }
    return result;
}

double Values::norm() const
{
    double result = 0.0;
    for (const auto& v : *this) {
        result += v * v;
    }
    return std::sqrt(result);
}

double Values::distance(const Values& o) const
{
    return (*this - o).norm();
}

bool Values::near(const Values& o, double epsilon) const
{
    return distance(o) < epsilon;
}

}
