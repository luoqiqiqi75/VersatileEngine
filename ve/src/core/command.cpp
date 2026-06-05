// command.cpp — CallProto specializations, Command, command:: namespace

#include "ve/core/command.h"
#include "ve/core/pipeline.h"
#include "ve/core/schema.h"
#include "ve/core/log.h"
#include "parse_util.h"

#include <algorithm>
#include <map>
#include <set>

namespace ve {

// ============================================================================
// CallProto specializations
// ============================================================================
//
// All CallProtos read/write the top-level envelope:
//   /request   /reply   /code   /message
// _pipe/ subtree is framework-internal; only NodeInOut parks pointers there.

// ----- VarInVarOut ------------------------------------------------------------

void CallProto<tag::VarInVarOut>::import(Node* ctx, const Var& v)
{
    if (!ctx) return;
    Node* req = ctx->at("request");
    if (req->shadow()) {
        command::parseArgs(req, v);
        return;
    }
    schema::importAs<schema::VarS>(req, v);
}

Var CallProto<tag::VarInVarOut>::exportOut(Node* ctx)
{
    if (!ctx) return {};
    return schema::exportAs<schema::VarS>(ctx->at("reply"));
}

Var CallProto<tag::VarInVarOut>::makeFailure(int /*code*/, const std::string& /*msg*/)
{
    return {};   // raw-data form: failures yield empty Var
}


// ----- RequestReply -----------------------------------------------------------

void CallProto<tag::RequestReply>::import(Node* ctx, const Var& v)
{
    if (!ctx) return;
    Node* req = ctx->at("request");
    if (req->shadow()) {
        command::parseArgs(req, v);
        return;
    }
    schema::importAs<schema::VarS>(req, v);
}

Result CallProto<tag::RequestReply>::exportOut(Node* ctx)
{
    if (!ctx) return Result::fail(Result::UNKNOWN_CMD, "ctx is null");
    Result r;
    r.code    = ctx->get("code").toInt(0);
    r.message = ctx->get("message").toString();
    r.data    = schema::exportAs<schema::VarS>(ctx->at("reply"));
    return r;
}

Result CallProto<tag::RequestReply>::makeFailure(int code, const std::string& msg)
{
    return Result::fail(code, msg);
}


// ----- ListInDictOut ----------------------------------------------------------

void CallProto<tag::ListInDictOut>::import(Node* ctx, const Var::ListV& v)
{
    if (!ctx) return;
    Node* req = ctx->at("request");
    if (req->shadow()) {
        Var input(v);
        command::parseArgs(req, input);
        return;
    }
    req->clear();
    for (size_t i = 0; i < v.size(); ++i)
        req->at(static_cast<int>(i))->set(v[i]);
}

Result CallProto<tag::ListInDictOut>::exportOut(Node* ctx)
{
    return CallProto<tag::RequestReply>::exportOut(ctx);
}

Result CallProto<tag::ListInDictOut>::makeFailure(int code, const std::string& msg)
{
    return Result::fail(code, msg);
}


// ----- NodeInOut --------------------------------------------------------------

void CallProto<tag::NodeInOut>::import(Node* ctx, const Input& v)
{
    if (!ctx) return;
    // Park caller's in/out pointers under _pipe/ — Pipeline reads these to wire bypass.
    ctx->set("_pipe/_bind_in",  Var(static_cast<void*>(v.in)));
    ctx->set("_pipe/_bind_out", Var(static_cast<void*>(v.out)));
}

void CallProto<tag::NodeInOut>::exportOut(Node* /*ctx*/) {}
void CallProto<tag::NodeInOut>::makeFailure(int /*code*/, const std::string& /*msg*/) {}


// ============================================================================
// Command
// ============================================================================

namespace {

// Singleton sentinel returned when Command(key) cannot resolve `key`. Carries a
// proc that returns Result::fail(UNKNOWN_CMD, ...) so downstream Pipeline path
// produces a clean failure instead of segfaulting on null _n.
Node* missingCommandNode()
{
    static Node* s_n = []() -> Node* {
        auto* n = new Node("_missing_cmd");
        Proc fail_proc = [](Node*, Node*) -> Result {
            return Result::fail(Result::UNKNOWN_CMD, "unknown command");
        };
        n->at("_proc")->set(Var::custom(std::move(fail_proc)));
        n->at("_in_schema") ->set(Var::custom(InSchema {InSchema::Empty}));
        n->at("_out_schema")->set(Var::custom(OutSchema{OutSchema::Empty}));
        return n;
    }();
    return s_n;
}

} // anonymous

Command::Command(const std::string& key, char sep)
{
    const Factory& cf = command::factory();
    _n = cf.node(key, sep);
    if (!_n) _n = missingCommandNode();
}

bool Command::isValid() const
{
    return _n && _n != missingCommandNode();
}

Proc Command::proc() const
{
    if (!_n) return {};
    auto* pn = _n->find("_proc", false);
    if (!pn) return {};
    Var v = pn->get();
    if (auto* p = v.customPtr<Proc>()) return *p;
    return {};
}

InSchema Command::inSchema() const
{
    if (!_n) return {};
    auto* sn = _n->find("_in_schema", false);
    if (!sn) return {};
    Var v = sn->get();
    if (auto* p = v.customPtr<InSchema>()) return *p;
    return {};
}

OutSchema Command::outSchema() const
{
    if (!_n) return {};
    auto* sn = _n->find("_out_schema", false);
    if (!sn) return {};
    Var v = sn->get();
    if (auto* p = v.customPtr<OutSchema>()) return *p;
    return {};
}

Loop* Command::loop() const
{
    if (!_n) return nullptr;
    auto* ln = _n->find("loop", false);
    if (!ln) return nullptr;
    return static_cast<Loop*>(ln->get().toPointer());
}

// Independent call: spin up a transient Pipeline + addPathStep wired to
// /request, /reply; let Pipeline::call<CallProtoT> do the rest.
template<typename CallProtoT>
typename CallProtoT::Output Command::call(const typename CallProtoT::Input& input)
{
    if (!isValid()) {
        return CallProtoT::makeFailure(Result::UNKNOWN_CMD,
            "unknown command: " + (_n ? _n->name() : std::string{"?"}));
    }
    Pipeline pipe(_n->name());
    pipe.addPathStep(*this, "request", "reply", loop());
    return pipe.template call<CallProtoT>(input);
}

// Explicit instantiations for the standard 4 CallProto specializations.
// User-defined CallProtos must either inline-instantiate or add their own
// extern template declaration in user code.
template Var    Command::call<CallProto<tag::VarInVarOut>>  (const Var&);
template Result Command::call<CallProto<tag::RequestReply>> (const Var&);
template Result Command::call<CallProto<tag::ListInDictOut>>(const Var::ListV&);
template void   Command::call<CallProto<tag::NodeInOut>>    (const CallProto<tag::NodeInOut>::Input&);


// ============================================================================
// command:: namespace — factory + query + parseArgs + Args
// ============================================================================

namespace command {

Factory& factory() { return factory::at("cmd"); }

bool has(const std::string& key, char sep)
{
    const Factory& cf = factory();
    return cf.node(key, sep) != nullptr;
}


// --- argument parsing (state-machine, declare-driven) ---
//
// Writes parsed values into the `in` node directly (in is the input container
// in the 2-node Proc model; CallProto already places it where Pipeline wires).
// declare/<param>/_default + _short metadata read from in->shadow() if set.

namespace {

struct DeclParam
{
    const Node* node = nullptr;
    std::string name;
};

void buildDeclInfo(const Node* decl, std::vector<DeclParam>& params)
{
    if (!decl) return;
    for (auto* param : *decl) {
        const auto& nm = param->name();
        if (nm.empty() || nm[0] == '_') continue;
        params.push_back({param, nm});
    }
}

const DeclParam* findExactOrPrefix(const std::vector<DeclParam>& params,
                                   const std::string& name)
{
    const DeclParam* prefix = nullptr;
    for (const auto& p : params) {
        if (p.name == name) return &p;
        if (!prefix && p.name.size() >= name.size()
            && p.name.compare(0, name.size(), name) == 0) {
            prefix = &p;
        }
    }
    return prefix;
}

const DeclParam* findShort(const std::vector<DeclParam>& params,
                           const std::string& key)
{
    for (const auto& p : params) {
        if (auto* shortNode = p.node->find("_short", false)) {
            if (shortNode->getString() == key) return &p;
        }
    }
    return findExactOrPrefix(params, key);
}

Var parseForDecl(const Node* decl, const std::string& raw)
{
    return decl ? parse::parseValueAs(raw, decl->get().type()) : parse::parseValue(raw);
}

bool declIsBool(const Node* decl)
{
    return decl && decl->get().type() == Var::BOOL;
}

} // anonymous

bool parseArgs(Node* in, const std::vector<std::string>& args, int startIdx)
{
    if (!in) return false;

    in->clear();
    Node* req = in;   // write directly into the input container (2-node Proc model)

    const Node* decl = in->shadow();   // in may carry declare/ as shadow

    std::vector<DeclParam> params;
    buildDeclInfo(decl, params);

    const DeclParam* currentTarget = nullptr;
    int posIndex = 0;

    auto isKeyword = [&](const std::string& token) -> const DeclParam* {
        if (token.size() < 2 || token[0] != '-') return nullptr;
        if (parse::isInt(token) || parse::isDouble(token)) return nullptr;
        if (token[1] == '-') {
            auto eq = token.find('=', 2);
            std::string name = (eq != std::string::npos) ? token.substr(2, eq - 2) : token.substr(2);
            return findExactOrPrefix(params, name);
        }
        if (token.size() == 2) {
            std::string key(1, token[1]);
            return findShort(params, key);
        }
        return nullptr;
    };

    for (size_t i = static_cast<size_t>(startIdx); i < args.size(); ++i) {
        const auto& token = args[i];

        // --name=value inline form
        if (token.size() > 2 && token[0] == '-' && token[1] == '-') {
            auto eq = token.find('=', 2);
            if (eq != std::string::npos) {
                std::string name = token.substr(2, eq - 2);
                if (auto* param = findExactOrPrefix(params, name)) {
                    currentTarget = nullptr;
                    req->at(param->name, false)->set(parseForDecl(param->node, token.substr(eq + 1)));
                    continue;
                }
            }
        }

        const DeclParam* matched = isKeyword(token);
        if (matched) {
            if (currentTarget && declIsBool(currentTarget->node)) {
                req->at(currentTarget->name, false)->set(true);
            }
            currentTarget = matched;
            continue;
        }

        if (currentTarget) {
            req->at(currentTarget->name, false)->set(parseForDecl(currentTarget->node, token));
            currentTarget = nullptr;
        } else if (posIndex < static_cast<int>(params.size())) {
            const auto& param = params[posIndex];
            req->at(param.name, false)->set(parseForDecl(param.node, token));
            ++posIndex;
        } else {
            req->at(req->count(), false)->set(parse::parseValue(token));
        }
    }

    if (currentTarget) {
        req->at(currentTarget->name, false)->set(true);
    }

    return true;
}

bool parseArgs(Node* in, const Var& input)
{
    if (!in) return false;

    in->clear();
    Node* req = in;
    if (input.isNull()) return true;

    if (input.isDict()) {
        for (auto& [key, val] : input.toDict()) {
            if (!key.empty() && key[0] != '_')
                req->at(key, false)->set(val);
        }
        return true;
    }

    if (input.isList()) {
        std::vector<std::string> strs;
        strs.reserve(input.toList().size());
        for (auto& item : input.toList())
            strs.push_back(item.toString());
        return parseArgs(in, strs, 0);
    }

    // scalar: bind to first positional param if declared, else int-index 0
    const Node* decl = in->shadow();
    if (decl) {
        for (auto* param : *decl) {
            const auto& nm = param->name();
            if (nm.empty() || nm[0] == '_') continue;
            if (!param->find("_short", false)) {
                req->at(nm, false)->set(input);
                return true;
            }
        }
    }
    req->at(0, false)->set(input);
    return true;
}


// --- Args accessor (reads from the in node directly) ---

Var Args::var(const std::string& key, const Var& def) const
{
    if (!_n || key.empty()) return def;
    if (auto* n = _n->find(key)) {
        Var v = n->get();
        if (!v.isNull()) return v;
    }
    return def;
}

std::string Args::string(const std::string& key, const std::string& def) const
{
    Var v = var(key);
    return v.isNull() ? def : v.toString(def);
}

int64_t Args::integer(const std::string& key, int64_t def) const
{
    Var v = var(key);
    return v.isNull() ? def : v.toInt64(def);
}

double Args::number(const std::string& key, double def) const
{
    Var v = var(key);
    return v.isNull() ? def : v.toDouble(def);
}

bool Args::flag(const std::string& key, bool def) const
{
    Var v = var(key);
    return v.isNull() ? def : v.toBool(def);
}

bool Args::has(const std::string& key) const
{
    if (!_n || key.empty()) return false;
    auto* n = _n->find(key, false);
    return n && !n->get().isNull();
}

// Wrap the in node — proc bodies use args(in) to read params.
Args args(Node* in)
{
    return Args(in);
}

} // namespace command

} // namespace ve
