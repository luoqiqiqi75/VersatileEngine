// factory.cpp - ve::Factory, ve::factory::, ve::version::
#include "ve/core/factory.h"
#include "ve/core/node.h"
#include "ve/core/log.h"

namespace ve {

struct Factory::Private
{
    Strings keys;  // registered keys (caller-form, for fast enumeration)
};

Factory::Factory(const std::string& name)
    : NodeRef(node::root()->at("ve/factory/" + name))   // ensure-exists at /ve/factory/{name}
    , _p(std::make_unique<Private>())
{
}

Factory::~Factory() = default;

Strings Factory::keys() const { return _p->keys; }

Node* Factory::reg(const std::string& key, Node* functor_n, Var callable, Loop* lr)
{
    if (!functor_n) return nullptr;

    // Track the caller-form key for enumeration.
    if (auto it = std::find(_p->keys.begin(), _p->keys.end(), key); it == _p->keys.end()) {
        _p->keys.push_back(key);
    } else {
        veLogE << "<ve/factory>" << _n->name() << ": duplicate registration for key: " << key;
        return functor_n;
    }

    functor_n->set(std::move(callable));
    if (lr) functor_n->at("loop")->set(Var(static_cast<void*>(lr)));
    return functor_n;
}


// ============================================================================
// factory:: namespace
// ============================================================================

namespace factory {

Node* root() { return n("ve/factory"); }

static Dict<Factory*> s_factories;

Factory& at(const std::string& name)
{
    auto f = s_factories.value(name, nullptr);
    if (!f) {
        f = new Factory(name);
        s_factories[name] = f;
    }
    return *f;
}

} // namespace factory

// ============================================================================
// version:: namespace
// ============================================================================

namespace version {

void reg(const std::string& key, const int ver)
{
    factory::at("version").reg(key, [ver] () -> int { return ver; });
}

int number(const std::string& key)
{
    return factory::at("version").exec<int>(key);
}

bool check(const std::string& key, int min_api)
{
    return number(key) >= min_api;
}

} // namespace version

} // namespace ve
