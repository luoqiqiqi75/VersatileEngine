// ----------------------------------------------------------------------------
// common.h — hemera → ve compatibility bridge + ROS-specific engine layer
// ----------------------------------------------------------------------------
// Copyright (c) 2023-present Thilo and VersatileEngine contributors.
// Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
// See LICENSE file in the project root for full license information.
// ----------------------------------------------------------------------------

#pragma once

// --- Core re-exports ---
#include "ve/core/base.h"
#include "ve/core/log.h"
#include "ve/core/data.h"
#include "ve/core/factory.h"
#include "ve/core/module.h"

// ---------------------------------------------------------------------------
// hemera → ve namespace bridge
// ---------------------------------------------------------------------------
namespace hemera {
    using namespace ve;
}

constexpr const char* HEMERA_UNDEFINED_OBJECT_NAME = VE_UNDEFINED_OBJECT_NAME;

#define HEMERA_REGISTER_MODULE(Key, Class) VE_REGISTER_MODULE(Key, Class)

#ifndef HEMERA_APP_PATH
#define HEMERA_APP_PATH "./module/"
#endif

// ---------------------------------------------------------------------------
// hemera-era console logging  (tag / itag / wtag / etag)
// Prefer ve::log::i / ve::log::w / ve::log::e for new code.
// ---------------------------------------------------------------------------
namespace hemera {

constexpr const char* TAG_RED       = "\033[1;31m";
constexpr const char* TAG_GREEN     = "\033[1;32m";
constexpr const char* TAG_YELLOW    = "\033[1;33m";
constexpr const char* TAG_BLUE      = "\033[1;34m";
constexpr const char* TAG_PURPLE    = "\033[1;35m";
constexpr const char* TAG_RESET     = "\033[0m";

inline void tag() { std::cout << std::endl; }
template<typename Arg, typename... Rest>
inline void tag(Arg&& arg, Rest&&... rest) { std::cout << std::forward<Arg>(arg) << " "; tag(std::forward<Rest>(rest)...); }

template<typename... Args> inline void itag(Args&&... args) { std::cout << TAG_GREEN;  tag(std::forward<Args>(args)...); std::cout << TAG_RESET; }
template<typename... Args> inline void wtag(Args&&... args) { std::cout << TAG_YELLOW; tag(std::forward<Args>(args)...); std::cout << TAG_RESET; }
template<typename... Args> inline void etag(Args&&... args) { std::cout << TAG_RED;    tag(std::forward<Args>(args)...); std::cout << TAG_RESET; }

}

// ---------------------------------------------------------------------------
// hemera::util
// ---------------------------------------------------------------------------
namespace hemera::util {

bool startsWith(const std::string& str, const std::string& sub_str, bool ignore_case = false);
bool endsWith(const std::string& str, const std::string& sub_str, bool ignore_case = false);

}

// ---------------------------------------------------------------------------
// hemera::engine — ROS module loading & lifecycle
// ---------------------------------------------------------------------------
namespace hemera::engine {

VE_API ve::Module* module(const std::string& key);
VE_API ve::Vector<ve::Module*> modules();

VE_API bool loadApp(const std::string& name, const std::string& prefix = HEMERA_APP_PATH, ve::Module::State exec_step = ve::Module::INIT);
VE_API bool autoLoadAllApps(const std::string& prefix = HEMERA_APP_PATH);

VE_API void start(bool block = true);
VE_API void stop();

}
