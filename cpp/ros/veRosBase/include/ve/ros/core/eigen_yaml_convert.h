///////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2021
// Licensed under the Apache License, Version 2.0
//////////////////////////////////////////////////////////////////////////////

#pragma once

#include <Eigen/Dense>
#include "yaml-cpp/yaml.h"

namespace YAML {

template<typename _Scalar, int _Rows, int _Cols, int _Options, int _MaxRows, int _MaxCols>
struct convert<Eigen::Matrix<_Scalar, _Rows, _Cols, _Options, _MaxRows, _MaxCols>>
{
    using EigenT = Eigen::Matrix<_Scalar, _Rows, _Cols, _Options, _MaxRows, _MaxCols>;

    static Node encode(const EigenT& e)
    {
        Node n;
        for (int i = 0; i < e.rows(); i++) {
            auto rn = n[i];
            for (int j = 0; j < e.cols(); j++) {
                rn[j] = e(i, j);
            }
        }
        return n;
    }

    static bool decode(const Node& n, EigenT& e)
    {
        auto r = e.rows(), c = e.cols();
        try {
            if (!n.IsSequence() || n.size() < r) return false;
            for (int i = 0; i < r; i++) {
                auto rn = n[i];
                if (!rn.IsSequence() || rn.size() < c) return false;
                for (int j = 0; j < c; j++) {
                    e(i, j) = rn[j].template as<_Scalar>();
                }
            }
        } catch (const std::exception& e) {
            return false;
        }
        return true;
    }
};

template<typename _Scalar, int _Rows, int _Options, int _MaxRows, int _MaxCols>
struct convert<Eigen::Matrix<_Scalar, _Rows, 1, _Options, _MaxRows, _MaxCols>>
{
    using EigenT = Eigen::Matrix<_Scalar, _Rows, 1, _Options, _MaxRows, _MaxCols>;

    static Node encode(const EigenT& e)
    {
        Node n;
        for (int i = 0; i < e.rows(); i++) {
            n[i] = e(i, 0);
        }
        return n;
    }

    static bool decode(const Node& n, EigenT& e)
    {
        auto r = e.rows();
        try {
            if (!n.IsSequence() || n.size() < r) return false;
            for (int i = 0; i < r; i++) {
                e(i, 0) = n[i].template as<_Scalar>();
            }
        } catch (const std::exception& e) {
            return false;
        }
        return true;
    }
};

template<typename _Scalar, int _Cols, int _Options, int _MaxRows, int _MaxCols>
struct convert<Eigen::Matrix<_Scalar, 1, _Cols, _Options, _MaxRows, _MaxCols>>
{
    using EigenT = Eigen::Matrix<_Scalar, 1, _Cols, _Options, _MaxRows, _MaxCols>;

    static Node encode(const EigenT& e)
    {
        Node n;
        for (int i = 0; i < e.cols(); i++) {
            n[i] = e(0, i);
        }
        return n;
    }

    static bool decode(const Node& n, EigenT& e)
    {
        auto c = e.cols();
        try {
            if (!n.IsSequence() || n.size() < c) return false;
            for (int i = 0; i < c; i++) {
                e(0, i) = n[i].template as<_Scalar>();
            }
        } catch (const std::exception& e) {
            return false;
        }
        return true;
    }
};

template<typename _Scalar, int _Options>
struct convert<Eigen::Quaternion<_Scalar, _Options>>
{
    using EigenT = Eigen::Quaternion<_Scalar, _Options>;

    static Node encode(const EigenT& e)
    {
        Node n;
        n[0] = e.x();
        n[1] = e.y();
        n[2] = e.z();
        n[3] = e.w();
        return n;
    }

    static bool decode(const Node& n, EigenT& e)
    {
        if (!n.IsSequence()) return false;
        if (n.size() == 4) {
            e.x() = n[0].template as<_Scalar>();
            e.y() = n[1].template as<_Scalar>();
            e.z() = n[2].template as<_Scalar>();
            e.w() = n[3].template as<_Scalar>();
        } else if (n.size() == 3) {
            Eigen::AngleAxis<_Scalar> r(n[0].template as<_Scalar>(), Eigen::Matrix<_Scalar, 3, 1>::UnitX());
            Eigen::AngleAxis<_Scalar> p(n[1].template as<_Scalar>(), Eigen::Matrix<_Scalar, 3, 1>::UnitY());
            Eigen::AngleAxis<_Scalar> y(n[2].template as<_Scalar>(), Eigen::Matrix<_Scalar, 3, 1>::UnitZ());
            e = y * p * r;
        } else {
            return false;
        }
        return true;
    }
};

}
