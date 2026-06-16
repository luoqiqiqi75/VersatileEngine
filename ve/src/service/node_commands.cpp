// node_commands.cpp — envelope v2: op|cmd/params → code/message/data
//
// Command implementations registered in factory::at("std").
// resolveCmd() reads "op" (std factory) or "cmd" (command factory) from ctx.
// finalizeReply() mutates a Pipeline's contextNode into reply format.

#include "node_commands.h"

#include "ve/core/node.h"
#include "ve/core/command.h"
#include "ve/core/schema.h"
#include "ve/core/pipeline.h"

namespace ve {
namespace service {

static std::string toJson(const Node& n)
{
    return schema::exportAs<schema::JsonS>(&n, schema::JsonS::compact());
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
    int flags = params->get("flags").toInt(Node::COPY_DEFAULT);
    Node* target = r->at(params->get("path").toString());
    target->copy(tree, flags);
    data->set("path", target->path(r));
    return Result::ok();
}

static void copyDepth(Node* dst, Node* src, int depth)
{
    if (!src->get().isNull())
        dst->set(src->get());
    if (depth == 0) return;
    int next = depth > 0 ? depth - 1 : -1;
    for (auto* child : src->children())
        copyDepth(dst->at(child->name()), child, next);
}

static Result export_(Node* ctx, Node* params, Node* data)
{
    Node* r = root(ctx);
    std::string path = normalizePath(params->get("path").toString());
    Node* target = path.empty() ? r : r->find(path);
    if (!target) return Result::fail(ERR_NOT_FOUND, "not found: " + path);
    int depth = params->get("depth").toInt(-1);
    copyDepth(data->at("tree"), target, depth);
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

static Result trigger(Node* ctx, Node* params, Node* data)
{
    Node* r = root(ctx);
    std::string path = params->get("path").toString();
    Node* target = r->find(path);
    if (!target) return Result::fail(ERR_NOT_FOUND, "not found: " + path);
    target->trigger<Node::NODE_CHANGED>();
    data->set("path", target->path(r));
    return Result::ok();
}

static Result watch(Node* ctx, Node* params, Node* data)
{
    Session* s = session(ctx);
    if (!s->send) return Result::fail(ERR_UNSUPPORTED, "watch not supported");
    Node* r = s->root;
    std::string path = normalizePath(params->get("path").toString());
    Node* target = path.empty() ? r : r->find(path);
    if (!target) return Result::fail(ERR_NOT_FOUND, "not found: " + path);
    target->onChanged(s, [s, r, target]() {
        Node event;
        event.set("event", "node.changed");
        event.set("path", target->path(r));
        event.at("value")->set(target->get());
        s->send(toJson(event));
    });
    data->set("path", target->path(r));
    return Result::ok();
}

static Result unwatch(Node* ctx, Node* params, Node* data)
{
    Session* s = session(ctx);
    Node* r = s->root;
    std::string path = normalizePath(params->get("path").toString());
    Node* target = path.empty() ? r : r->find(path);
    if (!target) return Result::fail(ERR_NOT_FOUND, "not found: " + path);
    target->disconnect(s);
    data->set("path", target->path(r));
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

} // namespace op

// ============================================================================
// Registration + helpers
// ============================================================================

void registerNodeCommands()
{
    auto& f = factory::at("std");
    if (f.has("get")) return;
    f.reg("get",      Var::callable(op::get),         "get node value");
    f.reg("set",      Var::callable(op::set),         "set node value");
    f.reg("export",   Var::callable(op::export_),     "export subtree");
    f.reg("import",   Var::callable(op::import_),     "import tree");
    f.reg("children", Var::callable(op::children),    "list children");
    f.reg("erase",    Var::callable(op::erase),       "erase node");
    f.reg("trigger",  Var::callable(op::trigger),     "trigger NODE_CHANGED");
    f.reg("watch",    Var::callable(op::watch),       "watch node changes");
    f.reg("unwatch",  Var::callable(op::unwatch),     "stop watching");
    f.reg("commands", Var::callable(op::commandList), "list commands");
}

CmdRef resolveCmd(Node* ctx)
{
    std::string op = ctx->get("op").toString();
    if (!op.empty()) {
        Factory& f = factory::at("std");
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
