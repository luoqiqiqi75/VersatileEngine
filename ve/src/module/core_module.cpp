// core_module.cpp - ve::CoreModule (ve.core)
//
// System module: crash handler (rescue), runtime plugin loading.
//   constructor: rescue setup, plugin command unless config/plugin/fixed
//
// Log configuration lives on /ve/entry/log and is applied by ve::entry::setup()
// so the entry pipeline's own output honors it — not here.

#include "ve/core/module.h"
#include "ve/core/command.h"
#include "ve/core/log.h"
#include "ve/core/res.h"
#include "ve/core/schema.h"
#include "ve/entry.h"
#include "ve/service/rescue.h"
#include "../core/plugin.h"

#include <cstdio>

namespace ve {

static Result loadPlugin(Node*, Node* in, Node* out)
{
    if (Node* args = in->find("args")) {
        Strings tokens;
        for (Node* token : *args) tokens.push_back(token->getString());
        std::string error;
        if (!command::bind(command::factory(), "plugin", tokens, in, &error)) return Result::fail(error);
    }
    // enabled is a ve.json declaration, not caller input.
    Node spec;
    spec.set("name", in->get("name"));
    spec.set("path", in->get("path"));
    std::string error;
    return plugin::load(&spec, out, entry::verbose(), &error) ? Result::ok() : Result::fail(error);
}

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
        // The plugin set is fixed at startup unless configured otherwise.
        if (!node()->get("config/plugin/fixed").toBool(true)) {
            auto& f = command::factory();
            schema::JsonS::toNode(f.node(), std::string(res::read("ve/core/cmd.json")));
            f.reg("plugin", Var::callable(Proc(loadPlugin)));
        }
    }
};

} // namespace ve

VE_REGISTER_PRIORITY_MODULE(ve.core, ve::CoreModule, 0, 1)
