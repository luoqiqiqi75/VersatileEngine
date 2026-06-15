// node_commands.cpp — envelope v2: cmd/params → code/message/data
//
// One dispatch entry: resolve cmd in standard/node then global factory,
// create+run through Command, mutate ctx into the reply.  Batch is an
// ordinary registered command that recurses through the same dispatch.

#include "node_commands.h"
#include "node_session.h"

#include "ve/core/node.h"
#include "ve/core/command.h"
#include "ve/core/schema.h"

namespace ve {
namespace service {

static const schema::ExportOptions<schema::JsonS> compactJson{0};

static std::string toJson(const Node& n)
{
    return schema::exportAs<schema::JsonS>(&n, compactJson);
}

static std::string normalizePath(std::string path)
{
    while (!path.empty() && path.front() == VE_NODE_PATH_SEP) path.erase(path.begin());
    return path;
}

static bool dispatchImpl(Session* session, Node* ctx);

// ============================================================================
// Command implementations — Proc(ctx, params, data)
// ============================================================================
namespace cmd {

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

static Result copy(Node* ctx, Node* params, Node* data)
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

static Result batch(Node* ctx, Node* params, Node* data)
{
    Session* s = session(ctx);
    if (params->count() > 500)
        return Result::fail(ERR_INVALID, "batch exceeds limit");
    for (auto* item : params->children()) {
        Node sub;
        sub.copy(item);
        dispatchImpl(s, &sub);
        data->append()->copy(&sub);
    }
    return Result::ok();
}

} // namespace cmd

// ============================================================================
// Registration + dispatch
// ============================================================================

void registerNodeCommands()
{
    auto& f = factory::at("standard/node");
    if (f.has("node.get")) return;
    f.reg("node.get",      Var::callable(cmd::get),         "get node value");
    f.reg("node.set",      Var::callable(cmd::set),         "set node value");
    f.reg("node.copy",     Var::callable(cmd::copy),        "copy tree to node");
    f.reg("node.children", Var::callable(cmd::children),    "list children");
    f.reg("node.erase",    Var::callable(cmd::erase),       "erase node");
    f.reg("node.trigger",  Var::callable(cmd::trigger),     "trigger NODE_CHANGED");
    f.reg("node.watch",    Var::callable(cmd::watch),       "watch node changes");
    f.reg("node.unwatch",  Var::callable(cmd::unwatch),     "stop watching");
    f.reg("command.list",  Var::callable(cmd::commandList), "list commands");
    f.reg("batch",         Var::callable(cmd::batch),       "run batch requests");
}

static void setError(Node* ctx, int code, const std::string& message)
{
    ctx->erase("cmd");
    ctx->erase("params");
    ctx->set("code", static_cast<int64_t>(code));
    ctx->set("message", message);
}

static bool dispatchImpl(Session* session, Node* ctx)
{
    std::string cmd = ctx->get("cmd").toString();
    if (cmd.empty()) {
        setError(ctx, ERR_INVALID, "cmd required");
        return true;
    }

    Factory& nodeF = factory::at("standard/node");
    Factory* f = nodeF.has(cmd) ? &nodeF : nullptr;
    if (!f) {
        Factory& cmdF = command::factory();
        if (cmdF.has(cmd)) f = &cmdF;
    }
    if (!f) {
        setError(ctx, ERR_NOT_FOUND, "unknown: " + cmd);
        return true;
    }

    ctx->at("_session")->set(Var::ptr(session));
    Command c = command::create(*f, cmd, ctx, ctx->at("params"), ctx->at("data"));
    c.run();

    if (c.result().isAccepted()) return false;

    ctx->erase("cmd");
    ctx->erase("params");
    ctx->set("code", static_cast<int64_t>(c.result().code()));
    if (c.result().isError()) {
        ctx->erase("data");
        if (!c.result().message().empty())
            ctx->set("message", c.result().message());
    }
    return true;
}

bool dispatch(Session* session, Node* ctx)
{
    return dispatchImpl(session, ctx);
}

} // namespace service
} // namespace ve
