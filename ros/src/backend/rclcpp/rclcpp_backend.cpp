#include "ve/ros/backend.h"

#include <rclcpp/rclcpp.hpp>
#include <rclcpp/parameter_client.hpp>

#include <chrono>
#include <mutex>
#ifndef _WIN32
#include <unistd.h>
#endif

namespace ve::ros {

namespace {

Var parameterValueToVar(const rclcpp::ParameterValue& value)
{
    switch (value.get_type()) {
    case rclcpp::ParameterType::PARAMETER_BOOL:
        return Var(value.get<bool>());
    case rclcpp::ParameterType::PARAMETER_INTEGER:
        return Var(static_cast<int64_t>(value.get<int64_t>()));
    case rclcpp::ParameterType::PARAMETER_DOUBLE:
        return Var(value.get<double>());
    case rclcpp::ParameterType::PARAMETER_STRING:
        return Var(value.get<std::string>());
    case rclcpp::ParameterType::PARAMETER_BYTE_ARRAY: {
        Var::ListV list;
        for (auto item : value.get<std::vector<uint8_t>>())
            list.push_back(Var(static_cast<int64_t>(item)));
        return Var(std::move(list));
    }
    case rclcpp::ParameterType::PARAMETER_BOOL_ARRAY: {
        Var::ListV list;
        for (auto item : value.get<std::vector<bool>>())
            list.push_back(Var(item));
        return Var(std::move(list));
    }
    case rclcpp::ParameterType::PARAMETER_INTEGER_ARRAY: {
        Var::ListV list;
        for (auto item : value.get<std::vector<int64_t>>())
            list.push_back(Var(static_cast<int64_t>(item)));
        return Var(std::move(list));
    }
    case rclcpp::ParameterType::PARAMETER_DOUBLE_ARRAY: {
        Var::ListV list;
        for (auto item : value.get<std::vector<double>>())
            list.push_back(Var(item));
        return Var(std::move(list));
    }
    case rclcpp::ParameterType::PARAMETER_STRING_ARRAY: {
        Var::ListV list;
        for (const auto& item : value.get<std::vector<std::string>>())
            list.push_back(Var(item));
        return Var(std::move(list));
    }
    default:
        return Var();
    }
}

bool homogenousListType(const Var::ListV& list, Var::Type& type)
{
    if (list.empty()) {
        type = Var::NONE;
        return true;
    }

    type = list.front().type();
    for (const auto& item : list) {
        if (item.type() != type)
            return false;
    }
    return true;
}

bool varToParameterValue(const Var& value, rclcpp::ParameterValue& out, std::string& error)
{
    switch (value.type()) {
    case Var::BOOL:
        out = rclcpp::ParameterValue(value.toBool());
        return true;
    case Var::INT:
        out = rclcpp::ParameterValue(static_cast<int64_t>(value.toInt64()));
        return true;
    case Var::DOUBLE:
        out = rclcpp::ParameterValue(value.toDouble());
        return true;
    case Var::STRING:
        out = rclcpp::ParameterValue(value.toString());
        return true;
    case Var::LIST: {
        Var::Type list_type = Var::NONE;
        if (!homogenousListType(value.toList(), list_type)) {
            error = "parameter arrays must be homogeneous";
            return false;
        }
        if (list_type == Var::NONE) {
            out = rclcpp::ParameterValue(std::vector<std::string>{});
            return true;
        }
        if (list_type == Var::BOOL) {
            std::vector<bool> values;
            for (const auto& item : value.toList())
                values.push_back(item.toBool());
            out = rclcpp::ParameterValue(values);
            return true;
        }
        if (list_type == Var::INT) {
            std::vector<int64_t> values;
            for (const auto& item : value.toList())
                values.push_back(item.toInt64());
            out = rclcpp::ParameterValue(values);
            return true;
        }
        if (list_type == Var::DOUBLE) {
            std::vector<double> values;
            for (const auto& item : value.toList())
                values.push_back(item.toDouble());
            out = rclcpp::ParameterValue(values);
            return true;
        }
        if (list_type == Var::STRING) {
            std::vector<std::string> values;
            for (const auto& item : value.toList())
                values.push_back(item.toString());
            out = rclcpp::ParameterValue(values);
            return true;
        }
        error = "unsupported parameter array type";
        return false;
    }
    default:
        error = "unsupported parameter type";
        return false;
    }
}

std::string fqNodeName(const std::string& name, const std::string& ns)
{
    if (name.empty())
        return "";
    if (ns.empty() || ns == "/")
        return "/" + name;
    if (ns.back() == '/')
        return ns + name;
    return ns + "/" + name;
}

std::string normalizeRemoteNodeName(std::string name)
{
    if (name.empty())
        return name;
    while (name.size() > 1 && name.back() == '/')
        name.pop_back();
    if (name.empty())
        return "/";
    if (name.front() != '/')
        name.insert(name.begin(), '/');
    return name;
}

bool isRuntimeHelperNode(const std::string& name, const std::string& ns)
{
    const std::string full = fqNodeName(name, ns);
    return full.rfind("/ve/ve_ros_runtime", 0) == 0;
}

bool containsFilter(const std::string& text, const std::string& filter)
{
    return filter.empty() || text.find(filter) != std::string::npos;
}

std::string runtimeNodeName()
{
#ifdef _WIN32
    return "ve_ros_runtime";
#else
    return "ve_ros_runtime_" + std::to_string(static_cast<long long>(::getpid()));
#endif
}

bool isInternalParamName(const std::string& name)
{
    return name.rfind("qos_overrides./", 0) == 0
        || name == "start_type_description_service";
}

class RclcppBackend : public Backend
{
public:
    std::string key() const override { return "rclcpp"; }
    std::string displayName() const override { return "ROS2 rclcpp"; }
    std::string transport() const override { return "rmw"; }
    int priority() const override { return 20; }
    std::string summary() const override
    {
        return "Primary ROS2 backend using rclcpp and the active RMW implementation.";
    }

    bool isAvailable() const override { return true; }
    bool isEnabled() const override
    {
        return !env("RMW_IMPLEMENTATION").empty();
    }

    Var::DictV details() const override
    {
        Var::DictV dict;
        dict["rmw_implementation"] = Var(env("RMW_IMPLEMENTATION"));
        dict["domain_id"] = Var(env("ROS_DOMAIN_ID", "0"));
        dict["node_name"] = Var(node_name_);
        dict["node_namespace"] = Var(node_namespace_);
        dict["node_full_name"] = Var(node_full_name_);
        return dict;
    }

    bool start(Node*, std::string& error) override
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (node_)
            return true;

        try {
            context_ = std::make_shared<rclcpp::Context>();
            int argc = 0;
            context_->init(argc, nullptr);

            rclcpp::NodeOptions options;
            options.context(context_);
            options.use_global_arguments(true);
            options.start_parameter_services(true);
            options.start_parameter_event_publisher(true);
            node_name_ = runtimeNodeName();
            node_namespace_ = "/ve";
            node_full_name_ = fqNodeName(node_name_, node_namespace_);
            node_ = std::make_shared<rclcpp::Node>(node_name_, node_namespace_, options);
            if (!node_->has_parameter("backend"))
                node_->declare_parameter("backend", key());
        } catch (const std::exception& e) {
            error = e.what();
            node_.reset();
            context_.reset();
            return false;
        }

        return true;
    }

    void stop() override
    {
        std::lock_guard<std::mutex> lock(mu_);
        node_.reset();
        if (context_) {
            context_->shutdown("ve.ros shutdown");
            context_.reset();
        }
    }

    bool isStarted() const override
    {
        std::lock_guard<std::mutex> lock(mu_);
        return static_cast<bool>(node_);
    }

    Var::ListV listNodes(const std::string& filter = "") const override
    {
        std::lock_guard<std::mutex> lock(mu_);
        Var::ListV items;
        if (!node_)
            return items;

        const auto nodes = node_->get_node_graph_interface()->get_node_names_and_namespaces();
        for (const auto& [name, ns] : nodes) {
            if (isRuntimeHelperNode(name, ns))
                continue;
            const std::string full = fqNodeName(name, ns);
            if (!containsFilter(full, filter) && !containsFilter(name, filter))
                continue;

            Var::DictV item;
            item["name"] = Var(name);
            item["namespace"] = Var(ns);
            item["full_name"] = Var(full);
            items.push_back(Var(std::move(item)));
        }
        return items;
    }

    Var::ListV listTopics(const std::string& filter = "") const override
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (!node_)
            return {};

        Var::ListV items;
        const auto topics = node_->get_topic_names_and_types();
        for (const auto& [name, types] : topics) {
            if (!containsFilter(name, filter))
                continue;

            Var::DictV item;
            item["name"] = Var(name);
            item["publisher_count"] = Var(static_cast<int64_t>(node_->count_publishers(name)));
            item["subscriber_count"] = Var(static_cast<int64_t>(node_->count_subscribers(name)));

            Var::ListV type_list;
            for (const auto& type : types)
                type_list.push_back(Var(type));
            item["types"] = Var(std::move(type_list));
            items.push_back(Var(std::move(item)));
        }
        return items;
    }

    Var::DictV topicInfo(const std::string& topic) const override
    {
        Var::DictV result;
        result["topic"] = Var(topic);

        std::lock_guard<std::mutex> lock(mu_);
        if (!node_) {
            result["ok"] = Var(false);
            result["message"] = Var("rclcpp backend is not started");
            return result;
        }

        const auto topics = node_->get_topic_names_and_types();
        auto it = topics.find(topic);
        if (it == topics.end()) {
            result["ok"] = Var(false);
            result["message"] = Var("topic not found");
            return result;
        }

        Var::ListV type_list;
        for (const auto& type : it->second)
            type_list.push_back(Var(type));

        Var::ListV publishers;
        for (const auto& endpoint : node_->get_publishers_info_by_topic(topic)) {
            Var::DictV item;
            item["node_name"] = Var(endpoint.node_name());
            item["node_namespace"] = Var(endpoint.node_namespace());
            item["topic_type"] = Var(endpoint.topic_type());
            publishers.push_back(Var(std::move(item)));
        }

        Var::ListV subscriptions;
        for (const auto& endpoint : node_->get_subscriptions_info_by_topic(topic)) {
            Var::DictV item;
            item["node_name"] = Var(endpoint.node_name());
            item["node_namespace"] = Var(endpoint.node_namespace());
            item["topic_type"] = Var(endpoint.topic_type());
            subscriptions.push_back(Var(std::move(item)));
        }

        result["ok"] = Var(true);
        result["message"] = Var("topic info ok");
        result["types"] = Var(std::move(type_list));
        result["publisher_count"] = Var(static_cast<int64_t>(node_->count_publishers(topic)));
        result["subscriber_count"] = Var(static_cast<int64_t>(node_->count_subscribers(topic)));
        result["publishers"] = Var(std::move(publishers));
        result["subscriptions"] = Var(std::move(subscriptions));
        return result;
    }

    Var::ListV listServices(const std::string& filter = "") const override
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (!node_)
            return {};

        Var::ListV items;
        const auto services = node_->get_service_names_and_types();
        for (const auto& [name, types] : services) {
            if (!containsFilter(name, filter))
                continue;

            Var::DictV item;
            item["name"] = Var(name);
            item["server_count"] = Var(static_cast<int64_t>(node_->count_services(name)));
            item["client_count"] = Var(static_cast<int64_t>(node_->count_clients(name)));

            Var::ListV type_list;
            for (const auto& type : types)
                type_list.push_back(Var(type));
            item["types"] = Var(std::move(type_list));
            items.push_back(Var(std::move(item)));
        }
        return items;
    }

    Var::DictV serviceInfo(const std::string& service) const override
    {
        Var::DictV result;
        result["service"] = Var(service);

        std::lock_guard<std::mutex> lock(mu_);
        if (!node_) {
            result["ok"] = Var(false);
            result["message"] = Var("rclcpp backend is not started");
            return result;
        }

        const auto services = node_->get_service_names_and_types();
        auto it = services.find(service);
        if (it == services.end()) {
            result["ok"] = Var(false);
            result["message"] = Var("service not found");
            return result;
        }

        Var::ListV type_list;
        for (const auto& type : it->second)
            type_list.push_back(Var(type));

        result["ok"] = Var(true);
        result["message"] = Var("service info ok");
        result["types"] = Var(std::move(type_list));
        result["server_count"] = Var(static_cast<int64_t>(node_->count_services(service)));
        result["client_count"] = Var(static_cast<int64_t>(node_->count_clients(service)));
        return result;
    }

    Var::DictV listParams(const std::string& node_name = "") const override
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (!node_)
            return makeResult(false, "rclcpp backend is not started");

        if (!node_name.empty())
            return listParamsForNodeLocked(normalizeRemoteNodeName(node_name));

        Var::DictV result = makeResult(true, "param list ok");
        Var::DictV nodes_dict;
        for (const auto& [name, ns] : node_->get_node_graph_interface()->get_node_names_and_namespaces()) {
            if (isRuntimeHelperNode(name, ns))
                continue;
            const std::string full = fqNodeName(name, ns);
            nodes_dict[full] = listParamsForNodeLocked(full);
        }
        result["nodes"] = Var(std::move(nodes_dict));
        return result;
    }

    Var::DictV getParam(const std::string& node_name, const std::string& name) const override
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (!node_)
            return makeResult(false, "rclcpp backend is not started");
        const std::string normalized_node_name = normalizeRemoteNodeName(node_name);
        if (normalized_node_name.empty() || name.empty())
            return makeResult(false, "node/name is required");

        auto client = makeParamClientLocked(normalized_node_name);
        if (!client->wait_for_service(std::chrono::milliseconds(1000)))
            return makeResult(false, "parameter service not ready");

        Var::DictV result = makeResult(true, "param get ok");
        const auto params = client->get_parameters({name}, std::chrono::milliseconds(1000));
        if (params.empty()) {
            result["ok"] = Var(false);
            result["message"] = Var("parameter not found");
            return result;
        }
        result["node"] = Var(normalized_node_name);
        result["name"] = Var(name);
        result["value"] = parameterValueToVar(params.front().get_parameter_value());
        return result;
    }

    Var::DictV setParam(const std::string& node_name, const std::string& name, const Var& value) const override
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (!node_)
            return makeResult(false, "rclcpp backend is not started");
        const std::string normalized_node_name = normalizeRemoteNodeName(node_name);
        if (normalized_node_name.empty() || name.empty())
            return makeResult(false, "node/name is required");

        rclcpp::ParameterValue parameter_value;
        std::string error;
        if (!varToParameterValue(value, parameter_value, error))
            return makeResult(false, error);

        auto client = makeParamClientLocked(normalized_node_name);
        if (!client->wait_for_service(std::chrono::milliseconds(1000)))
            return makeResult(false, "parameter service not ready");

        auto results = client->set_parameters({rclcpp::Parameter(name, parameter_value)},
                                              std::chrono::milliseconds(1000));
        Var::DictV result = makeResult(!results.empty() && results.front().successful,
                                       results.empty() ? "parameter set failed" : results.front().reason);
        result["node"] = Var(normalized_node_name);
        result["name"] = Var(name);
        result["value"] = value;
        if (!results.empty())
            result["successful"] = Var(results.front().successful);
        return result;
    }

private:
    std::shared_ptr<rclcpp::SyncParametersClient> makeParamClientLocked(const std::string& node_name) const
    {
        rclcpp::ExecutorOptions options;
        options.context = context_;
        auto executor = std::make_shared<rclcpp::executors::SingleThreadedExecutor>(options);
        return std::make_shared<rclcpp::SyncParametersClient>(executor, node_, node_name);
    }

    Var::DictV makeResult(bool ok, std::string message) const
    {
        Var::DictV result;
        result["ok"] = Var(ok);
        result["message"] = Var(std::move(message));
        return result;
    }

    Var::DictV listParamsForNodeLocked(const std::string& node_name) const
    {
        Var::DictV result = makeResult(false, "param list failed");
        result["node"] = Var(node_name);

        auto client = makeParamClientLocked(node_name);
        if (!client->wait_for_service(std::chrono::milliseconds(500))) {
            result["message"] = Var("parameter service not ready");
            return result;
        }

        const auto listed = client->list_parameters({}, 100, std::chrono::milliseconds(1000));
        Var::ListV names;
        Var::ListV raw_names;
        for (const auto& name : listed.names)
        {
            raw_names.push_back(Var(name));
            if (!isInternalParamName(name))
                names.push_back(Var(name));
        }
        result["ok"] = Var(true);
        result["message"] = Var("param list ok");
        result["params"] = Var(std::move(names));
        result["params_raw"] = Var(std::move(raw_names));
        return result;
    }

    mutable std::mutex mu_;
    rclcpp::Context::SharedPtr context_;
    rclcpp::Node::SharedPtr node_;
    std::string node_name_;
    std::string node_namespace_;
    std::string node_full_name_;
};

const bool registered = []() {
    registerBackend(std::make_shared<RclcppBackend>());
    return true;
}();

} // namespace

} // namespace ve::ros
