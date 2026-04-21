#include "node_protocol.h"

#include "node_task_service.h"
#include "subscribe_service.h"

#include "ve/core/command.h"
#include "ve/core/node.h"
#include "ve/core/pipeline.h"
#include "ve/core/schema.h"
#include "ve/core/impl/json.h"

#include <algorithm>

namespace ve {
namespace service {

static const schema::ExportOptions compactJson{0};

static void okReply(Node* rep, const Var& id)
{
    rep->clear();
    rep->set("ok", true);
    if (!id.isNull()) {
        rep->at("id")->set(id);
    }
}

static void okReply(Node* rep, const Var& id, Node* data)
{
    okReply(rep, id);
    if (data) {
        rep->at("data")->copy(data);
    }
}

static void okReply(Node* rep, const Var& id, const Var& data)
{
    okReply(rep, id);
    rep->at("data")->set(data);
}

static void acceptedReply(Node* rep, const Var& id, const std::string& taskId)
{
    okReply(rep, id);
    rep->set("accepted", true);
    rep->set("task_id", taskId);
}

static void errorReply(Node* rep, const Var& id, const std::string& code, const std::string& error)
{
    rep->clear();
    rep->set("ok", false);
    if (!id.isNull()) {
        rep->at("id")->set(id);
    }
    rep->set("code", code);
    rep->set("error", error);
}

static Node* findNode(Node* root, Node* req)
{
    return root->find(req->get("path").toString());
}

static Node* ensureNode(Node* root, Node* req)
{
    return root->at(req->get("path").toString());
}

static Var exportTreeVar(Node* target, int depth)
{
    if (depth < 0) {
        return schema::exportAs<schema::VarS>(target);
    }
    return impl::json::parse(impl::json::exportTree(target, depth, compactJson));
}

static void makeNodeMeta(Node* out, Node* root, SubscribeService* subscribe, Node* target)
{
    out->set("type", static_cast<int64_t>(target->get().type()));
    out->set("child_count", static_cast<int64_t>(target->count()));
    out->set("has_shadow", target->shadow() != nullptr);
    out->set("subscribers", static_cast<int64_t>(
        subscribe ? subscribe->getSubscriberCount(target->path(root)) : 0));
    if (target->parent()) {
        out->set("parent_path", target->parent()->path(root));
    }
}

static void makeChildInfo(Node* out, Node* root, SubscribeService* subscribe, Node* child, bool withMeta)
{
    std::string childPath = child->path(root);
    out->set("name", child->name());
    out->set("path", childPath);
    out->set("has_value", !child->get().isNull());
    out->set("child_count", static_cast<int64_t>(child->count()));
    if (withMeta) {
        out->set("type", static_cast<int64_t>(child->get().type()));
        out->set("subscribers", static_cast<int64_t>(
            subscribe ? subscribe->getSubscriberCount(childPath) : 0));
    }
}

static void dispatchNodeProtocolInternal(Node* root, Node* req, Node* rep,
                                        SubscribeService* subscribe, NodeTaskService* tasks,
                                        int batchLimit, bool allowSubscriptions,
                                        uint64_t sessionId, bool allowAsyncEvents,
                                        const NodeEventFn& sendEvent);

static void dispatchBatch(Node* root, Node* req, Node* rep,
                          SubscribeService* subscribe, NodeTaskService* tasks,
                          int batchLimit, bool allowSubscriptions,
                          uint64_t sessionId)
{
    Node* itemsNode = req->find("items");
    Var id = req->get("id");
    if (!itemsNode) {
        errorReply(rep, id, "invalid_params", "items array required");
        return;
    }
    if (batchLimit > 0 && itemsNode->count() > batchLimit) {
        errorReply(rep, id, "invalid_params", "batch size exceeds limit");
        return;
    }

    Node items("items");
    for (auto* child : itemsNode->children()) {
        dispatchNodeProtocolInternal(root, child, items.append(), subscribe, tasks,
                                     batchLimit, allowSubscriptions, sessionId,
                                     false, {});
    }
    okReply(rep, id, &items);
}

static void dispatchNodeProtocolInternal(Node* root, Node* req, Node* rep,
                                        SubscribeService* subscribe, NodeTaskService* tasks,
                                        int batchLimit, bool allowSubscriptions,
                                        uint64_t sessionId, bool allowAsyncEvents,
                                        const NodeEventFn& sendEvent)
{
    if (!root || !req || !rep) {
        return;
    }

    Var id = req->get("id");
    std::string op = req->get("op").toString();
    if (op.empty()) {
        errorReply(rep, id, "invalid_request", "op is required");
        return;
    }

    if (op == "batch") {
        dispatchBatch(root, req, rep, subscribe, tasks, batchLimit, allowSubscriptions, sessionId);
        return;
    }

    if (op == "node.get") {
        std::string path = req->get("path").toString();
        Node* target = findNode(root, req);
        if (!target) {
            errorReply(rep, id, "not_found", "node not found: " + path);
            return;
        }

        Node data("data");
        data.set("path", target->path(root));
        data.at("value")->set(target->get());
        if (req->find("depth")) {
            data.at("tree")->set(exportTreeVar(target, req->get("depth").toInt(-1)));
        }
        if (req->get("meta").toBool(false)) {
            makeNodeMeta(data.at("meta"), root, subscribe, target);
        }
        okReply(rep, id, &data);
        return;
    }

    if (op == "node.list") {
        std::string path = req->get("path").toString();
        Node* target = findNode(root, req);
        if (!target) {
            errorReply(rep, id, "not_found", "node not found: " + path);
            return;
        }

        bool withMeta = req->get("meta").toBool(false);
        Node data("data");
        data.set("path", target->path(root));
        Node* children = data.at("children");
        for (auto* child : target->children()) {
            makeChildInfo(children->append(), root, subscribe, child, withMeta);
        }
        okReply(rep, id, &data);
        return;
    }

    if (op == "node.set") {
        Node* valueNode = req->find("value");
        if (!valueNode) {
            errorReply(rep, id, "invalid_params", "value is required");
            return;
        }

        Node* target = ensureNode(root, req);
        target->set(schema::exportAs<schema::VarS>(valueNode));

        Node data("data");
        data.set("path", target->path(root));
        okReply(rep, id, &data);
        return;
    }

    if (op == "node.put") {
        Node* treeNode = req->find("tree");
        if (!treeNode) {
            treeNode = req->find("value");
        }
        if (!treeNode) {
            errorReply(rep, id, "invalid_params", "tree is required");
            return;
        }

        Node* target = ensureNode(root, req);
        schema::ImportOptions importOptions;
        importOptions.auto_insert = true;
        importOptions.auto_remove = true;
        importOptions.auto_update = true;
        schema::importAs<schema::VarS>(target, schema::exportAs<schema::VarS>(treeNode), importOptions);

        Node data("data");
        data.set("path", target->path(root));
        okReply(rep, id, &data);
        return;
    }

    if (op == "node.remove") {
        std::string path = req->get("path").toString();
        if (path.empty()) {
            errorReply(rep, id, "invalid_params", "cannot remove root");
            return;
        }
        if (!root->erase(path)) {
            errorReply(rep, id, "not_found", "node not found: " + path);
            return;
        }

        Node data("data");
        data.set("path", path);
        okReply(rep, id, &data);
        return;
    }

    if (op == "node.trigger") {
        std::string path = req->get("path").toString();
        Node* target = findNode(root, req);
        if (!target) {
            errorReply(rep, id, "not_found", "node not found: " + path);
            return;
        }

        target->trigger<Node::NODE_CHANGED>();
        if (target->isWatching()) {
            target->activate(Node::NODE_CHANGED, target);
        }

        Node data("data");
        data.set("path", target->path(root));
        okReply(rep, id, &data);
        return;
    }

    if (op == "command.list") {
        auto cmds = command::keys();
        std::sort(cmds.begin(), cmds.end());
        cmds.erase(std::unique(cmds.begin(), cmds.end()), cmds.end());

        Node data("data");
        Node* commands = data.at("commands");
        for (const auto& key : cmds) {
            Node* item = commands->append();
            item->set("name", key);
            item->set("help", command::help(key));
        }
        okReply(rep, id, &data);
        return;
    }

    if (op == "command.run") {
        std::string name = req->get("name").toString();
        if (name.empty()) {
            errorReply(rep, id, "invalid_params", "command name is required");
            return;
        }
        if (!command::has(name)) {
            errorReply(rep, id, "not_found", "unknown command: " + name);
            return;
        }

        Var args;
        if (Node* argsNode = req->find("args")) {
            args = schema::exportAs<schema::VarS>(argsNode);
        } else if (Node* bodyNode = req->find("body")) {
            args = schema::exportAs<schema::VarS>(bodyNode);
        }

        const bool waitCmd = req->find("wait") ? req->get("wait").toBool(true) : true;
        if (waitCmd) {
            Result result = command::call(name, args, true);
            if (result.isSuccess() || result.isAccepted()) {
                okReply(rep, id, result.content());
            } else {
                errorReply(rep, id, "command_failed", result.content().toString());
            }
            return;
        }

        Node* ctxNode = command::context(name);
        if (!ctxNode) {
            errorReply(rep, id, "internal_error", "cannot create command context");
            return;
        }
        command::parseArgs(ctxNode, args);
        Pipeline* detached = nullptr;
        Result result = command::call(name, ctxNode, false, &detached);
        if (detached) {
            if (!tasks) {
                delete detached;
                delete ctxNode;
                errorReply(rep, id, "internal_error", "task service unavailable");
                return;
            }

            std::string taskId = tasks->attach(name, id, ctxNode, detached,
                [allowAsyncEvents, sendEvent](const Node& event) {
                    if (allowAsyncEvents && sendEvent) {
                        sendEvent(event);
                    }
                });
            if (taskId.empty()) {
                errorReply(rep, id, "internal_error", "failed to start task");
            } else {
                acceptedReply(rep, id, taskId);
            }
            return;
        }

        delete ctxNode;
        if (result.isSuccess() || result.isAccepted()) {
            okReply(rep, id, result.content());
        } else {
            errorReply(rep, id, "command_failed", result.content().toString());
        }
        return;
    }

    if (op == "subscribe") {
        if (!allowSubscriptions || !subscribe) {
            errorReply(rep, id, "unsupported", "subscriptions are not supported on this transport");
            return;
        }

        std::string path = req->get("path").toString();
        bool bubble = req->get("bubble").toBool(false);
        bool tree = req->get("tree").toBool(false);
        subscribe->subscribe(sessionId, path, bubble, tree);

        Node data("data");
        data.set("path", path);
        data.set("subscribed", true);
        okReply(rep, id, &data);
        return;
    }

    if (op == "unsubscribe") {
        if (!allowSubscriptions || !subscribe) {
            errorReply(rep, id, "unsupported", "subscriptions are not supported on this transport");
            return;
        }

        std::string path = req->get("path").toString();
        subscribe->unsubscribe(sessionId, path);

        Node data("data");
        data.set("path", path);
        data.set("subscribed", false);
        okReply(rep, id, &data);
        return;
    }

    errorReply(rep, id, "unknown_op", "unknown op: " + op);
}

void dispatchNodeProtocol(Node* root, Node* req, Node* rep,
                          SubscribeService* subscribe,
                          NodeTaskService* tasks,
                          int batchLimit,
                          bool allowSubscriptions,
                          uint64_t sessionId,
                          bool allowAsyncEvents,
                          const NodeEventFn& sendEvent)
{
    dispatchNodeProtocolInternal(root, req, rep, subscribe, tasks, batchLimit,
                                 allowSubscriptions, sessionId, allowAsyncEvents, sendEvent);
}

} // namespace service
} // namespace ve
