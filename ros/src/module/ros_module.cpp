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
        schema::importAs<schema::VarS>(&temp, item, Node::COPY_STRICT | Node::COPY_UPDATE);
        target->append(key)->copy(&temp, Node::COPY_STRICT | Node::COPY_UPDATE);
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
        schema::importAs<schema::VarS>(&temp, item, Node::COPY_STRICT | Node::COPY_UPDATE);
        target->at(key)->copy(&temp, Node::COPY_STRICT | Node::COPY_UPDATE);
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

ros::QosProfile parseQos(Node* in)
{
    ros::QosProfile qos;
    const auto reliability = in->get("qos_reliability").toString();
    if (!reliability.empty()) qos.reliability = reliability;
    const auto durability = in->get("qos_durability").toString();
    if (!durability.empty()) qos.durability = durability;
    const auto history = in->get("qos_history").toString();
    if (!history.empty()) qos.history = history;
    const auto depth = in->get("qos_depth").toInt(0);
    if (depth > 0) qos.depth = static_cast<int>(depth);
    return qos;
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
            veLogW << "[ve.ros] backend failed: " << error;
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

        command::reg("ros.info", [this]() -> Var {
            return Var(buildInfo());
        }, "Show ros summary.");

        command::reg("ros.backend.list", []() -> Var {
            return Var(ros::backendInfoList());
        }, "List ros backends.");

        command::reg("ros.backend.info", [](Node* in, Node* out) -> Result {
            std::string key_name = in->get("key").toString();
            if (key_name.empty()) {
                if (auto current = ros::defaultBackend())
                    key_name = current->key();
            }
            if (key_name.empty())
                return Result::fail("no ros backend is registered");
            auto current = ros::backend(key_name);
            if (!current)
                return Result::fail("not found: " + key_name);
            schema::VarS::importNode(out, Var(current->info()));
            return Result::ok();
        }, "Show backend details.");

        command::reg("ros.parser.list", []() -> Var {
            return Var(ros::parserInfoList());
        }, "List ros parsers.");

        command::reg("ros.env", []() -> Var {
            return Var(ros::envInfo());
        }, "Show ROS env vars.");

        command::reg("ros.node.list", [](Node* in, Node* out) -> Result {
            const auto result = ros::listNodes(in->get("filter").toString());
            writeNamedNodeList("nodes", result);
            Var::ListV names;
            for (const auto& item : result) {
                if (item.isDict())
                    names.push_back(item.toDict().value("full_name"));
                else
                    names.push_back(item);
            }
            schema::VarS::importNode(out, Var(std::move(names)));
            return Result::ok();
        }, "List ROS nodes.");

        command::reg("ros.topic.list", [](Node* in, Node* out) -> Result {
            const auto result = ros::listTopics(in->get("filter").toString());
            writeNamedPathList("topics", result, "name");
            Var::ListV names;
            for (const auto& item : result) {
                if (item.isDict())
                    names.push_back(item.toDict().value("name"));
                else
                    names.push_back(item);
            }
            schema::VarS::importNode(out, Var(std::move(names)));
            return Result::ok();
        }, "List ROS topics.");

        command::reg("ros.topic.info", [](Node* in, Node* out) -> Result {
            const auto topic_name = in->get("name").toString();
            if (topic_name.empty())
                return Result::fail("topic name required");
            const auto result = ros::topicInfo(topic_name);
            if (!result.value("ok").toBool(false))
                return Result::fail(result.value("message").toString("topic info failed"));
            schema::VarS::importNode(out, Var(result));
            return Result::ok();
        }, "Show topic details.");

        command::reg("ros.topic.subscribe", [](Node* in, Node* out) -> Result {
            ros::TopicSubscriptionConfig config;
            config.name = in->get("name").toString();
            config.topic = in->get("topic").toString();
            config.type = in->get("type").toString();
            config.target_node = in->get("target_node").toString();
            config.payload_format = in->get("payload_format").toString("yaml");
            config.qos = parseQos(in);
            if (config.name.empty() || config.topic.empty())
                return Result::fail("name/topic required");

            const auto result = ros::subscribeTopic(config);
            if (!result.value("ok").toBool(false))
                return Result::fail(result.value("message").toString("subscribe failed"));
            writeRosMirror("subscriptions/" + config.name, Var(result));
            schema::VarS::importNode(out, Var(result));
            return Result::ok();
        }, "Subscribe to a topic.");

        command::reg("ros.topic.unsubscribe", [](Node* in, Node* out) -> Result {
            const auto name = in->get("name").toString();
            if (name.empty())
                return Result::fail("name required");
            const auto result = ros::unsubscribeTopic(name);
            if (!result.value("ok").toBool(false))
                return Result::fail(result.value("message").toString("unsubscribe failed"));
            n("ve/ros")->erase("subscriptions/" + name);
            schema::VarS::importNode(out, Var(result));
            return Result::ok();
        }, "Remove a topic subscription.");

        command::reg("ros.topic.publish", [](Node* in, Node* out) -> Result {
            ros::TopicPublishRequest request;
            request.topic = in->get("topic").toString();
            request.type = in->get("type").toString();
            request.payload = in->get("payload").toString();
            request.payload_format = in->get("payload_format").toString("yaml");
            request.qos = parseQos(in);
            if (request.topic.empty() || request.payload.empty())
                return Result::fail("topic/payload required");

            const auto result = ros::publishTopic(request);
            if (!result.value("ok").toBool(false))
                return Result::fail(result.value("message").toString("publish failed"));
            writeRosMirror("publications/last", Var(result));
            schema::VarS::importNode(out, Var(result));
            return Result::ok();
        }, "Publish to a topic.");

        command::reg("ros.topic.once", [](Node* in, Node* out) -> Result {
            ros::TopicOnceRequest request;
            request.topic = in->get("topic").toString();
            request.target_node = in->get("target_node").toString();

            std::string type_str = in->get("type").toString();
            std::string fmt_str = in->get("payload_format").toString("yaml");
            std::string timeout_str = in->get("timeout_ms").toString();

            if (!type_str.empty() && (looksLikePayloadFormat(type_str) || looksLikeInt(type_str))) {
                if (looksLikePayloadFormat(type_str)) fmt_str = type_str;
                else if (looksLikeInt(type_str)) timeout_str = type_str;
                type_str.clear();
            }

            request.type = type_str;
            request.payload_format = fmt_str;
            request.qos = parseQos(in);

            if (request.topic.empty())
                return Result::fail("topic required");

            if (!timeout_str.empty()) {
                try { request.timeout_ms = std::stoi(timeout_str); }
                catch (...) { return Result::fail("timeout_ms must be integer"); }
            }

            if (request.type.empty())
                request.type = inferTopicType(request.topic);
            if (request.type.empty())
                return Result::fail("cannot infer topic type; specify type");

            const auto result = ros::onceTopic(request);
            if (!result.value("ok").toBool(false))
                return Result::fail(result.value("message").toString("once failed"));
            writeRosMirror("once/last", Var(result));
            schema::VarS::importNode(out, Var(result));
            return Result::ok();
        }, "Wait for one message.");

        command::reg("ros.service.list", [](Node* in, Node* out) -> Result {
            const auto result = ros::listServices(in->get("filter").toString());
            writeNamedPathList("services", result, "name");
            Var::ListV names;
            for (const auto& item : result) {
                if (item.isDict())
                    names.push_back(item.toDict().value("name"));
                else
                    names.push_back(item);
            }
            schema::VarS::importNode(out, Var(std::move(names)));
            return Result::ok();
        }, "List ROS services.");

        command::reg("ros.service.info", [](Node* in, Node* out) -> Result {
            const auto service_name = in->get("name").toString();
            if (service_name.empty())
                return Result::fail("service name required");
            const auto result = ros::serviceInfo(service_name);
            if (!result.value("ok").toBool(false))
                return Result::fail(result.value("message").toString("service info failed"));
            schema::VarS::importNode(out, Var(result));
            return Result::ok();
        }, "Show service details.");

        command::reg("ros.service.call", [](Node* in, Node* out) -> Result {
            ros::ServiceCallRequest request;
            request.service = in->get("service").toString();
            request.type = in->get("type").toString();
            request.request = in->get("request").toString();
            request.payload_format = in->get("payload_format").toString("yaml");
            request.timeout_wait_ms = static_cast<int>(in->get("timeout_wait_ms").toInt(5000));
            request.timeout_response_ms = static_cast<int>(in->get("timeout_response_ms").toInt(10000));
            if (request.service.empty() || request.type.empty() || request.request.empty())
                return Result::fail("service/type/request required");

            const auto result = ros::callService(request);
            if (!result.value("ok").toBool(false))
                return Result::fail(result.value("message").toString("service call failed"));
            writeRosMirror("service_calls/last", Var(result));
            schema::VarS::importNode(out, Var(result));
            return Result::ok();
        }, "Call a ROS service.");

        command::reg("ros.param.list", [](Node* in, Node* out) -> Result {
            const std::string node_filter = in->get("node").toString();
            const auto result = ros::listParams(node_filter);
            if (!result.value("ok").toBool(false))
                return Result::fail(result.value("message").toString("param list failed"));

            auto* params_root = n("ve/ros/params");

            if (node_filter.empty()) {
                const auto& nodes = result.value("nodes");
                if (nodes.isList()) {
                    for (const auto& nn : nodes.toList())
                        params_root->at(normalizeNamedPath(nn.toString(), "node"));
                }
                schema::VarS::importNode(out, nodes);
                return Result::ok();
            }

            const std::string nn = result.value("node").toString();
            auto* node_n = params_root->at(normalizeNamedPath(nn, "node"));

            const auto& values = result.value("values");
            if (values.isDict()) {
                for (const auto& [pname, pval] : values.toDict())
                    node_n->at(pname)->set(pval);
            }

            Var::ListV names;
            const auto& params = result.value("params");
            if (params.isList()) {
                for (const auto& pname : params.toList())
                    names.push_back(Var(nn + "/" + pname.toString()));
            }
            schema::VarS::importNode(out, Var(std::move(names)));
            return Result::ok();
        }, "List params. No node: list nodes. With node: params+values.");

        command::reg("ros.param.get", [](Node* in, Node* out) -> Result {
            const auto node_name = in->get("node").toString();
            const auto param_name = in->get("name").toString();
            if (node_name.empty() || param_name.empty())
                return Result::fail("node/name required");

            const auto result = ros::getParam(node_name, param_name);
            if (!result.value("ok").toBool(false))
                return Result::fail(result.value("message").toString("param get failed"));
            const std::string nn = result.value("node").toString();
            n("ve/ros/params")->at(normalizeNamedPath(nn, "node"))->at(param_name)->set(result.value("value"));
            schema::VarS::importNode(out, Var(result));
            return Result::ok();
        }, "Get one ROS parameter.");

        command::reg("ros.param.set", [](Node* in, Node* out) -> Result {
            auto node_name = in->get("node").toString();
            auto param_name = in->get("name").toString();
            Var value = in->get("value");
            if (node_name.empty() || param_name.empty() || value.isNull())
                return Result::fail("node/name/value required");
            if (value.isString())
                value = ros::yaml::decode(value.toString());

            const auto result = ros::setParam(node_name, param_name, value);
            if (!result.value("ok").toBool(false))
                return Result::fail(result.value("message").toString("param set failed"));
            const std::string nn = result.value("node").toString();
            n("ve/ros/params")->at(normalizeNamedPath(nn, "node"))->at(param_name)->set(value);
            schema::VarS::importNode(out, Var(result));
            return Result::ok();
        }, "Set one ROS parameter.");

        command::reg("ros.runtime.refresh", [](Node*, Node* out) -> Result {
            std::string error;
            if (!ros::refreshRuntime(n("ve/ros"), error))
                return Result::fail(error);
            schema::VarS::importNode(out, Var(ros::runtimeInfo()));
            return Result::ok();
        }, "Refresh cached ROS lists.");
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
