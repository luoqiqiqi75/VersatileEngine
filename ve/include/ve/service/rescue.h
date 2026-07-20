// rescue.h - process crash reporting (ve::service)
#pragma once

#include "ve/global.h"

namespace ve {
namespace service {

// Installs the process-level crash reporter. Repeated calls are harmless.
//
// The reporter writes the faulting thread and stack to stderr. If stderr is
// unavailable, platform implementations may use a debugger sink or a local
// crash log as a fallback.
VE_API void setupRescue();

// Observable variant for startup code that needs to diagnose unsupported
// platforms or native handler installation failures.
VE_API bool trySetupRescue() noexcept;

} // namespace service
} // namespace ve
