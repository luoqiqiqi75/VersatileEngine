#pragma once

// Unified: rtt modules share ve/global.h for common includes & macros.
// RTT-specific extras (imol constants, IC_MEMBER macros) kept here.

#include "ve/global.h"

#include <string>
#include <cstring>
#include <typeinfo>
#include <type_traits>
#include <stdexcept>
#include <queue>

constexpr const char* IMOL_NULL_OBJECT_NAME = "@null";
constexpr const char* IMOL_UNDEFINED_OBJECT_NAME = "@undefined";
