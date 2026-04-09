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
            auto a = command::args(ctx);
            std::string key_name = a.string("key");
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
            auto a = command::args(ctx);
            const auto result = ros::listNodes(a.string("filter"));
            writeNodeTree(n("ve/ros"), "runtime/nodes", Var(result));
            return okResult(Var(result));
        });

        command::reg("ros.topic.list", [this](Node* ctx) -> Result {
            auto a = command::args(ctx);
            const auto result = ros::listTopics(a.string("filter"));
            writeNodeTree(n("ve/ros"), "runtime/topics", Var(result));
            return okResult(Var(result));
        });

        command::reg("ros.topic.info", [](Node* ctx) -> Result {
            auto a = command::args(ctx);
            const auto topic_name = a.string("name");
            if (topic_name.empty())
                return failResult("topic name is required");
            return okResult(Var(ros::topicInfo(topic_name)));
        });

        command::reg("ros.topic.subscribe", [this](Node* ctx) -> Result {
            auto a = command::args(ctx);
            ros::TopicSubscriptionConfig config;
            config.name = a.string("name");
            config.topic = a.string("topic");
            config.type = a.string("type");
            config.target_node = a.string("target_node");
            config.payload_format = a.string("payload_format", "cdr_hex");
            if (config.name.empty() || config.topic.empty())
                return failResult("name/topic is required");

            const auto result = ros::subscribeTopic(config);
            if (result.value("ok").toBool(false))
                writeNodeTree(n("ve/ros"), "runtime/subscriptions/" + config.name, Var(result));
            return okResult(Var(result));
        });

        command::reg("ros.topic.unsubscribe", [this](Node* ctx) -> Result {
            auto a = command::args(ctx);
            const auto name = a.string("name");
            if (name.empty())
                return failResult("name is required");
            const auto result = ros::unsubscribeTopic(name);
            if (result.value("ok").toBool(false))
                n("ve/ros/runtime")->erase("subscriptions/" + name);
            return okResult(Var(result));
        });

        command::reg("ros.topic.publish", [this](Node* ctx) -> Result {
            auto a = command::args(ctx);
            ros::TopicPublishRequest request;
            request.topic = a.string("topic");
            request.type = a.string("type");
            request.payload = a.string("payload");
            request.payload_format = a.string("payload_format", "cdr_hex");
            if (request.topic.empty() || request.payload.empty())
                return failResult("topic/payload is required");

            const auto result = ros::publishTopic(request);
            writeNodeTree(n("ve/ros"), "runtime/publications/last", Var(result));
            return okResult(Var(result));
        });

        command::reg("ros.service.list", [this](Node* ctx) -> Result {
            auto a = command::args(ctx);
            const auto result = ros::listServices(a.string("filter"));
            writeNodeTree(n("ve/ros"), "runtime/services", Var(result));
            return okResult(Var(result));
        });

        command::reg("ros.service.info", [](Node* ctx) -> Result {
            auto a = command::args(ctx);
            const auto service_name = a.string("name");
            if (service_name.empty())
                return failResult("service name is required");
            return okResult(Var(ros::serviceInfo(service_name)));
        });

        command::reg("ros.param.list", [this](Node* ctx) -> Result {
            auto a = command::args(ctx);
            const auto result = ros::listParams(a.string("node"));
            writeNodeTree(n("ve/ros"), "runtime/params", Var(result));
            return okResult(Var(result));
        });

        command::reg("ros.param.get", [](Node* ctx) -> Result {
            auto a = command::args(ctx);
            const auto node_name = a.string("node");
            const auto param_name = a.string("name");
            if (node_name.empty() || param_name.empty())
                return failResult("node/name is required");
            return okResult(Var(ros::getParam(node_name, param_name)));
        });

        command::reg("ros.param.set", [](Node* ctx) -> Result {
            auto a = command::args(ctx);
            auto node_name = a.string("node");
            auto param_name = a.string("name");
            Var value = a.var("value");
            if (node_name.empty() || param_name.empty() || value.isNull())
                return failResult("node/name/value is required");
            if (value.isString())
                value = ros::yaml::decode(value.toString());
            return okResult(Var(ros::setParam(node_name, param_name, value)));
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
