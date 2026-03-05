#include "ve/ros/core/math_service.h"

namespace hemera::service {

template<>
bool convert(const Eigen::Vector3d& v, Eigen::Quaterniond& q)
{
    Eigen::AngleAxisd r(v.x(), Eigen::Vector3d::UnitX());
    Eigen::AngleAxisd p(v.y(), Eigen::Vector3d::UnitY());
    Eigen::AngleAxisd y(v.z(), Eigen::Vector3d::UnitZ());
    q = y * p * r;
    return true;
}

template<>
bool convert(const Eigen::Matrix3d& r, Eigen::Quaterniond& q)
{
    Eigen::Vector3d rpy = r.eulerAngles(2, 1, 0);
    std::swap(rpy.x(), rpy.z());
    return convert(rpy, q);
}

}
