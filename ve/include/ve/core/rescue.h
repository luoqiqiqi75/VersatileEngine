// ----------------------------------------------------------------------------
// rescue.h — crash handler public API
// ----------------------------------------------------------------------------
// Pure C++ / Win32 / POSIX — no Qt dependency.
//
// Usage:
//   #include "ve/core/rescue.h"
//   setupRescue();   // call once at app startup
// ----------------------------------------------------------------------------
#pragma once

#include "ve/global.h"

VE_API void setupRescue();
