// ----------------------------------------------------------------------------
// data.h
// ----------------------------------------------------------------------------
// Copyright (c) 2023-present Thilo and VersatileEngine contributors.
// Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
// See LICENSE file in the project root for full license information.
// ----------------------------------------------------------------------------

#pragma once

#include "ve/global.h"

#include "imol/core/modulemanager.h"

#define VE_DATA_PATH_SEPARATOR "."

#define PRIVATE_VE_DATA_DECLARE_MULTIPLE_SUB_PATH_WITH_CONTEXT(Func) \
template<typename... Ts> inline Data* Func(QObject* context, const QString& path, const QString& sub_path, Ts&&... other_paths) \
{ return Func(context, path + VE_DATA_PATH_SEPARATOR + sub_path, std::forward<Ts>(other_paths)...); }
#define PRIVATE_VE_DATA_DECLARE_MULTIPLE_SUB_PATH(Func) \
template<typename... Ts> inline Data* Func(const QString& path, const QString& sub_path, Ts&&... other_paths) \
{ return Func(path + VE_DATA_PATH_SEPARATOR + sub_path, std::forward<Ts>(other_paths)...); }

namespace ve {

namespace flags {

inline bool get(int flags, int f) { return (flags & f) == f; }
inline int set(int& flags, int f, bool on_off) { return flags = on_off ? flags | f : flags & ~f; }

}

using Data = imol::ModuleObject;
using DataListener = imol::ModuleObject;

namespace data {

using Manager = imol::ModuleManager;
VE_API Manager& manager();

template<typename T> inline T path(const T& t) { return t; }
template<typename T, typename T1, typename... Ts> inline T path(const T& t, const T1& t1, const Ts&... ts)
{ if (t1 == "") return path(t, ts...); else return t + VE_DATA_PATH_SEPARATOR + path(t1, ts...); }

VE_API Data* create(QObject* context, const QString& path);
PRIVATE_VE_DATA_DECLARE_MULTIPLE_SUB_PATH_WITH_CONTEXT(create)
VE_API bool free(QObject* context, const QString& path);
PRIVATE_VE_DATA_DECLARE_MULTIPLE_SUB_PATH_WITH_CONTEXT(free)

VE_API Data* at(const QString& path);
PRIVATE_VE_DATA_DECLARE_MULTIPLE_SUB_PATH(at)

VE_API DataListener* listenerAt(const QString& path);
PRIVATE_VE_DATA_DECLARE_MULTIPLE_SUB_PATH(listenerAt)

}

VE_API Data* d(Data* root, const std::string& path);
VE_API Data* d(const std::string& path);
VE_API Data* d(const char* path);
VE_API Data* d(Data* root, const QString& path);
VE_API Data* d(const QString& path);
VE_API Data* d(Data* root, const char* path);

}

#define VE_D(...) [] { static ve::Data* static_d = ve::d(__VA_ARGS__); return static_d; }()