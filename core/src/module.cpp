#include "ve/core/module.h"
#include "ve/core/data.h"

#include "internal.h"

#define STATE_IMPL(S, F) \
trigger(MODULE_STATE_ABOUT_TO_CHANGE); \
F(); _p->s = S;                        \
trigger(MODULE_STATE_CHANGED)

namespace ve {

struct Module::Private
{
    State s = NONE;
};

Module::Module() : Object("ve::module_" + data::at("ve.module.global_module_key")->getString().toStdString()), _p(new Private) {}
Module::~Module() noexcept { delete _p; }

Module::State Module::state() const { return _p->s; }

template<> void Module::exeState<Module::INIT>() { STATE_IMPL(INIT, init); }
template<> void Module::exeState<Module::READY>() { STATE_IMPL(READY, ready); }
template<> void Module::exeState<Module::DEINIT>() { STATE_IMPL(DEINIT, deinit); }

void Module::init() {}
void Module::ready() {}
void Module::deinit() {}

ModuleFactory& globalModuleFactory()
{
    static ModuleFactory i("ve::g_module_factory");
    return i;
}

namespace module {

Module* instance(const std::string &name)
{
    for (int i = 0; i < g_module_names.sizeAsInt(); i++) {
        if (g_module_names[i] == name) return g_modules.value(i, nullptr);
    }
    return nullptr;
}

}

}

std::ostream& operator<< (std::ostream& os, ve::Module::State s)
{
    switch (s) {
        case ve::Module::NONE: os << "NONE"; break;
        case ve::Module::INIT: os << "INIT"; break;
        case ve::Module::READY: os << "READY"; break;
        case ve::Module::DEINIT: os << "DEINIT"; break;
    }
    return os;
}
