// core_module.cpp - ve::CoreModule (ve.core)
//
// System module: log configuration, crash handler (rescue), builtin commands.
//   constructor: rescue + log config (from JSON defaults)
//   init():      register builtin commands (so other modules can extend)

#include "ve/core/module.h"
#include "ve/core/log.h"
#include "ve/core/command.h"
#include "ve/service/rescue.h"

namespace ve {

class CoreModule : public Module
{
public:
    explicit CoreModule(const std::string& name) : Module(name)
    {
        auto* n = node();

        bool rescue = true;
        if (auto* rn = n->resolve("config/rescue/enabled")) {
            rescue = rn->get<bool>(true);
        }
        if (rescue) setupRescue();

        if (auto* log_n = n->resolve("config/log")) {
            if (auto* level_n = log_n->resolve("level")) {
                std::string lvl = level_n->get<std::string>();
                if      (lvl == "debug") log::setLevel(LogLevel::Debug);
                else if (lvl == "info")  log::setLevel(LogLevel::Info);
                else if (lvl == "warn")  log::setLevel(LogLevel::Waring);
                else if (lvl == "error") log::setLevel(LogLevel::Error);
            }
            if (auto* app_n = log_n->resolve("app")) {
                log::setAppName(app_n->get<std::string>());
            }
            if (auto* dir_n = log_n->resolve("dir")) {
                log::setLogDir(dir_n->get<std::string>());
            }
        }
    }

protected:
    void init() override
    {
        command::initBuiltins();
    }
};

} // namespace ve

VE_REGISTER_PRIORITY_MODULE(ve.core, ve::CoreModule, 0, 1)
