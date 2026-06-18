// node_commands.cpp — envelope v2: op|cmd/params → code/message/data
//
// Command implementations registered in factory::at("service/op").
// resolveCmd() reads "op" (std factory) or "cmd" (command factory) from ctx.
// finalizeReply() mutates a Pipeline's contextNode into reply format.

#include "node_commands.h"

#include "ve/core/node.h"
#include "ve/core/command.h"
#include "ve/core/schema.h"
#include "ve/core/pipeline.h"
#include "ve/core/res.h"

namespace ve {
namespace service {

static std::string toJson(const Node& n)
{
    return schema::fromNode<schema::JsonS>(&n, schema::JsonS::compact());
}

static std::string normalizePath(std::string path)
{
    while (!path.empty() && path.front() == VE_NODE_PATH_SEP) path.erase(path.begin());
    return path;
}

// ============================================================================
// Command implementations — Proc(ctx, params, data)
// ============================================================================
namespace op {

static Session* session(Node* ctx) { return ctx->get("_session").as<Session*>(); }
static Node*    root(Node* ctx)    { return session(ctx)->root; }

static Result get(Node* ctx, Node* params, Node* data)
{
    Node* r = root(ctx);
    std::string path = normalizePath(params->get("path").toString());
    Node* target = path.empty() ? r : r->find(path);
    if (!target) return Result::fail(ERR_NOT_FOUND, "not found: " + path);
    data->at("value")->set(target->get());
    return Result::ok();
}

static Result set(Node* ctx, Node* params, Node* data)
{
    Node* r = root(ctx);
    Node* vn = params->find("value");
    if (!vn) return Result::fail(ERR_INVALID, "value required");
    Node* target = r->at(params->get("path").toString());
    target->copy(vn);
    data->set("path", target->path(r));
    return Result::ok();
}

static Result import_(Node* ctx, Node* params, Node* data)
{
    Node* r = root(ctx);
    Node* tree = params->find("tree");
    if (!tree) return Result::fail(ERR_INVALID, "tree required");
    int flags = (params->get("insert").toBool(true)   ? Node::COPY_INSERT  : 0)
              | (params->get("replace").toBool(true)  ? Node::COPY_REPLACE : 0)
              | (params->get("remove").toBool(false)  ? Node::COPY_REMOVE  : 0)
              | (params->get("update").toBool(false)  ? Node::COPY_UPDATE  : 0);
    Node* target = r->at(params->get("path").toString());
    target->copy(tree, flags);
    data->set("path", target->path(r));
    return Result::ok();
}

static Result export_(Node* ctx, Node* params, Node* data)
{
    Node* r = root(ctx);
    std::string path = normalizePath(params->get("path").toString());
    Node* target = path.empty() ? r : r->find(path);
    if (!target) return Result::fail(ERR_NOT_FOUND, "not found: " + path);
    int depth = params->get("depth").toInt(-1);
    data->at("tree")->copy(target, Node::COPY_DEFAULT, depth);
    return Result::ok();
}

static Result children(Node* ctx, Node* params, Node* data)
{
    Node* r = root(ctx);
    std::string path = normalizePath(params->get("path").toString());
    Node* target = path.empty() ? r : r->find(path);
    if (!target) return Result::fail(ERR_NOT_FOUND, "not found: " + path);
    Node* list = data->at("children");
    for (auto* child : target->children()) {
        Node* item = list->append();
        item->set("name", child->name());
        item->set("path", child->path(r));
        if (!child->get().isNull()) item->at("value")->set(child->get());
        item->set("child_count", static_cast<int64_t>(child->count()));
    }
    return Result::ok();
}

static Result erase(Node* ctx, Node* params, Node* data)
{
    Node* r = root(ctx);
    std::string path = params->get("path").toString();
    if (path.empty()) return Result::fail(ERR_INVALID, "cannot erase root");
    if (!r->erase(path)) return Result::fail(ERR_NOT_FOUND, "not found: " + path);
    data->set("path", path);
    return Result::ok();
}

static Result trigger(Node* ctx, Node* params, Node*)
{
    Node* r = root(ctx);
    std::string path = params->get("path").toString();
    Node* target = r->find(path);
    if (!target) return Result::fail(ERR_NOT_FOUND, "not found: " + path);
    target->trigger<Node::NODE_CHANGED>();
    return Result::ok();
}

static Result subscribe(Node* ctx, Node* params, Node* data)
{
    Session* s = session(ctx);
    if (!s->send) return Result::fail(ERR_UNSUPPORTED, "subscribe not supported");
    Node* r = s->root;
    std::string path = normalizePath(params->get("path").toString());
    Node* target = path.empty() ? r : r->find(path);
    if (!target) return Result::fail(ERR_NOT_FOUND, "not found: " + path);
    int depth = params->get("depth").toInt(-1);

    auto emit = [s, r, target, depth]() {
        Node event;
        event.set("event", "node.changed");
        event.set("path", target->path(r));
        event.at("data")->copy(target, Node::COPY_DEFAULT, depth);
        s->send(toJson(event));
    };

    if (params->get("once").toBool(false)) {
        target->once<Node::NODE_CHANGED>(s, emit);
    } else {
        target->onChanged(s, emit);
    }

    if (params->get("immediate").toBool(false))
        data->copy(target, Node::COPY_DEFAULT, depth);

    return Result::ok();
}

static Result unsubscribe(Node* ctx, Node* params, Node*)
{
    Session* s = session(ctx);
    Node* r = s->root;
    std::string path = normalizePath(params->get("path").toString());
    Node* target = path.empty() ? r : r->find(path);
    if (!target) return Result::fail(ERR_NOT_FOUND, "not found: " + path);
    target->disconnect(s);
    return Result::ok();
}

static Result commandList(Node*, Node*, Node* data)
{
    Node* commands = data->at("commands");
    for (const auto& key : command::factory().keys()) {
        Node* item = commands->append();
        item->set("name", key);
        item->set("help", command::factory().help(key));
    }
    return Result::ok();
}

// Self-description: the command's describe subtree (description / usage /
// input_schema / output_schema). Looks up std factory first, then cmd factory.
static Result describe(Node*, Node* params, Node* data)
{
    std::string name = params->get("name").toString();
    if (name.empty()) return Result::fail(ERR_INVALID, "name required");

    const Factory& sf = factory::at("service/op");
    const Factory& cf = command::factory();
    Node* n = sf.node(name);
    if (!n || !n->get().isCallable()) n = cf.node(name);
    if (!n || !n->get().isCallable()) return Result::fail(ERR_NOT_FOUND, "not found: " + name);

    data->set("name", name);
    data->copy(n->find("describe")); // describe subtree; copy() no-ops on null
    return Result::ok();
}

} // namespace op

// ============================================================================
// Registration + helpers
// ============================================================================

// Command docs (description / usage / input_schema / output_schema) are embedded
// via ve_embed_files and parsed once; each command's describe subtree is attached
// to its registered node at <key>/describe.
void registerNodeCommands()
{
    auto& f = factory::at("service/op");
    if (f.has("get")) return;

    Node docs;
    schema::JsonS::toNode(&docs, std::string(ve::res::read("ve/service/op.json")));

    auto R = [&](const char* key, Var callable) {
        Node* n = f.reg(key, std::move(callable));
        n->at("describe")->copy(docs.find(key)); // copy() no-ops on null
    };

    R("get",         Var::callable(op::get));
    R("set",         Var::callable(op::set));
    R("export",      Var::callable(op::export_));
    R("import",      Var::callable(op::import_));
    R("children",    Var::callable(op::children));
    R("erase",       Var::callable(op::erase));
    R("trigger",     Var::callable(op::trigger));
    R("subscribe",   Var::callable(op::subscribe));
    R("unsubscribe", Var::callable(op::unsubscribe));
    R("commands",    Var::callable(op::commandList));
    R("describe",    Var::callable(op::describe));
}

CmdRef resolveCmd(Node* ctx)
{
    std::string op = ctx->get("op").toString();
    if (!op.empty()) {
        Factory& f = factory::at("service/op");
        return {f.has(op) ? &f : nullptr, op};
    }
    std::string cmd = ctx->get("cmd").toString();
    if (!cmd.empty()) {
        Factory& f = command::factory();
        return {f.has(cmd) ? &f : nullptr, cmd};
    }
    return {nullptr, {}};
}

bool finalizeReply(Pipeline& pipe)
{
    if (pipe.result().isAccepted()) return false;
    Node* ctx = pipe.contextNode();
    ctx->erase("op");
    ctx->erase("cmd");
    ctx->erase("batch");
    ctx->erase("params");
    ctx->set("code", static_cast<std::int64_t>(pipe.result().code()));
    if (pipe.result().isError()) {
        ctx->erase("data");
        if (!pipe.result().message().empty())
            ctx->set("message", pipe.result().message());
    }
    return true;
}

} // namespace service
} // namespace ve
