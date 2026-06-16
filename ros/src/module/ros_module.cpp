// ros_module.cpp  - ve::RosModule (ve.ros)

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
    Node temp("temp");
    if (!ros::topicInfo(topic, &temp)) return "";
    auto* types = temp.find("types");
    if (!types) return "";
    auto list = types->toStrings();
    return list.empty() ? "" : list.front();
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

std::string stripLeadingSlashes(const std::string& name)
{
    std::size_t i = 0;
    while (i < name.size() && name[i] == '/') ++i;
    return i < name.size() ? name.substr(i) : "node";
}

} // namespace

class RosModule : public Module
{
    std::string active_backend_;
    bool commands_registered_ = false;

public:
    explicit RosModule(const std::string& name) : Module(name)
    {
        node()->set("config/domain_id", Var(0));
        node()->set("config/service_prefix", Var("ve"));
        node()->set("config/backend", Var(""));
        node()->set("config/note", Var(
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
            n("ve/ros")->set("last_error", Var(error));
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

        command::reg("ros.info", [this](Node*, Node* out) -> Result {
            buildInfo(out);
            return Result::ok();
        }, "Show ros summary.");

        command::reg("ros.backend.list", [](Node*, Node* out) -> Result {
            ros::backendInfoList(out);
            return Result::ok();
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
            current->info(out);
            return Result::ok();
        }, "Show backend details.");

        command::reg("ros.parser.list", [](Node*, Node* out) -> Result {
            ros::parserInfoList(out);
            return Result::ok();
        }, "List ros parsers.");

        command::reg("ros.env", [](Node*, Node* out) -> Result {
            ros::envInfo(out);
            return Result::ok();
        }, "Show ROS env vars.");

        command::reg("ros.node.list", [](Node* in, Node* out) -> Result {
            auto result = ros::listNodes(in->get("filter").toString());
            auto* mirror = n("ve/ros/nodes");
            schema::VarS::importNode(mirror, Var(result), Node::COPY_STRICT);
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
            auto result = ros::listTopics(in->get("filter").toString());
            auto* mirror = n("ve/ros/topics");
            schema::VarS::importNode(mirror, Var(result), Node::COPY_STRICT);
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
            auto name = in->get("name").toString();
            if (name.empty()) return Result::fail("topic name required");
            return ros::topicInfo(name, out);
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
            auto r = ros::subscribeTopic(config, out);
            if (!r) return r;
            n("ve/ros/subscriptions/" + config.name)->copy(out);
            return Result::ok();
        }, "Subscribe to a topic.");

        command::reg("ros.topic.unsubscribe", [](Node* in, Node* out) -> Result {
            auto name = in->get("name").toString();
            if (name.empty()) return Result::fail("name required");
            auto r = ros::unsubscribeTopic(name, out);
            if (!r) return r;
            n("ve/ros")->erase("subscriptions/" + name);
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
            auto r = ros::publishTopic(request, out);
            if (!r) return r;
            n("ve/ros/publications/last")->copy(out);
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

            auto r = ros::onceTopic(request, out);
            if (!r) return r;
            n("ve/ros/once/last")->copy(out);
            return Result::ok();
        }, "Wait for one message.");

        command::reg("ros.service.list", [](Node* in, Node* out) -> Result {
            auto result = ros::listServices(in->get("filter").toString());
            auto* mirror = n("ve/ros/services");
            schema::VarS::importNode(mirror, Var(result), Node::COPY_STRICT);
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
            auto name = in->get("name").toString();
            if (name.empty()) return Result::fail("service name required");
            return ros::serviceInfo(name, out);
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
            auto r = ros::callService(request, out);
            if (!r) return r;
            n("ve/ros/service_calls/last")->copy(out);
            return Result::ok();
        }, "Call a ROS service.");

        command::reg("ros.param.list", [](Node* in, Node* out) -> Result {
            auto node_filter = in->get("node").toString();
            Node temp("temp");
            auto r = ros::listParams(node_filter, &temp);
            if (!r) return r;

            auto* params_root = n("ve/ros/params");

            if (node_filter.empty()) {
                auto* nodes = temp.find("nodes");
                if (nodes) {
                    auto list = nodes->toStrings();
                    Var::ListV out_list;
                    for (auto& s : list) {
                        params_root->at(stripLeadingSlashes(s));
                        out_list.push_back(Var(std::move(s)));
                    }
                    schema::VarS::importNode(out, Var(std::move(out_list)));
                }
                return Result::ok();
            }

            auto nn = temp.get("node").toString();
            auto key = stripLeadingSlashes(nn);
            auto* node_n = params_root->at(key);

            if (auto* values = temp.find("values"))
                node_n->copy(values);

            Var::ListV names;
            if (auto* params = temp.find("params")) {
                for (const auto& pname : params->toStrings())
                    names.push_back(Var(nn + "/" + pname));
            }
            schema::VarS::importNode(out, Var(std::move(names)));
            return Result::ok();
        }, "List params. No node: list nodes. With node: params+values.");

        command::reg("ros.param.get", [](Node* in, Node* out) -> Result {
            auto node_name = in->get("node").toString();
            auto param_name = in->get("name").toString();
            if (node_name.empty() || param_name.empty())
                return Result::fail("node/name required");
            auto r = ros::getParam(node_name, param_name, out);
            if (!r) return r;
            auto nn = out->get("node").toString();
            if (nn.empty()) nn = node_name;
            n("ve/ros/params/" + stripLeadingSlashes(nn) + "/" + param_name)->set(out->get("value"));
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
            auto r = ros::setParam(node_name, param_name, value, out);
            if (!r) return r;
            auto nn = out->get("node").toString();
            if (nn.empty()) nn = node_name;
            n("ve/ros/params/" + stripLeadingSlashes(nn) + "/" + param_name)->set(value);
            return Result::ok();
        }, "Set one ROS parameter.");

        command::reg("ros.runtime.refresh", [](Node*, Node* out) -> Result {
            std::string error;
            if (!ros::refreshRuntime(n("ve/ros"), error))
                return Result::fail(error);
            return ros::runtimeInfo(out);
        }, "Refresh cached ROS lists.");
    }

    void buildInfo(Node* out) const
    {
        out->set("state", n("ve/ros")->get("state"));
        out->set("domain_id", Var(static_cast<int64_t>(node()->get("config/domain_id").toInt(0))));
        out->set("service_prefix", Var(node()->get("config/service_prefix").toString("ve")));
        out->set("backend_requested", Var(node()->get("config/backend").toString()));
        out->set("backend_active", Var(active_backend_));
        if (auto current = ros::backend(active_backend_))
            current->info(out->at("backend_active_info"));
        ros::backendInfoList(out->at("backends"));
        ros::parserInfoList(out->at("parsers"));
        ros::envInfo(out->at("env"));
        out->at("nodes")->copy(n("ve/ros/nodes"));
        out->at("topics")->copy(n("ve/ros/topics"));
        out->at("services")->copy(n("ve/ros/services"));
        out->at("params")->copy(n("ve/ros/params"));
        out->set("note", Var(node()->get("config/note").toString()));
    }

    void syncRuntimeState(const std::string& state)
    {
        auto* root = n("ve/ros");
        root->set("state", Var(state));
        root->set("domain_id", Var(static_cast<int64_t>(node()->get("config/domain_id").toInt(0))));
        root->set("service_prefix", Var(node()->get("config/service_prefix").toString("ve")));
        root->set("backend_requested", Var(node()->get("config/backend").toString()));
        root->set("backend_active", Var(active_backend_));

        ros::backendInfoList(root->at("backends"));
        ros::parserInfoList(root->at("parsers"));
        ros::envInfo(root->at("env"));
        root->set("note", Var(
            "ve/ros exposes discovery and parameter APIs through backend-neutral entry points."));
    }
};

} // namespace ve

VE_REGISTER_PRIORITY_MODULE(ve.ros, ve::RosModule, 40, 1)
