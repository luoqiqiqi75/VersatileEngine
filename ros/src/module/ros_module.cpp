// ros_module.cpp  - ve::RosModule (ve.ros)
//
// Official runtime entry point for ve/ros:
//   - selects and starts a backend
//   - exposes discovery + parameter commands
//   - keeps runtime state under ve/ros/*

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

void writeRosMirror(const std::string& path, const Var& value)
{
    auto* root = n("ve/ros");
    writeNodeTree(root, path, value);
}

void writeNamedNodeList(const std::string& path, const Var::ListV& list)
{
    auto* root = n("ve/ros");
    auto* target = root->at(path);
    target->clear();
    target->set(Var());

    for (const auto& item : list) {
        if (!item.isDict()) {
            target->append("")->set(item);
            continue;
        }
        const auto& dict = item.toDict();
        auto it = dict.find("name");
        const std::string key = (it != dict.end() && !it->second.toString().empty())
            ? it->second.toString()
            : "node";
        Node temp(key);
        schema::importAs<schema::VarS>(&temp, item, schema::ImportOptions{true, true, true});
        target->append(key)->copy(&temp, true, true, true);
    }
}

std::string normalizeNamedPath(std::string key, const std::string& def = "item")
{
    if (key.empty())
        return def;
    while (!key.empty() && key.front() == '/')
        key.erase(key.begin());
    while (!key.empty() && key.back() == '/')
        key.pop_back();
    return key.empty() ? def : key;
}

void writeNamedPathList(const std::string& path, const Var::ListV& list, const std::string& key_field)
{
    auto* root = n("ve/ros");
    auto* target = root->at(path);
    target->clear();
    target->set(Var());

    for (const auto& item : list) {
        if (!item.isDict()) {
            target->append("")->set(item);
            continue;
        }
        const auto& dict = item.toDict();
        auto it = dict.find(key_field);
        const std::string key = normalizeNamedPath(
            (it != dict.end() && !it->second.toString().empty()) ? it->second.toString() : "",
            "item");
        Node temp("temp");
        schema::importAs<schema::VarS>(&temp, item, schema::ImportOptions{true, true, true});
        target->at(key)->copy(&temp, true, true, true);
    }
}

bool looksLikeInt(const std::string& text)
{
    if (text.empty())
        return false;
    std::size_t start = (text[0] == '-' || text[0] == '+') ? 1 : 0;
    if (start >= text.size())
        return false;
    for (std::size_t i = start; i < text.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(text[i])))
            return false;
    }
    return true;
}

bool looksLikePayloadFormat(const std::string& text)
{
    return text == "yaml" || text == "var" || text == "cdr_hex";
}

std::string inferTopicType(const std::string& topic)
{
    const auto info = ros::topicInfo(topic);
    if (!info.value("ok").toBool(false))
        return "";
    const auto types = info.value("types");
    if (!types.isList() || types.toList().empty())
        return "";
    return types.toList().front().toString();
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
        if (!ros::activateBackend(requested_backend, n("ve/ros"), error)) {
            active_backend_.clear();
            syncRuntimeState("error");
            n("ve/ros/last_error")->set(Var(error));
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

        command::declareNode("ros.backend.info")->at("key");
        command::declareNode("ros.node.list")->at("filter");
        command::declareNode("ros.topic.list")->at("filter");
        command::declareNode("ros.topic.info")->at("name");

        auto* subscribe_decl = command::declareNode("ros.topic.subscribe");
        subscribe_decl->at("name");
        subscribe_decl->at("topic");
        subscribe_decl->at("type");
        subscribe_decl->at("target_node");
        subscribe_decl->at("payload_format");

        command::declareNode("ros.topic.unsubscribe")->at("name");

        auto* publish_decl = command::declareNode("ros.topic.publish");
        publish_decl->at("topic");
        publish_decl->at("type");
        publish_decl->at("payload");
        publish_decl->at("payload_format");

        auto* once_decl = command::declareNode("ros.topic.once");
        once_decl->at("topic");
        once_decl->at("target_node");
        once_decl->at("type");
        once_decl->at("payload_format");
        once_decl->at("timeout_ms");

        command::declareNode("ros.service.list")->at("filter");
        command::declareNode("ros.service.info")->at("name");
        command::declareNode("ros.param.list")->at("node");

        auto* param_get_decl = command::declareNode("ros.param.get");
        param_get_decl->at("node");
        param_get_decl->at("name");

        auto* param_set_decl = command::declareNode("ros.param.set");
        param_set_decl->at("node");
        param_set_decl->at("name");
        param_set_decl->at("value");

        command::reg("ros.info", [this]() -> Result {
            return okResult(Var(buildInfo()));
        }, "Show ros backend, env and cached runtime summary.");

        command::reg("ros.backend.list", []() -> Result {
            return okResult(Var(ros::backendInfoList()));
        }, "List registered ros backends.");

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
        }, "Show backend details.");

        command::reg("ros.parser.list", []() -> Result {
            return okResult(Var(ros::parserInfoList()));
        }, "List registered ros payload parsers.");

        command::reg("ros.env", []() -> Result {
            return okResult(Var(ros::envInfo()));
        }, "Show ROS-related environment variables.");

        command::reg("ros.node.list", [this](Node* ctx) -> Result {
            auto a = command::args(ctx);
            const auto result = ros::listNodes(a.string("filter"));
            writeNamedNodeList("nodes", result);
            return okResult(Var(result));
        }, "List ROS nodes.");

        command::reg("ros.topic.list", [this](Node* ctx) -> Result {
            auto a = command::args(ctx);
            const auto result = ros::listTopics(a.string("filter"));
            writeNamedPathList("topics", result, "name");
            return okResult(Var(result));
        }, "List ROS topics.");

        command::reg("ros.topic.info", [](Node* ctx) -> Result {
            auto a = command::args(ctx);
            const auto topic_name = a.string("name");
            if (topic_name.empty())
                return failResult("topic name is required");
            return okResult(Var(ros::topicInfo(topic_name)));
        }, "Show ROS topic details.");

        command::reg("ros.topic.subscribe", [this](Node* ctx) -> Result {
            auto a = command::args(ctx);
            ros::TopicSubscriptionConfig config;
            config.name = a.string("name");
            config.topic = a.string("topic");
            config.type = a.string("type");
            config.target_node = a.string("target_node");
            config.payload_format = a.string("payload_format", "yaml");
            if (config.name.empty() || config.topic.empty())
                return failResult("name/topic is required");

            const auto result = ros::subscribeTopic(config);
            if (result.value("ok").toBool(false))
                writeRosMirror("subscriptions/" + config.name, Var(result));
            return okResult(Var(result));
        }, "Subscribe to a topic.");

        command::reg("ros.topic.unsubscribe", [this](Node* ctx) -> Result {
            auto a = command::args(ctx);
            const auto name = a.string("name");
            if (name.empty())
                return failResult("name is required");
            const auto result = ros::unsubscribeTopic(name);
            if (result.value("ok").toBool(false)) {
                n("ve/ros")->erase("subscriptions/" + name);
            }
            return okResult(Var(result));
        }, "Remove a named topic subscription.");

        command::reg("ros.topic.publish", [this](Node* ctx) -> Result {
            auto a = command::args(ctx);
            ros::TopicPublishRequest request;
            request.topic = a.string("topic");
            request.type = a.string("type");
            request.payload = a.string("payload");
            request.payload_format = a.string("payload_format", "yaml");
            if (request.topic.empty() || request.payload.empty())
                return failResult("topic/payload is required");

            const auto result = ros::publishTopic(request);
            writeRosMirror("publications/last", Var(result));
            return okResult(Var(result));
        }, "Publish to a topic.");

        command::reg("ros.topic.once", [this](Node* ctx) -> Result {
            auto a = command::args(ctx);
            ros::TopicOnceRequest request;
            request.topic = a.string("topic");
            request.target_node = a.string("target_node");

            // type can be inferred from topic if not given
            std::string type_str = a.string("type");
            std::string fmt_str = a.string("payload_format", "yaml");
            std::string timeout_str = a.string("timeout_ms");

            // Handle ambiguous positional: type vs payload_format vs timeout_ms
            if (!type_str.empty() && (looksLikePayloadFormat(type_str) || looksLikeInt(type_str))) {
                // "type" slot actually holds a format or timeout
                if (looksLikePayloadFormat(type_str)) fmt_str = type_str;
                else if (looksLikeInt(type_str)) timeout_str = type_str;
                type_str.clear();
            }

            request.type = type_str;
            request.payload_format = fmt_str;

            if (request.topic.empty())
                return failResult("topic is required");

            if (!timeout_str.empty()) {
                try { request.timeout_ms = std::stoi(timeout_str); }
                catch (...) { return failResult("timeout_ms must be an integer"); }
            }

            if (request.type.empty())
                request.type = inferTopicType(request.topic);
            if (request.type.empty())
                return failResult("cannot infer topic type; specify [type] explicitly");

            const auto result = ros::onceTopic(request);
            writeRosMirror("once/last", Var(result));
            return okResult(Var(result));
        }, "Wait for one message.");

        command::reg("ros.service.list", [this](Node* ctx) -> Result {
            auto a = command::args(ctx);
            const auto result = ros::listServices(a.string("filter"));
            writeNamedPathList("services", result, "name");
            return okResult(Var(result));
        }, "List ROS services.");

        command::reg("ros.service.info", [](Node* ctx) -> Result {
            auto a = command::args(ctx);
            const auto service_name = a.string("name");
            if (service_name.empty())
                return failResult("service name is required");
            return okResult(Var(ros::serviceInfo(service_name)));
        }, "Show ROS service details.");

        command::reg("ros.param.list", [this](Node* ctx) -> Result {
            auto a = command::args(ctx);
            const auto result = ros::listParams(a.string("node"));
            writeRosMirror("params", Var(result));
            return okResult(Var(result));
        }, "List ROS parameters.");

        command::reg("ros.param.get", [](Node* ctx) -> Result {
            auto a = command::args(ctx);
            const auto node_name = a.string("node");
            const auto param_name = a.string("name");
            if (node_name.empty() || param_name.empty())
                return failResult("node/name is required");
            return okResult(Var(ros::getParam(node_name, param_name)));
        }, "Get one ROS parameter.");

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
        }, "Set one ROS parameter.");

        command::reg("ros.runtime.refresh", [this]() -> Result {
            std::string error;
            if (!ros::refreshRuntime(n("ve/ros"), error))
                return failResult(error);
            return okResult(Var(ros::runtimeInfo()));
        }, "Refresh cached ROS lists under ve/ros.");
    }

    Var::DictV buildInfo() const
    {
        Var::DictV dict;
        dict["state"] = Var(n("ve/ros/state")->getString());
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
        dict["nodes"] = n("ve/ros/nodes")->get();
        dict["topics"] = n("ve/ros/topics")->get();
        dict["services"] = n("ve/ros/services")->get();
        dict["params"] = n("ve/ros/params")->get();
        dict["note"] = Var(node()->get("config/note").toString());
        return dict;
    }

    void syncRuntimeState(const std::string& state)
    {
        auto* root = n("ve/ros");
        root->set("state", Var(state));
        root->set("domain_id", Var(static_cast<int64_t>(node()->get("config/domain_id").toInt(0))));
        root->set("service_prefix", Var(node()->get("config/service_prefix").toString("ve")));
        root->set("backend_requested", Var(node()->get("config/backend").toString()));
        root->set("backend_active", Var(active_backend_));
        writeNodeTree(root, "backends", Var(ros::backendInfoList()));
        writeNodeTree(root, "parsers", Var(ros::parserInfoList()));
        writeNodeTree(root, "env", Var(ros::envInfo()));
        root->at("nodes");
        root->at("topics");
        root->at("services");
        root->at("params");
        root->at("subscriptions");
        root->at("publications");
        root->at("once");
        root->set("note", Var(
            "ve/ros exposes discovery and parameter APIs through backend-neutral entry points."));
    }
};

} // namespace ve

VE_REGISTER_PRIORITY_MODULE(ve.ros, ve::RosModule, 40, 1)
