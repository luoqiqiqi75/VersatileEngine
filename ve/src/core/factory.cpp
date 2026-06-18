// factory.cpp - ve::Factory, ve::factory::, ve::version::
#include "ve/core/factory.h"
#include "ve/core/node.h"
#include "ve/core/log.h"

#include "parse_util.h"

#include <unordered_set>

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

Node* Factory::reg(const std::string& key, Node* functor_n, Var callable, const std::string& help, Loop* lr)
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
    if (!help.empty()) functor_n->at("help")->set(help);
    if (lr) functor_n->at("loop")->set(Var(static_cast<void*>(lr)));
    return functor_n;
}

// --- describe-driven argument binding ---------------------------------------

namespace {

// JSON-Schema type name -> Var::Type for coercion. array/object/absent -> NONE
// (auto-detect via parse::parseValue).
Var::Type schemaVarType(const std::string& t)
{
    if (t == "string")  return Var::STRING;
    if (t == "integer") return Var::INT;
    if (t == "number")  return Var::DOUBLE;
    if (t == "boolean") return Var::BOOL;
    return Var::NONE;
}

} // namespace

std::string Factory::usage(const std::string& key, char sep) const
{
    Node* d = describe(key, sep);
    if (!d) return key;

    std::string u = d->get("usage").toString();
    if (!u.empty()) return u;

    // Synthesize: "key <required> [optional]" from input_schema.
    Node* schema = d->find("input_schema");
    Node* props  = schema ? schema->find("properties") : nullptr;
    if (!props) return key;

    std::unordered_set<std::string> required;
    if (Node* req = schema->find("required"))
        for (Node* c : req->children()) required.insert(c->get().toString());

    std::string out = key;
    for (Node* p : props->children()) {
        const std::string& name = p->name();
        out += required.count(name) ? " <" + name + ">" : " [" + name + "]";
    }
    return out;
}

bool Factory::bind(const std::string& key, const Strings& tokens, Node* in,
                   std::string* err, char sep) const
{
    if (!in) { if (err) *err = "no input node"; return false; }

    Node* d = describe(key, sep);
    Node* schema = d ? d->find("input_schema") : nullptr;
    Node* props  = schema ? schema->find("properties") : nullptr;
    if (!props) return true; // no schema to bind against; leave caller's fallback

    parse::Flags fl = parse::parseFlags(tokens, 0);

    std::unordered_set<std::string> required;
    if (Node* req = schema->find("required"))
        for (Node* c : req->children()) required.insert(c->get().toString());

    int posCursor = 0;
    for (Node* p : props->children()) {
        const std::string& name = p->name();
        Var::Type vt = schemaVarType(p->get("type").toString());

        std::string raw;
        bool have = false;
        if (fl.has(name)) {
            have = true;
            raw = fl.get(name);
            if (vt == Var::BOOL && raw.empty()) raw = "true"; // --flag => true
        } else if (posCursor < fl.posCount()) {
            have = true;
            raw = fl.pos(posCursor++);
        }
        if (have) in->set(name, parse::parseValueAs(raw, vt));
    }

    for (const auto& r : required) {
        if (!in->find(r)) {
            if (err) *err = "missing required argument: " + r;
            return false;
        }
    }
    return true;
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
