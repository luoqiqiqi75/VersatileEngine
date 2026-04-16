// core_module.cpp - ve::CoreModule (ve.core)
//
// System module: log configuration, crash handler (rescue).
//   constructor: rescue + log config (from JSON defaults)

#include "ve/core/module.h"
#include "ve/core/log.h"
#include "ve/service/rescue.h"

namespace ve {

class CoreModule : public Module
{
public:
    explicit CoreModule(const std::string& name) : Module(name)
    {
        auto* n = node();

        { // rescue
            if (n->get("config/rescue/enabled").toBool(true)) {
                service::setupRescue();
            }
        }

        { // log
            std::string lvl = n->get("config/log/level").toString("info");
            if (lvl[0] == 'd') {
                log::setLevel(LogLevel::Debug);
            } else if (lvl[0] == 'w') {
                log::setLevel(LogLevel::Waring);
            } else if (lvl[0] == 'e') {
                log::setLevel(LogLevel::Error);
            } else {
                log::setLevel(LogLevel::Info);
            }

            log::setAppName(n->get("config/log/app").toString("ve"));
            std::string log_dir = n->get("config/log/dir").toString("");
            if (!log_dir.empty()) {
                log::setLogDir(log_dir);
            }
        }
    }
};

} // namespace ve

VE_REGISTER_PRIORITY_MODULE(ve.core, ve::CoreModule, 0, 1)
