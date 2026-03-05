///////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2023
//////////////////////////////////////////////////////////////////////////////

#pragma once

#include "common.h"
#include "eigen_yaml_convert.h"

namespace hemera {

using EigenVector3 = Eigen::Vector3d;
inline EigenVector3 ZeroEigenVector3() { return EigenVector3().setZero(); }

template<int S> using EigenVector = Eigen::Matrix<double, S, 1>;
template<int S> inline EigenVector<S> ZeroEigenVector() { return EigenVector<S>().setZero(); }

using FrameM = Eigen::Matrix4d;
using FrameTR = std::pair<Eigen::Vector3d, Eigen::Matrix3d>;
using FrameTQ = std::pair<Eigen::Vector3d, Eigen::Quaterniond>;
using FrameTE = std::pair<Eigen::Vector3d, Eigen::Vector3d>;

inline FrameM ZeroFrameM() { return FrameM().setZero(); }
inline FrameTR ZeroFrameTR() { return FrameTR(ZeroEigenVector3(), Eigen::Matrix3d().setZero()); }
inline FrameTQ ZeroFrameTQ() { return FrameTQ(ZeroEigenVector3(), Eigen::Quaterniond(1, 0, 0, 0)); }
inline FrameTE ZeroFrameTE() { return FrameTE(ZeroEigenVector3(), ZeroEigenVector3()); }

using JntArr = Eigen::Matrix<double, 7, 1>;
using HandArr = ve::Array<double, 7>;

namespace service {

enum ConvertType : int {
    CONVERT_DEFAULT = 0
};
template<ConvertType CT = CONVERT_DEFAULT, typename FromT, typename ToT> bool convert(const FromT& f, ToT& t);
template<ConvertType CT = CONVERT_DEFAULT, typename FromT, typename ToT> inline ToT convert(const FromT& f) { ToT t; convert<CT>(f, t); return t; }

enum MathConvertType {

};

}
}
