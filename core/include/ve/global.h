// ----------------------------------------------------------------------------
// global.h
// ----------------------------------------------------------------------------
// This file is part of Versatile Engine
// ----------------------------------------------------------------------------
// Copyright (c) 2023 - 2023 Thilo, LuoQi, Qi Lu.
// Copyright (c) 2023 - 2023 Versatile Engine contributors (cf. AUTHORS.md)
//
// This file may be used under the terms of the GNU General Public License
// version 3.0 as published by the Free Software Foundation and appearing in
// the file LICENSE included in the packaging of this file.  Please review the
// following information to ensure the GNU General Public License version 3.0
// requirements will be met: http://www.gnu.org/copyleft/gpl.html.
//
// If you do not wish to use this file under the terms of the GPL version 3.0
// then you may purchase a commercial license. For more information contact
// <luoqiqiqi75@sina.com>.
//
// This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
// WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
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

#define VE_DECLARE_PRIVATE class Private; Private *_p;

#define PRIVATE_VE_AUTO_RUN_NAME(_PREFIX, _SUFFIX) PRIVATE_VE_AUTO_RUN_NAME_I(_PREFIX, _SUFFIX)
#define PRIVATE_VE_AUTO_RUN_NAME_I(_PREFIX, _SUFFIX) _PREFIX##_SUFFIX
#define PRIVATE_VE_AUTO_RUN_FUNC PRIVATE_VE_AUTO_RUN_NAME(_ve_auto_run_func_, __LINE__)
#define PRIVATE_VE_AUTO_RUN_VAR PRIVATE_VE_AUTO_RUN_NAME(_ve_auto_run_var_, __LINE__)
#define VE_AUTO_RUN(_CONTENT) namespace { int PRIVATE_VE_AUTO_RUN_FUNC() { _CONTENT; return 0; } \
/*const int __attribute__ ((unused))*/int PRIVATE_VE_AUTO_RUN_VAR = PRIVATE_VE_AUTO_RUN_FUNC(); /* NOLINT */ }

#ifndef NULL
#define NULL 0
#endif

#define VE_MEMBER_0(ClassMember) std::bind(ClassMember, this)
#define VE_MEMBER_1(ClassMember) std::bind(ClassMember, this, std::placeholders::_1)
#define VE_MEMBER_2(ClassMember) std::bind(ClassMember, this, std::placeholders::_1, std::placeholders::_2)
#define VE_MEMBER_3(ClassMember) std::bind(ClassMember, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)

