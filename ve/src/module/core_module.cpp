// core_module.cpp - ve::CoreModule (ve.core)
//
// System module: crash handler (rescue).
//   constructor: rescue setup
//
// Log configuration lives on /ve/entry/log and is applied by ve::entry::setup()
// so the entry pipeline's own output honors it — not here.

#include "ve/core/module.h"
#include "ve/core/log.h"
#include "ve/service/rescue.h"

#include <cstdio>

namespace ve {

class CoreModule : public Module
{
public:
    CoreModule()
    {
        if (node()->get("config/rescue/enabled").toBool(true)) {
            if (!service::trySetupRescue()) {
                std::fputs("[VE] crash reporting could not be installed\n", stderr);
            }
        }
    }
};

} // namespace ve

VE_REGISTER_PRIORITY_MODULE(ve.core, ve::CoreModule, 0, 1)
