// ----------------------------------------------------------------------------
// global.h
// ----------------------------------------------------------------------------
// Copyright (c) 2023-present Thilo and VersatileEngine contributors.
// Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
// See LICENSE file in the project root for full license information.
// ----------------------------------------------------------------------------

#pragma once

#include <memory>
#include <thread>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <vector>
#include <array>
#include <list>
#include <set>
#include <map>
#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <future>

#if (!defined(VE_STATIC_LIBRARY) && (defined(_WIN32) || defined(_WIN64))) // shared as default
#ifdef VE_LIBRARY
#define VE_API __declspec(dllexport)
#else
#define VE_API __declspec(dllimport)
#endif
#else
#define VE_API
#endif

#define VE_DEF_STR(X) PRIVATE_VE_DEF_STR_IMPL(X)
#define PRIVATE_VE_DEF_STR_IMPL(X) #X
#define VE_DEF_COMBINE(X,Y) PRIVATE_VE_DEF_CONNECT_IMPL(X,Y)
#define PRIVATE_VE_DEF_COMBINE_IMPL(X,Y) X##Y

#define VE_DECLARE_PRIVATE struct Private; Private *_p;
#define VE_DECLARE_SHARED_PRIVATE struct Private; std::shared_ptr<Private> _p;
#define VE_DECLARE_UNIQUE_PRIVATE struct Private; std::unique_ptr<Private> _p;

#define PRIVATE_VE_AUTO_RUN_NAME(_PREFIX, _SUFFIX) PRIVATE_VE_AUTO_RUN_NAME_I(_PREFIX, _SUFFIX)
#define PRIVATE_VE_AUTO_RUN_NAME_I(_PREFIX, _SUFFIX) _PREFIX##_SUFFIX
#define PRIVATE_VE_AUTO_RUN_FUNC PRIVATE_VE_AUTO_RUN_NAME(_ve_auto_run_func_, __LINE__)
#define PRIVATE_VE_AUTO_RUN_VAR PRIVATE_VE_AUTO_RUN_NAME(_ve_auto_run_var_, __LINE__)
#define VE_AUTO_RUN(_CONTENT) namespace { int PRIVATE_VE_AUTO_RUN_FUNC() { _CONTENT; return 0; } \
/*const int __attribute__ ((unused))*/int PRIVATE_VE_AUTO_RUN_VAR = PRIVATE_VE_AUTO_RUN_FUNC(); /* NOLINT */ }

#ifndef NULL
#define NULL 0
#endif

#define VE_MEMBER_0(ClassMember) [this] { ClassMember(); }
#define VE_MEMBER_1(ClassMember) [this] (auto&& PH1) { ClassMember(std::forward<decltype(PH1)>(PH1)); }
#define VE_MEMBER_2(ClassMember) [this] (auto&& PH1, auto&& PH2) { ClassMember(std::forward<decltype(PH1)>(PH1), std::forward<decltype(PH2)>(PH2)); }
#define VE_MEMBER_3(ClassMember) [this] (auto&& PH1, auto&& PH2, auto&& PH3) { ClassMember(std::forward<decltype(PH1)>(PH1), std::forward<decltype(PH2)>(PH2), std::forward<decltype(PH3)>(PH3)); }

