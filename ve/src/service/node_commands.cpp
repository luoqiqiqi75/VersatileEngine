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
        item->set("help", command::description(key));
    }
    return Result::ok();
}

// Self-description: the command's instruction subtree (description / usage /
// input_schema / output_schema). Looks up service/op factory first, then cmd factory.
static Result describe(Node*, Node* params, Node* data)
{
    std::string name = params->get("name").toString();
    if (name.empty()) return Result::fail(ERR_INVALID, "name required");

    const Factory& sf = factory::at("service/op");
    const Factory& cf = command::factory();
    Node* n = sf.node(name, VE_FACTORY_KEY_SEP);
    if (!n || !n->get().isCallable()) n = cf.node(name, VE_FACTORY_KEY_SEP);
    if (!n || !n->get().isCallable()) return Result::fail(ERR_NOT_FOUND, "not found: " + name);

    data->set("name", name);
    data->copy(n->find("instruction"));
    // Expand local #/definitions $ref so clients see concrete schemas.
    // Prefer the factory that actually owns the command node.
    Node* root = (sf.node(name, VE_FACTORY_KEY_SEP) == n) ? sf.node() : cf.node();
    command::resolveSchemas(data, root);
    return Result::ok();
}

} // namespace op

// ============================================================================
// Registration + helpers
// ============================================================================

// Command docs (description / usage / input_schema / output_schema) are embedded
// via ve_embed_files and parsed once; each command's instruction subtree is attached
// to its registered node at <key>/instruction.
VE_API void registerNodeCommands(Factory& op_f)
{
    auto R = [&](const char* key, auto callable) { op_f.reg(key, std::move(Var::callable(callable))); };

    R("get",         op::get);
    R("set",         op::set);
    R("export",      op::export_);
    R("import",      op::import_);
    R("children",    op::children);
    R("erase",       op::erase);
    R("trigger",     op::trigger);
    R("subscribe",   op::subscribe);
    R("unsubscribe", op::unsubscribe);
    R("commands",    op::commandList);
    R("describe",    op::describe);
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

static bool formatReply(Node* ctx, const Result& result)
{
    if (result.isAccepted()) return false;
    ctx->erase("op");
    ctx->erase("cmd");
    ctx->erase("batch");
    ctx->erase("params");
    ctx->erase("async");
    ctx->set("code", static_cast<std::int64_t>(result.code()));
    if (result.isError()) {
        ctx->erase("data");
        if (!result.message().empty())
            ctx->set("message", result.message());
    }
    return true;
}

bool finalizeReply(Pipeline& pipe)
{
    return formatReply(pipe.contextNode(), pipe.result());
}

Result executeNodeRequest(Pipeline pipe, const std::shared_ptr<Session>& session, NodeReply reply,
                          bool push_async_result)
{
    Node* ctx = pipe.contextNode();
    ctx->set("_session", Var::ptr(session.get()));
    const bool background = ctx->get("async").toBool(false);
    std::vector<Command> commands;
    auto add = [&](Node* item, Node* out) {
        auto ref = resolveCmd(item);
        if (!ref.factory) {
            ctx->erase("batch");
            ctx->set("code", int64_t(ref.key.empty() ? ERR_INVALID : ERR_NOT_FOUND));
            ctx->set("message", ref.key.empty() ? std::string("op or cmd required") : "unknown: " + ref.key);
            reply(*ctx);
            return false;
        }
        Command command = command::create(*ref.factory, ref.key);
        command.setContextNodes(ctx, item->at("params"), out);
        if (background && !command.loop()) command.setLoop(loop::current());
        if (!background && command.loop() == loop::current()) command.setLoop(nullptr);
        commands.push_back(std::move(command));
        return true;
    };
    if (Node* batch = ctx->find("batch")) {
        Node* out = ctx->at("data");
        for (auto* item : batch->children())
            if (!add(item, out->append())) return Result::fail(ctx->get("code").toInt(), ctx->get("message").toString());
    } else if (!add(ctx, ctx->at("data"))) {
        return Result::fail(ctx->get("code").toInt(), ctx->get("message").toString());
    }

    if (!background) {
        Result result = Result::ok();
        // Advance on the connection thread so an unbound batch step cannot
        // inherit the registered loop of the preceding step.
        for (auto& command : commands) {
            std::promise<Result> completed;
            auto finished = completed.get_future();
            command.call([&completed](Command& command) { completed.set_value(command.result()); });
            result = finished.get();
            if (!result.isSuccess()) break;
        }
        if (formatReply(ctx, result)) reply(*ctx);
        return result;
    }

    for (auto& command : commands) pipe.add(std::move(command));
    // HTTP has a single acknowledgement response. Its response callback must
    // not be retained by the background command after the handler returns.
    NodeReply result_reply = push_async_result ? reply : NodeReply{};
    auto token = session->beginAsync();
    pipe.onFinished(nullptr, [session, token, result_reply](Pipeline& pipe) {
        if (finalizeReply(pipe) && result_reply) result_reply(*pipe.contextNode());
    });
    Node accepted;
    if (auto id = ctx->find("id")) accepted.at("id")->copy(id);
    accepted.set("code", 0);
    accepted.set("accepted", true);
    reply(accepted);
    pipe.async();
    return Result::accept();
}

} // namespace service
} // namespace ve
