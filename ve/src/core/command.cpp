// command.cpp - Command instance and command factory helpers

#include "ve/core/command.h"
#include "ve/core/schema.h"

#include "parse_util.h"

#include <algorithm>
#include <exception>
#include <unordered_set>

namespace ve {

struct Command::Private
{
    Loop* l = nullptr;

    Node* ctx_n = nullptr;
    Node* in_n = nullptr;
    Node* out_n = nullptr;

    Node internal_ctx_n;

    Result r;
};

Command::Command(Node* factory_n, Node* ctx_n, Node* in_n, Node* out_n) : NodeRef(factory_n), _p(std::make_shared<Private>())
{
    _p->r = Result::fail(-0xff01, "command invalid");
    if (factory_n) {
        _p->l = factory_n->get("loop").as<Loop*>();
    }
    setContextNodes(ctx_n, in_n, out_n);
}

Command::~Command() = default;

Node* Command::contextNode() const { return _p->ctx_n; }
Node* Command::inputNode() const { return _p->in_n; }
Node* Command::outputNode() const { return _p->out_n; }

Command& Command::setContextNodes(Node* ctx_n, Node* in_n, Node* out_n)
{
    _p->ctx_n = ctx_n ? ctx_n : &_p->internal_ctx_n;
    _p->in_n = in_n ? in_n : _p->ctx_n->at("in");
    _p->out_n = out_n ? out_n : _p->ctx_n->at("out");
    return *this;
}

bool Command::valid() const
{
    return node() != nullptr && node()->get().isCallable()
        && _p->ctx_n != nullptr && _p->in_n != nullptr && _p->out_n != nullptr;
}

Command& Command::run()
{
    if (!valid()) { _p->r = Result::fail("command invalid"); return *this; }
    try {
        _p->r = node()->get().invoke(_p->ctx_n, _p->in_n, _p->out_n).as<Result>();
    } catch (const std::exception& e) {
        _p->r = Result::fail(e.what());
    } catch (...) {
        _p->r = Result::fail("unknown exception");
    }
    return *this;
}

Loop* Command::loop() const { return _p->l; }
void Command::setLoop(Loop* l) { _p->l = l; }

Result Command::result() const { return _p->r; }

void Command::call(Callback cb, Loop* cb_loop) const
{
    auto task = [c = *this, cb, cb_l = cb_loop]() mutable {
        c.run(); // proc exec in l
        if (cb_l) {
            cb_l->post([cb, c]() mutable { if (cb) cb(c); }); // callback exec in loop
        } else if (cb) {
            cb(c); // callback exec in l
        }
    };
    _p->l ? _p->l->post(task) : task();
}

namespace command {

Factory& factory()
{
    return factory::at("cmd");
}

static Var::Type schemaVarType(const std::string& t)
{
    if (t == "string")  return Var::STRING;
    if (t == "integer") return Var::INT;
    if (t == "number")  return Var::DOUBLE;
    if (t == "boolean") return Var::BOOL;
    return Var::NONE;
}

std::string description(const Factory& f, const std::string& key)
{
    auto i_n = instruction(f, key);
    return i_n ? i_n->get("description").toString() : std::string{};
}

std::string usage(const Factory& f, const std::string& key)
{
    Node* d = instruction(f, key);
    if (!d) return key;

    std::string u = d->get("usage").toString();
    if (!u.empty()) return u;

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

bool bind(const Factory& f, const std::string& key, const Strings& tokens, Node* in, std::string* err)
{
    if (!in) { if (err) *err = "no input node"; return false; }

    Node* d = instruction(f, key);
    Node* schema = d ? d->find("input_schema") : nullptr;
    Node* props  = schema ? schema->find("properties") : nullptr;
    if (!props) return true;

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
            if (vt == Var::BOOL && raw.empty()) raw = "true";
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

bool bindJson(const std::string& json, Node* in, std::string* err)
{
    if (!in) { if (err) *err = "no input node"; return false; }
    if (!schema::toNode<schema::JsonS>(in, json)) {
        if (err) *err = "invalid JSON";
        return false;
    }
    return true;
}

} // namespace command

} // namespace ve
