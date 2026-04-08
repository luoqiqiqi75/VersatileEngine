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

#include <charconv>

namespace ve {

namespace {

Result okResult(const Var& content)
{
    return Result(Result::SUCCESS, content);
}

Result failResult(const std::string& message)
{
    return Result(Result::FAIL, Var(message));
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

bool parseIntStrict(const std::string& text, int* out)
{
    if (!out || text.empty())
        return false;
    int value = 0;
    const auto* begin = text.data();
    const auto* end = text.data() + text.size();
    auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc() || ptr != end)
        return false;
    *out = value;
    return true;
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

void setDeclareExample(const std::string& key,
                       const std::string& help,
                       const Strings& positional,
                       const Var::DictV& named = {})
{
    auto* decl = n("ve/command/declare/" + key);
    decl->set("help", Var(help));

    auto* args_n = decl->at("args");
    args_n->clear();
    for (int i = 0; i < positional.sizeAsInt(); ++i) {
        args_n->set(i, positional[static_cast<std::size_t>(i)]);
    }

    if (named.size() > 0)
        schema::importAs<schema::VarS>(decl->at("named"), Var(named), schema::ImportOptions{true, true, true});
}

std::string argString(const Var& args, const std::string& key = "", const std::string& def = "")
{
    if (args.isString() && key.empty())
        return args.toString(def);
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

        setDeclareExample("ros.info", "Show ros backend, env and cached runtime summary.", {});
        command::reg("ros.info", Step("ros.info", [this](const Var&) -> Result {
            return okResult(Var(buildInfo()));
        }), "Show ros backend, env and cached runtime summary.");

        setDeclareExample("ros.backend.list", "List registered ros backends.", {});
        command::reg("ros.backend.list", Step("ros.backend.list", [](const Var&) -> Result {
            return okResult(Var(ros::backendInfoList()));
        }), "List registered ros backends.");

        setDeclareExample("ros.backend.info", "Show backend details. Positional arg0 or named key=backend.", {"backend_key"});
        command::reg("ros.backend.info", Step("ros.backend.info", [](const Var& args) -> Result {
            std::string key_name = argStringAt(args, "key", 0);
            if (key_name.empty()) {
                if (auto current = ros::defaultBackend())
                    key_name = current->key();
            }
            if (key_name.empty())
                return failResult("no ros backend is registered");
            if (auto current = ros::backend(key_name))
                return okResult(Var(current->info()));
            return failResult("ros backend not found: " + key_name);
        }), "Show backend details. Positional arg0 or named key=backend.");

        setDeclareExample("ros.parser.list", "List registered ros payload parsers.", {});
        command::reg("ros.parser.list", Step("ros.parser.list", [](const Var&) -> Result {
            return okResult(Var(ros::parserInfoList()));
        }), "List registered ros payload parsers.");

        setDeclareExample("ros.env", "Show ROS-related environment variables.", {});
        command::reg("ros.env", Step("ros.env", [](const Var&) -> Result {
            return okResult(Var(ros::envInfo()));
        }), "Show ROS-related environment variables.");

        setDeclareExample("ros.node.list", "List ROS nodes. Optional positional arg0 or named filter.", {"filter"});
        command::reg("ros.node.list", Step("ros.node.list", [this](const Var& args) -> Result {
            const auto result = ros::listNodes(argStringAt(args, "filter", 0));
            writeNamedNodeList("nodes", result);
            return okResult(Var(result));
        }), "List ROS nodes. Optional positional arg0 or named filter.");

        setDeclareExample("ros.topic.list", "List ROS topics. Optional positional arg0 or named filter.", {"filter"});
        command::reg("ros.topic.list", Step("ros.topic.list", [this](const Var& args) -> Result {
            const auto result = ros::listTopics(argStringAt(args, "filter", 0));
            writeNamedPathList("topics", result, "name");
            return okResult(Var(result));
        }), "List ROS topics. Optional positional arg0 or named filter.");

        setDeclareExample("ros.topic.info", "Show ROS topic details. Positional arg0 or named name=topic.", {"topic"});
        command::reg("ros.topic.info", Step("ros.topic.info", [](const Var& args) -> Result {
            const auto topic_name = argStringAt(args, "name", 0);
            if (topic_name.empty())
                return failResult("topic name is required");
            return okResult(Var(ros::topicInfo(topic_name)));
        }), "Show ROS topic details. Positional arg0 or named name=topic.");

        setDeclareExample("ros.topic.subscribe",
                          "Subscribe to a topic. Positional: name topic [type] [payload_format]. Named: target_node.",
                          {"name", "topic", "type", "payload_format"},
                          {{"target_node", Var("optional target node path")}});
        command::reg("ros.topic.subscribe", Step("ros.topic.subscribe", [this](const Var& args) -> Result {
            ros::TopicSubscriptionConfig config;
            config.name = argStringAt(args, "name", 0);
            config.topic = argStringAt(args, "topic", 1);
            config.type = argStringAt(args, "type", 2);
            config.target_node = argString(args, "target_node");
            config.payload_format = argStringAt(args, "payload_format", 3, "yaml");
            if (config.name.empty() || config.topic.empty())
                return failResult("name/topic is required");

            const auto result = ros::subscribeTopic(config);
            if (result.value("ok").toBool(false))
                writeRosMirror("subscriptions/" + config.name, Var(result));
            return okResult(Var(result));
        }), "Subscribe to a topic. Positional: name topic [type] [payload_format]. Named: target_node.");

        setDeclareExample("ros.topic.unsubscribe", "Remove a named topic subscription.", {"name"});
        command::reg("ros.topic.unsubscribe", Step("ros.topic.unsubscribe", [this](const Var& args) -> Result {
            const auto name = argStringAt(args, "name", 0);
            if (name.empty())
                return failResult("name is required");
            const auto result = ros::unsubscribeTopic(name);
            if (result.value("ok").toBool(false)) {
                n("ve/ros")->erase("subscriptions/" + name);
            }
            return okResult(Var(result));
        }), "Remove a named topic subscription.");

        setDeclareExample("ros.topic.publish",
                          "Publish to a topic. Positional: topic type payload [payload_format].",
                          {"topic", "type", "payload", "payload_format"});
        command::reg("ros.topic.publish", Step("ros.topic.publish", [this](const Var& args) -> Result {
            ros::TopicPublishRequest request;
            request.topic = argStringAt(args, "topic", 0);
            request.type = argStringAt(args, "type", 1);
            request.payload = argStringAt(args, "payload", 2);
            request.payload_format = argStringAt(args, "payload_format", 3, "yaml");
            if (request.topic.empty() || request.payload.empty())
                return failResult("topic/payload is required");

            const auto result = ros::publishTopic(request);
            writeRosMirror("publications/last", Var(result));
            return okResult(Var(result));
        }), "Publish to a topic. Positional: topic type payload [payload_format].");

        setDeclareExample("ros.topic.once",
                          "Wait for one message. Positional: topic [type] [payload_format|timeout_ms] [timeout_ms]. Named: target_node timeout_ms.",
                          {"topic", "type", "payload_format_or_timeout", "timeout_ms"},
                          {{"target_node", Var("optional target node path")},
                           {"timeout_ms", Var("optional timeout milliseconds")}});
        command::reg("ros.topic.once", Step("ros.topic.once", [this](const Var& args) -> Result {
            ros::TopicOnceRequest request;
            request.topic = argStringAt(args, "topic", 0);
            const auto second = argStringAt(args, "type", 1);
            request.target_node = argString(args, "target_node");
            const auto third = argStringAt(args, "payload_format", 2);
            const auto fourth = argStringAt(args, "timeout_ms", 3);
            request.type = (!looksLikePayloadFormat(second) && !looksLikeInt(second)) ? second : "";
            request.payload_format = looksLikePayloadFormat(third) ? third : "yaml";
            std::string timeout_text = argString(args, "timeout_ms");
            if (timeout_text.empty() && looksLikeInt(second))
                timeout_text = second;
            if (timeout_text.empty() && looksLikeInt(third))
                timeout_text = third;
            if (timeout_text.empty() && looksLikeInt(fourth))
                timeout_text = fourth;
            if (request.topic.empty())
                return failResult("topic is required");
            if (!timeout_text.empty()) {
                int parsed_timeout = request.timeout_ms;
                if (!parseIntStrict(timeout_text, &parsed_timeout))
                    return failResult("timeout_ms must be an integer");
                request.timeout_ms = parsed_timeout;
            }
            if (request.type.empty())
                request.type = inferTopicType(request.topic);
            if (request.type.empty())
                return failResult("cannot infer topic type; specify [type] explicitly");

            const auto result = ros::onceTopic(request);
            writeRosMirror("once/last", Var(result));
            return okResult(Var(result));
        }), "Wait for one message. Positional: topic [type] [payload_format|timeout_ms] [timeout_ms]. Named: target_node timeout_ms.");

        setDeclareExample("ros.service.list", "List ROS services. Optional positional arg0 or named filter.", {"filter"});
        command::reg("ros.service.list", Step("ros.service.list", [this](const Var& args) -> Result {
            const auto result = ros::listServices(argStringAt(args, "filter", 0));
            writeNamedPathList("services", result, "name");
            return okResult(Var(result));
        }), "List ROS services. Optional positional arg0 or named filter.");

        setDeclareExample("ros.service.info", "Show ROS service details. Positional arg0 or named name=service.", {"service"});
        command::reg("ros.service.info", Step("ros.service.info", [](const Var& args) -> Result {
            const auto service_name = argStringAt(args, "name", 0);
            if (service_name.empty())
                return failResult("service name is required");
            return okResult(Var(ros::serviceInfo(service_name)));
        }), "Show ROS service details. Positional arg0 or named name=service.");

        setDeclareExample("ros.param.list", "List ROS parameters. Optional positional arg0 or named node.", {"node"});
        command::reg("ros.param.list", Step("ros.param.list", [this](const Var& args) -> Result {
            const auto result = ros::listParams(argStringAt(args, "node", 0));
            writeRosMirror("params", Var(result));
            return okResult(Var(result));
        }), "List ROS parameters. Optional positional arg0 or named node.");

        setDeclareExample("ros.param.get", "Get one ROS parameter. Positional: node name.", {"node", "name"});
        command::reg("ros.param.get", Step("ros.param.get", [](const Var& args) -> Result {
            const auto node_name = argStringAt(args, "node", 0);
            const auto param_name = argStringAt(args, "name", 1);
            if (node_name.empty() || param_name.empty())
                return failResult("node/name is required");
            return okResult(Var(ros::getParam(node_name, param_name)));
        }), "Get one ROS parameter. Positional: node name.");

        setDeclareExample("ros.param.set", "Set one ROS parameter. Dict {node,name,value} or positional [node name yaml_value].", {"node", "name", "yaml_value"});
        command::reg("ros.param.set", Step("ros.param.set", [](const Var& args) -> Result {
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
        }), "Set one ROS parameter. Dict {node,name,value} or positional [node name yaml_value].");

        setDeclareExample("ros.runtime.refresh", "Refresh cached ROS lists under ve/ros and ve/ros/runtime.", {});
        command::reg("ros.runtime.refresh", Step("ros.runtime.refresh", [this](const Var&) -> Result {
            std::string error;
            if (!ros::refreshRuntime(n("ve/ros"), error))
                return failResult(error);
            return okResult(Var(ros::runtimeInfo()));
        }), "Refresh cached ROS lists under ve/ros and ve/ros/runtime.");
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
