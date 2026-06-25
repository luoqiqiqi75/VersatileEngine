#include "ve/core/module.h"

#include "ve/core/log.h"

#define STATE_IMPL(S, F) \
trigger(MODULE_STATE_ABOUT_TO_CHANGE); \
F(); _p->s = S;                        \
trigger(MODULE_STATE_CHANGED)

namespace ve {

struct Module::Private
{
    State s = NONE;
};

namespace {

std::string s_constructing_key;

std::string moduleKeyToPath(const std::string& name)
{
    std::string path = name;
    for (auto& c : path) {
        if (c == '.') c = '/';
    }
    return path;
}

} // anon

Module::Module(const std::string& name, Node* module_n) : Object(name),
    NodeRef(module_n), _p(new Private)
{
    if (!_n) veLogW << "<ve.module> create " << name << " module with null node";
}

Module::Module(const std::string& name) : Module(name, n(moduleKeyToPath(name)))
{
}

Module::Module() : Module(s_constructing_key)
{
    s_constructing_key.clear();
}


Module::~Module() noexcept { delete _p; }

Module::State Module::state() const { return _p->s; }

template<> VE_API void Module::exeState<Module::INIT>() { STATE_IMPL(INIT, init); }
template<> VE_API void Module::exeState<Module::PREPARE>() { STATE_IMPL(PREPARE, prepare); }
template<> VE_API void Module::exeState<Module::READY>() { STATE_IMPL(READY, ready); }
template<> VE_API void Module::exeState<Module::DEINIT>() { STATE_IMPL(DEINIT, deinit); }

void Module::init() {}
void Module::prepare() {}
void Module::ready() {}
void Module::deinit() {}

namespace module {

namespace impl {
void setConstructingKey(const std::string& key) { s_constructing_key = key; }
}

Factory& factory() { return factory::at("module"); }

}

}

std::ostream& operator<< (std::ostream& os, ve::Module::State s)
{
    switch (s) {
        case ve::Module::INIT: os << "INIT"; break;
        case ve::Module::PREPARE: os << "PREPARE"; break;
        case ve::Module::READY: os << "READY"; break;
        case ve::Module::DEINIT: os << "DEINIT"; break;
    }
    return os;
}
