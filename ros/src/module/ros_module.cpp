// ros_module.cpp  - ve::RosModule (ve.ros)
//
// Official runtime entry point for ve/ros:
//   - selects and starts a backend
//   - exposes discovery + parameter commands
//   - keeps runtime state under ve/ros/runtime/*

#include "ve/core/command.h"
#include "ve/core/log.h"
#include "ve/core/module.h"
#include "ve/core/schema.h"
#include "ve/ros/parser.h"
#include "ve/ros/runtime.h"
#include "ve/ros/service.h"
#include "ve/ros/topic.h"
#include "ve/ros/yaml_schema.h"

namespace ve {

namespace {

Result okResult(const Var& content)
{
    return Result::ok(content);
}

Result failResult(const std::string& message)
{
    return Result::fail(Var(message));
}

void writeNodeTree(Node* root, const std::string& path, const Var& value)
{
    if (!root)
        return;
    auto* target = root->at(path);
    target->clear();
    target->set(Var());
    schema::importAs<schema::VarS>(target, value);
}

std::string argString(const Var& args, const std::string& key = "", const std::string& def = "")
{
    if (args.isString())
        return args.toString(def);
    if (args.isList()) {
        const auto& list = args.toList();
        return list.empty() ? def : list.front().toString(def);
    }
    if (args.isDict() && !key.empty()) {
        const auto& dict = args.toDict();
        auto it = dict.find(key);
        if (it != dict.end())
            return it->second.toString(def);
    }
    return def;
}

std::string argStringAt(const Var& args,
                        const std::string& key,
                        int positional_index,
                        const std::string& def = "")
{
    if (args.isDict() && !key.empty()) {
        const auto& dict = args.toDict();
        auto it = dict.find(key);
        if (it != dict.end())
            return it->second.toString(def);
    }
    if (args.isList()) {
        const auto& list = args.toList();
        if (positional_index >= 0 && positional_index < static_cast<int>(list.size()))
            return list[static_cast<std::size_t>(positional_index)].toString(def);
    }
    if (positional_index == 0 && args.isString())
        return args.toString(def);
    return def;
}

inline Var ctxGet(Node* ctx) { return ctx ? ctx->get() : Var(); }

} // namespace

class RosModule : public Module
{
    std::string active_backend_;
    bool commands_registered_ = false;

public:
    explicit RosModule(const std::string& name) : Module(name)
    {
        node()->at("config/domain_id")->set(Var(0));
        node()->at("config/service_prefix")->set(Var("ve"));
        node()->at("config/backend")->set(Var(""));
        node()->at("config/note")->set(Var(
            "ve.ros exposes official ROS integration surfaces. "
            "Project-specific adapters should live outside ve/ros core."));

        syncRuntimeState("created");
    }

protected:
    void init() override
    {
        registerCommands();
        syncRuntimeState("init");
    }

    void ready() override
    {
        const std::string requested_backend = node()->get("config/backend").toString();

        std::string error;
        if (!ros::activateBackend(requested_backend, n("ve/ros/runtime"), error)) {
            active_backend_.clear();
            syncRuntimeState("error");
            n("ve/ros/runtime/last_error")->set(Var(error));
            veLogW << "[ve.ros] failed to activate backend: " << error;
            return;
        }
        active_backend_ = ros::activeBackendKey();

        syncRuntimeState("ready");
        veLogI << "[ve.ros] ready";
    }

    void deinit() override
    {
        ros::deactivateBackend();

        syncRuntimeState("stopped");
    }

private:
    void registerCommands()
    {
        if (commands_registered_)
            return;
        commands_registered_ = true;

        command::reg("ros.info", [this]() -> Result {
            return okResult(Var(buildInfo()));
        });

        command::reg("ros.backend.list", []() -> Result {
            return okResult(Var(ros::backendInfoList()));
        });

        command::reg("ros.backend.info", [](Node* ctx) -> Result {
            const Var args = ctxGet(ctx);
            std::string key_name = argString(args, "key");
            if (key_name.empty()) {
                if (auto current = ros::defaultBackend())
                    key_name = current->key();
            }
            if (key_name.empty())
                return failResult("no ros backend is registered");
            if (auto current = ros::backend(key_name))
                return okResult(Var(current->info()));
            return failResult("ros backend not found: " + key_name);
        });

        command::reg("ros.parser.list", []() -> Result {
            return okResult(Var(ros::parserInfoList()));
        });

        command::reg("ros.env", []() -> Result {
            return okResult(Var(ros::envInfo()));
        });

        command::reg("ros.node.list", [this](Node* ctx) -> Result {
            const Var args = ctxGet(ctx);
            const auto result = ros::listNodes(argString(args, "filter"));
            writeNodeTree(n("ve/ros"), "runtime/nodes", Var(result));
            return okResult(Var(result));
        });

        command::reg("ros.topic.list", [this](Node* ctx) -> Result {
            const Var args = ctxGet(ctx);
            const auto result = ros::listTopics(argString(args, "filter"));
            writeNodeTree(n("ve/ros"), "runtime/topics", Var(result));
            return okResult(Var(result));
        });

        command::reg("ros.topic.info", [](Node* ctx) -> Result {
            const Var args = ctxGet(ctx);
            const auto topic_name = argStringAt(args, "name", 0);
            if (topic_name.empty())
                return failResult("topic name is required");
            return okResult(Var(ros::topicInfo(topic_name)));
        });

        command::reg("ros.topic.subscribe", [this](Node* ctx) -> Result {
            const Var args = ctxGet(ctx);
            ros::TopicSubscriptionConfig config;
            config.name = argStringAt(args, "name", 0);
            config.topic = argStringAt(args, "topic", 1);
            config.type = argStringAt(args, "type", 2);
            config.target_node = argString(args, "target_node");
            config.payload_format = argStringAt(args, "payload_format", 3, "cdr_hex");
            if (config.name.empty() || config.topic.empty())
                return failResult("name/topic is required");

            const auto result = ros::subscribeTopic(config);
            if (result.value("ok").toBool(false))
                writeNodeTree(n("ve/ros"), "runtime/subscriptions/" + config.name, Var(result));
            return okResult(Var(result));
        });

        command::reg("ros.topic.unsubscribe", [this](Node* ctx) -> Result {
            const Var args = ctxGet(ctx);
            const auto name = argStringAt(args, "name", 0);
            if (name.empty())
                return failResult("name is required");
            const auto result = ros::unsubscribeTopic(name);
            if (result.value("ok").toBool(false))
                n("ve/ros/runtime")->erase("subscriptions/" + name);
            return okResult(Var(result));
        });

        command::reg("ros.topic.publish", [this](Node* ctx) -> Result {
            const Var args = ctxGet(ctx);
            ros::TopicPublishRequest request;
            request.topic = argStringAt(args, "topic", 0);
            request.type = argStringAt(args, "type", 1);
            request.payload = argStringAt(args, "payload", 2);
            request.payload_format = argStringAt(args, "payload_format", 3, "cdr_hex");
            if (request.topic.empty() || request.payload.empty())
                return failResult("topic/payload is required");

            const auto result = ros::publishTopic(request);
            writeNodeTree(n("ve/ros"), "runtime/publications/last", Var(result));
            return okResult(Var(result));
        });

        command::reg("ros.service.list", [this](Node* ctx) -> Result {
            const Var args = ctxGet(ctx);
            const auto result = ros::listServices(argString(args, "filter"));
            writeNodeTree(n("ve/ros"), "runtime/services", Var(result));
            return okResult(Var(result));
        });

        command::reg("ros.service.info", [](Node* ctx) -> Result {
            const Var args = ctxGet(ctx);
            const auto service_name = argStringAt(args, "name", 0);
            if (service_name.empty())
                return failResult("service name is required");
            return okResult(Var(ros::serviceInfo(service_name)));
        });

        command::reg("ros.param.list", [this](Node* ctx) -> Result {
            const Var args = ctxGet(ctx);
            const auto result = ros::listParams(argString(args, "node"));
            writeNodeTree(n("ve/ros"), "runtime/params", Var(result));
            return okResult(Var(result));
        });

        command::reg("ros.param.get", [](Node* ctx) -> Result {
            const Var args = ctxGet(ctx);
            const auto node_name = argStringAt(args, "node", 0);
            const auto param_name = argStringAt(args, "name", 1);
            if (node_name.empty() || param_name.empty())
                return failResult("node/name is required");
            return okResult(Var(ros::getParam(node_name, param_name)));
        });

        command::reg("ros.param.set", [](Node* ctx) -> Result {
            const Var args = ctxGet(ctx);
            if (args.isDict()) {
                const auto& dict = args.toDict();
                const auto node_it = dict.find("node");
                const auto name_it = dict.find("name");
                const auto value_it = dict.find("value");
                if (node_it == dict.end() || name_it == dict.end() || value_it == dict.end())
                    return failResult("node/name/value is required");
                return okResult(Var(ros::setParam(node_it->second.toString(),
                                                  name_it->second.toString(),
                                                  value_it->second)));
            }
            if (args.isList() && args.toList().size() >= 3) {
                const auto& list = args.toList();
                return okResult(Var(ros::setParam(list[0].toString(),
                                                  list[1].toString(),
                                                  ros::yaml::decode(list[2].toString()))));
            }
            return failResult("args must be {node,name,value} or [node, name, yaml_value]");
        });

        command::reg("ros.runtime.refresh", [this]() -> Result {
            std::string error;
            if (!ros::refreshRuntime(n("ve/ros/runtime"), error))
                return failResult(error);
            return okResult(Var(ros::runtimeInfo()));
        });
    }

    Var::DictV buildInfo() const
    {
        Var::DictV dict;
        dict["state"] = Var(n("ve/ros/runtime/state")->getString());
        dict["domain_id"] = Var(static_cast<int64_t>(node()->get("config/domain_id").toInt(0)));
        dict["service_prefix"] = Var(node()->get("config/service_prefix").toString("ve"));
        dict["backend_requested"] = Var(node()->get("config/backend").toString());
        dict["backend_active"] = Var(active_backend_);
        if (auto current = ros::backend(active_backend_))
            dict["backend_active_info"] = Var(current->info());
        else
            dict["backend_active_info"] = Var();
        dict["backends"] = Var(ros::backendInfoList());
        dict["parsers"] = Var(ros::parserInfoList());
        dict["env"] = Var(ros::envInfo());
        dict["nodes"] = n("ve/ros/runtime/nodes")->get();
        dict["topics"] = n("ve/ros/runtime/topics")->get();
        dict["services"] = n("ve/ros/runtime/services")->get();
        dict["params"] = n("ve/ros/runtime/params")->get();
        dict["note"] = Var(node()->get("config/note").toString());
        return dict;
    }

    void syncRuntimeState(const std::string& state)
    {
        auto* root = n("ve/ros");
        root->set("state", Var(state));
        root->set("runtime/state", Var(state));
        root->set("runtime/domain_id", Var(static_cast<int64_t>(node()->get("config/domain_id").toInt(0))));
        root->set("runtime/service_prefix", Var(node()->get("config/service_prefix").toString("ve")));
        root->set("runtime/backend_requested", Var(node()->get("config/backend").toString()));
        root->set("runtime/backend_active", Var(active_backend_));
        writeNodeTree(root, "runtime/backends", Var(ros::backendInfoList()));
        writeNodeTree(root, "runtime/parsers", Var(ros::parserInfoList()));
        writeNodeTree(root, "runtime/env", Var(ros::envInfo()));
        root->at("runtime/nodes");
        root->at("runtime/topics");
        root->at("runtime/services");
        root->at("runtime/params");
        root->at("runtime/subscriptions");
        root->at("runtime/publications");
        root->set("runtime/note", Var(
            "ve/ros v1 exposes runtime discovery and parameter APIs through backend-neutral entry points."));
    }
};

} // namespace ve

VE_REGISTER_PRIORITY_MODULE(ve.ros, ve::RosModule, 40, 1)
