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
#include <numeric>

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

Values::Unit Values::unit() const { return unit_; }
Values& Values::setUnit(Unit unit) { unit_ = unit; return *this; }

Values& Values::add(double d)
{
    std::for_each(begin(), end(), [=] (double& it) { it += d; });
    return *this;
}

Values& Values::multiply(double d)
{
    std::for_each(begin(), end(), [=] (double& it) { it *= d; });
    return *this;
}

bool Values::isFinite() const
{
    return std::all_of(begin(), end(), [] (double value) { return std::isfinite(value); });
}

bool Values::smallerThan(const Values& other) const
{
    const auto count = std::min(size(), other.size());
    return std::equal(begin(), begin() + count, other.begin(), std::less<>());
}

bool Values::greaterThan(const Values &other) const
{
    const auto count = std::min(size(), other.size());
    return std::equal(begin(), begin() + count, other.begin(), std::greater<>());
}

Values& Values::operator+=(const Values& o)
{
    const auto count = std::min(size(), o.size());
    std::transform(begin(), begin() + count, o.begin(), begin(), std::plus<>());
    return *this;
}

Values& Values::operator-=(const Values& o)
{
    const auto count = std::min(size(), o.size());
    std::transform(begin(), begin() + count, o.begin(), begin(), std::minus<>());
    return *this;
}

Values& Values::append(const Values& o)
{
    insert(end(), o.begin(), o.end());
    return *this;
}

double Values::sum() const
{
    return std::accumulate(begin(), end(), 0.0);
}

double Values::norm() const
{
    return std::sqrt(std::inner_product(begin(), end(), begin(), 0.0));
}

double Values::dot(const Values& o) const
{
    const auto count = std::min(size(), o.size());
    return std::inner_product(begin(), begin() + count, o.begin(), 0.0);
}

double Values::distance(const Values& o) const
{
    return (*this - o).norm();
}

bool Values::approximate(const Values& o, double epsilon) const
{
    return distance(o) < epsilon;
}

Values& Values::normalize()
{
    const double length = norm();
    if (length > 0.0) multiply(1.0 / length);
    return *this;
}

Values& Values::clamp(double min, double max)
{
    std::for_each(begin(), end(), [=] (double& value) {
        value = std::clamp(value, min, max);
    });
    return *this;
}

}
