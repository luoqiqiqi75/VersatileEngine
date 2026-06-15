// node_commands.cpp — envelope v2: cmd/params → code/message/data
//
// Command implementations registered in factory::at("std").
// resolveCmd() parses optional "prefix:key" from the envelope cmd field.
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
        Node* sub = data->append();
        std::string cmd_str = item->get("cmd").toString();
        auto ref = resolveCmd(cmd_str);
        if (!ref.factory) {
            sub->set("code", int64_t(ERR_NOT_FOUND));
            sub->set("message", "unknown: " + cmd_str);
            continue;
        }
        Pipeline pipe;
        pipe.contextNode()->set("_session", Var::ptr(s));
        Command* c = pipe.add(command::create(*ref.factory, ref.key));
        c->setContextNodes(pipe.contextNode(), item->at("params"), sub);
        pipe.sync();
        sub->set("code", int64_t(pipe.result().code()));
        if (pipe.result().isError() && !pipe.result().message().empty())
            sub->set("message", pipe.result().message());
    }
    return Result::ok();
}

} // namespace cmd

// ============================================================================
// Registration + helpers
// ============================================================================

void registerNodeCommands()
{
    auto& f = factory::at("std");
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

CmdRef resolveCmd(const std::string& cmd)
{
    auto pos = cmd.find(':');
    if (pos != std::string::npos) {
        std::string prefix = cmd.substr(0, pos);
        std::string key = cmd.substr(pos + 1);
        Factory& f = factory::at(prefix);
        return {f.has(key) ? &f : nullptr, key};
    }
    Factory& stdF = factory::at("std");
    if (stdF.has(cmd)) return {&stdF, cmd};
    Factory& cmdF = command::factory();
    if (cmdF.has(cmd)) return {&cmdF, cmd};
    return {nullptr, cmd};
}

bool finalizeReply(Pipeline& pipe)
{
    if (pipe.result().isAccepted()) return false;
    Node* ctx = pipe.contextNode();
    ctx->erase("cmd");
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
