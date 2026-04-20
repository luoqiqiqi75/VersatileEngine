// factory.cpp - ve::Factory, ve::factory::, ve::version::
#include "ve/core/factory.h"
#include "ve/core/node.h"

namespace ve {

struct Factory::Private
{
    Node* root = nullptr;  // /ve/factory/{name}
    Strings keys;          // registered keys (for fast enumeration)
};

static std::string normalizeKey(const std::string& key)
{
    std::string p = key;
    std::replace(p.begin(), p.end(), '.', '/');
    return p;
}

Factory::Factory(const std::string& name)
    : Object(name), _p(std::make_unique<Private>())
{
    _p->root = node::root()->at("ve/factory/" + name);
}

Factory::~Factory() = default;

void Factory::reg(const std::string& key, Var callable,
                  const std::string& help, LoopRef lr)
{
    auto nkey = normalizeKey(key);
    auto* nd = _p->root->at(nkey);
    nd->set(std::move(callable));
    if (!help.empty())
        nd->at("help")->set(Var(help));
    if (lr)
        nd->at("loop")->set(Var::custom(std::move(lr)));

    // Track registered key
    auto it = std::find(_p->keys.begin(), _p->keys.end(), nkey);
    if (it == _p->keys.end()) {
        _p->keys.push_back(nkey);
    }
}

Node* Factory::node(const std::string& key) const
{
    return _p->root->find(normalizeKey(key), false);
}

Node* Factory::ensureNode(const std::string& key)
{
    return _p->root->at(normalizeKey(key));
}

void Factory::erase(const std::string& key)
{
    auto nkey = normalizeKey(key);
    auto* nd = node(nkey);
    if (nd && nd->parent()) {
        nd->parent()->remove(nd);
        // Remove from keys list
        auto it = std::find(_p->keys.begin(), _p->keys.end(), nkey);
        if (it != _p->keys.end()) {
            _p->keys.erase(it);
        }
    }
}

Node* Factory::root() const
{
    return _p->root;
}

Strings Factory::keys() const
{
    return _p->keys;
}

// ============================================================================
// factory:: namespace
// ============================================================================

namespace factory {

Factory& get(const std::string& name)
{
    static auto* s_map = new Hash<Factory*>();
    auto it = s_map->find(name);
    if (it != s_map->end()) return *it->second;
    auto* f = new Factory(name);
    (*s_map)[name] = f;
    return *f;
}

Node* root()
{
    return node::root()->at("ve/factory");
}

Strings keys(const std::string& factory_name)
{
    return get(factory_name).keys();
}

} // namespace factory

// ============================================================================
// version:: namespace
// ============================================================================

namespace version {

void reg(const std::string& key, int ver)
{
    const int v = ver;
    factory::get("version").reg(key, Var::callable([v]() -> int { return v; }));
}

int number(const std::string& key)
{
    auto* nd = factory::get("version").node(key);
    if (!nd || !nd->get().isCallable()) return 0;
    return nd->get().invoke({}).toInt(0);
}

bool check(const std::string& key, int min_api)
{
    return number(key) >= min_api;
}

} // namespace version

} // namespace ve
