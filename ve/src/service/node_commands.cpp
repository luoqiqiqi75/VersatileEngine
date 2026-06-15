// node_commands.cpp — shared node command family + one factory/pipeline entry
//
// One way to run an op: resolve it in factory "standard/node", run it through a
// pipeline, render the reply from the command's output + Result. There is no
// per-op branching and no special-cased batch — `batch` is itself a command that
// recurses through the same entry, so nested batches and deep errors all surface
// through the normal Result path.

#include "node_commands.h"
#include "node_session.h"

#include "ve/core/node.h"
#include "ve/core/command.h"
#include "ve/core/impl/json.h"

namespace ve {
namespace service {

static const schema::ExportOptions<schema::JsonS> compactJson{0};

// ============================================================================
// Error codes carried by Result; codeString() maps them to protocol strings.
// ============================================================================
enum Err : int {
    ERR_INVALID_REQUEST = -600,
    ERR_UNKNOWN_OP      = -601,
    ERR_INVALID_PARAMS  = -400,
    ERR_NOT_FOUND       = -404,
    ERR_UNSUPPORTED     = -405,
};

static std::string codeString(int code)
{
    switch (code) {
        case ERR_INVALID_REQUEST: return "invalid_request";
        case ERR_UNKNOWN_OP:      return "unknown_op";
        case ERR_INVALID_PARAMS:  return "invalid_params";
        case ERR_NOT_FOUND:       return "not_found";
        case ERR_UNSUPPORTED:     return "unsupported";
        default:                  return "failed";
    }
}

// ============================================================================
// Render helpers (writeNodeMeta / exportNodeTree exposed via header)
// ============================================================================

static std::string normalizePath(std::string path)
{
    while (!path.empty() && path.front() == VE_NODE_PATH_SEP) path.erase(path.begin());
    return path;
}

Var exportNodeTree(Node* target, int depth, const schema::ExportOptions<schema::JsonS>& options)
{
    if (depth < 0) return schema::exportAs<schema::VarS>(target, schema::ExportOptions<schema::VarS>{options.auto_ignore});
    return impl::json::parse(impl::json::exportTree(target, depth, options.indent, options.auto_ignore));
}

void writeNodeMeta(Node* out, Node* root, Node* target)
{
    out->set("path", target->path(root));
    out->set("type", static_cast<int64_t>(target->get().type()));
    out->set("child_count", static_cast<int64_t>(target->count()));
    if (target->parent()) out->set("parent_path", target->parent()->path(root));
}

static void makeChildInfo(Node* out, Node* root, Node* child, bool withMeta)
{
    out->set("name", child->name());
    out->set("path", child->path(root));
    out->set("has_value", !child->get().isNull());
    out->set("child_count", static_cast<int64_t>(child->count()));
    if (withMeta) out->set("type", static_cast<int64_t>(child->get().type()));
}

static void writeNodeGetData(Node* data, Node* root, Node* in, Node* target)
{
    data->set("path", target->path(root));
    data->at("value")->set(target->get());
    if (in->find("depth"))
        data->at("tree")->set(exportNodeTree(target, in->get("depth").toInt(-1), compactJson));
    if (in->get("meta").toBool(false))
        writeNodeMeta(data->at("meta"), root, target);
}

// The shared node-protocol reply envelope. Services serialize/frame this Node
// their own way (json / bin frame / http status / jsonrpc wrap) — only the
// envelope shape is shared, the transport encoding is not.
static void renderReply(Node* rep, const Var& id, Node* data, const Result& r)
{
    rep->clear();
    if (!id.isNull()) rep->at("id")->set(id);
    if (r.isError()) {
        rep->set("ok", false);
        rep->set("code", codeString(r.code()));
        rep->set("error", r.message());
    } else {
        rep->set("ok", true);
        if (data && data->count()) rep->at("data")->copy(data, Node::COPY_STRICT | Node::COPY_UPDATE);
    }
}

// runOp() is the single entry; batch recurses through it.
static void runOp(Node* root, Node* req, Node* rep, Session* session, int batchLimit);

// ============================================================================
// standard/node command family — Proc(ctx, in, out).
//   in carries the request plus the essentials a command needs: in/root (Node*),
//   in/session (Session*, optional), in/batch_limit. out is the reply data.
//   Errors are returned as Result(code, message) — never written to out.
// ============================================================================
namespace cmd {

static Node*    reqRoot(Node* in)    { return in->get("root").as<Node*>(); }
static Session* reqSession(Node* in) { return in->get("session").as<Session*>(); }

static Result get(Node*, Node* in, Node* out)
{
    Node* r = reqRoot(in);
    std::string path = normalizePath(in->get("path").toString());
    Node* target = path.empty() ? r : (r ? r->find(path) : nullptr);
    if (!target) return Result::fail(ERR_NOT_FOUND, "node not found: " + path);
    writeNodeGetData(out, r, in, target);
    return Result::ok();
}

static Result list(Node*, Node* in, Node* out)
{
    Node* r = reqRoot(in);
    std::string path = in->get("path").toString();
    Node* target = r ? r->find(path) : nullptr;
    if (!target) return Result::fail(ERR_NOT_FOUND, "node not found: " + path);
    out->set("path", target->path(r));
    Node* children = out->at("children");
    for (auto* child : target->children())
        makeChildInfo(children->append(), r, child, in->get("meta").toBool(false));
    return Result::ok();
}

static Result set(Node*, Node* in, Node* out)
{
    Node* valueNode = in->find("value");
    if (!valueNode) return Result::fail(ERR_INVALID_PARAMS, "value is required");
    Node* target = reqRoot(in)->at(in->get("path").toString());
    target->copy(valueNode);
    out->set("path", target->path(reqRoot(in)));
    return Result::ok();
}

static Result put(Node*, Node* in, Node* out)
{
    Node* treeNode = in->find("tree");
    if (!treeNode) treeNode = in->find("value");
    if (!treeNode) return Result::fail(ERR_INVALID_PARAMS, "tree is required");
    Node* target = reqRoot(in)->at(in->get("path").toString());
    target->copy(treeNode, Node::COPY_STRICT);   // replace subtree (insert + remove stale)
    out->set("path", target->path(reqRoot(in)));
    return Result::ok();
}

static Result remove(Node*, Node* in, Node* out)
{
    Node* r = reqRoot(in);
    std::string path = in->get("path").toString();
    if (path.empty()) return Result::fail(ERR_INVALID_PARAMS, "cannot remove root");
    if (!r->erase(path)) return Result::fail(ERR_NOT_FOUND, "node not found: " + path);
    out->set("path", path);
    return Result::ok();
}

static Result trigger(Node*, Node* in, Node* out)
{
    Node* r = reqRoot(in);
    std::string path = in->get("path").toString();
    Node* target = r->find(path);
    if (!target) return Result::fail(ERR_NOT_FOUND, "node not found: " + path);
    target->trigger<Node::NODE_CHANGED>();
    out->set("path", target->path(r));
    return Result::ok();
}

static Result commandList(Node*, Node*, Node* out)
{
    Node* commands = out->at("commands");
    for (const auto& key : command::factory().keys()) {
        Node* item = commands->append();
        item->set("name", key);
        item->set("help", command::factory().help(key));
    }
    return Result::ok();
}

static Result subscribe(Node*, Node* in, Node* out)
{
    Session* s = reqSession(in);
    if (!s) return Result::fail(ERR_UNSUPPORTED, "subscriptions are not supported on this transport");
    std::string path = in->get("path").toString();
    s->subscribe(path, in->get("bubble").toBool(false), in->get("tree").toBool(false));
    out->set("path", path);
    out->set("subscribed", true);
    return Result::ok();
}

static Result unsubscribe(Node*, Node* in, Node* out)
{
    Session* s = reqSession(in);
    if (!s) return Result::fail(ERR_UNSUPPORTED, "subscriptions are not supported on this transport");
    std::string path = in->get("path").toString();
    s->unsubscribe(path);
    out->set("path", path);
    out->set("subscribed", false);
    return Result::ok();
}

// batch is an ordinary command: it runs each item through the same runOp entry
// (so an item may itself be a batch) and collects each rendered reply into out.
static Result batch(Node*, Node* in, Node* out)
{
    Node* items = in->find("items");
    if (!items) return Result::fail(ERR_INVALID_PARAMS, "items array required");
    int limit = in->get("batch_limit").toInt(500);
    if (limit > 0 && items->count() > limit)
        return Result::fail(ERR_INVALID_PARAMS, "batch size exceeds limit");
    for (auto* item : items->children())
        runOp(reqRoot(in), item, out->append(), reqSession(in), limit);
    return Result::ok();
}

} // namespace cmd

void registerNodeCommands()
{
    auto& f = factory::at("standard/node");
    if (f.has("node.get")) return;   // register-once
    f.reg("node.get",     Var::callable(cmd::get),         "read a node (value + optional tree/meta)");
    f.reg("node.list",    Var::callable(cmd::list),        "list children");
    f.reg("node.set",     Var::callable(cmd::set),         "set a node value");
    f.reg("node.put",     Var::callable(cmd::put),         "replace a subtree");
    f.reg("node.remove",  Var::callable(cmd::remove),      "remove a node");
    f.reg("node.trigger", Var::callable(cmd::trigger),     "trigger NODE_CHANGED");
    f.reg("command.list", Var::callable(cmd::commandList), "list registered commands");
    f.reg("subscribe",    Var::callable(cmd::subscribe),   "subscribe to a node (session transports)");
    f.reg("unsubscribe",  Var::callable(cmd::unsubscribe), "unsubscribe from a node");
    f.reg("batch",        Var::callable(cmd::batch),       "run multiple requests");
}

// ============================================================================
// The single entry: resolve op via factory, run the command, render.
// ============================================================================
static void runOp(Node* root, Node* req, Node* rep, Session* session, int batchLimit)
{
    Var id = req->get("id");
    std::string op = req->get("op").toString();

    Factory& f = factory::at("standard/node");
    if (op.empty()) {
        renderReply(rep, id, nullptr, Result::fail(ERR_INVALID_REQUEST, "op is required"));
        return;
    }
    if (!f.has(op)) {
        renderReply(rep, id, nullptr, Result::fail(ERR_UNKNOWN_OP, "unknown op: " + op));
        return;
    }

    // Single synchronous command — no pipeline needed.
    Command cmd = command::create(f, op);
    cmd.inputNode()->copy(req, Node::COPY_STRICT | Node::COPY_UPDATE);
    cmd.inputNode()->at("root")->set(Var::ptr(root));
    if (session) cmd.inputNode()->at("session")->set(Var::ptr(session));
    cmd.inputNode()->set("batch_limit", static_cast<int64_t>(batchLimit));

    cmd.run();
    renderReply(rep, id, cmd.outputNode(), cmd.result());
}

void dispatchNode(Node* root, Node* req, Node* rep, Session* session, int batchLimit)
{
    runOp(root, req, rep, session, batchLimit);
}

} // namespace service
} // namespace ve
