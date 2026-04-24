#ifdef VE_ROS_HAS_DYNAMIC_TYPESUPPORT
#include "dynamic_typesupport_bridge.h"
#endif

#include "ve/ros/backend.h"
#include "ve/core/schema.h"
#include "ve/ros/yaml_schema.h"

#include <rclcpp/rclcpp.hpp>
#include <rclcpp/parameter_client.hpp>

#include <chrono>
#include <cstring>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
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

std::string encodeHex(const uint8_t* data, std::size_t size)
{
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < size; ++i)
        oss << std::setw(2) << static_cast<int>(data[i]);
    return oss.str();
}

bool decodeHex(const std::string& text, std::vector<uint8_t>& bytes)
{
    if (text.size() % 2 != 0)
        return false;

    bytes.clear();
    bytes.reserve(text.size() / 2);
    for (std::size_t i = 0; i < text.size(); i += 2) {
        unsigned int value = 0;
        std::istringstream iss(text.substr(i, 2));
        iss >> std::hex >> value;
        if (iss.fail())
            return false;
        bytes.push_back(static_cast<uint8_t>(value));
    }
    return true;
}

std::string normalizedPayloadFormat(std::string format)
{
    std::transform(format.begin(), format.end(), format.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return format.empty() ? "yaml" : format;
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

Var::DictV makeResult(bool ok, std::string message)
{
    Var::DictV result;
    result["ok"] = Var(ok);
    result["message"] = Var(std::move(message));
    return result;
}

Var::DictV decodedMessageResult(const rclcpp::SerializedMessage& message,
                                const std::string& topic,
                                const std::string& type,
                                const std::string& payload_format,
#ifdef VE_ROS_HAS_DYNAMIC_TYPESUPPORT
                                const std::shared_ptr<ve::ros::rclcpp_backend::DynamicTypesupportBridge>& bridge
#else
                                const std::shared_ptr<void>& bridge
#endif
                                )
{
    Var::DictV result = makeResult(true, "topic message decoded");
    result["topic"] = Var(topic);
    result["type"] = Var(type);
    result["payload_format"] = Var(payload_format);
    result["size"] = Var(static_cast<int64_t>(message.size()));

    const auto& raw = message.get_rcl_serialized_message();
    if (payload_format == "cdr_hex") {
        result["data"] = Var(encodeHex(raw.buffer, raw.buffer_length));
        return result;
    }

#ifdef VE_ROS_HAS_DYNAMIC_TYPESUPPORT
    Var decoded;
    std::string error;
    if (bridge && bridge->deserializeToVar(message, decoded, error)) {
        result["value"] = decoded;
        if (payload_format == "yaml")
            result["yaml"] = Var(ve::ros::yaml::encode(decoded));
        return result;
    }

    result["ok"] = Var(false);
    result["message"] = Var(error);
    result["data"] = Var(encodeHex(raw.buffer, raw.buffer_length));
    result["fallback_format"] = Var("cdr_hex");
#else
    result["ok"] = Var(false);
    result["message"] = Var("dynamic typesupport not available (ROS 2 Foxy), use cdr_hex format");
    result["data"] = Var(encodeHex(raw.buffer, raw.buffer_length));
    result["fallback_format"] = Var("cdr_hex");
#endif
    return result;
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

            rclcpp::ExecutorOptions exec_options;
            exec_options.context = context_;
            executor_ = std::make_shared<rclcpp::executors::SingleThreadedExecutor>(exec_options);
            executor_->add_node(node_);
            spinning_.store(true);
            spin_thread_ = std::thread([this]() {
                while (spinning_.load()) {
                    executor_->spin_some();
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            });
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
        {
            std::lock_guard<std::mutex> lock(param_clients_mu_);
            param_clients_.clear();
        }
        std::lock_guard<std::mutex> lock(mu_);
        subscriptions_.clear();
        publishers_.clear();
        spinning_.store(false);
        if (executor_) {
            executor_->cancel();
            if (node_)
                executor_->remove_node(node_);
        }
        if (spin_thread_.joinable())
            spin_thread_.join();
        executor_.reset();
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

    Var::DictV subscribeTopic(const TopicSubscriptionConfig& config) override
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (!node_)
            return makeResult(false, "rclcpp backend is not started");
        if (config.name.empty() || config.topic.empty())
            return makeResult(false, "name/topic is required");

        std::string topic_type = config.type.empty() ? inferTopicTypeLocked(config.topic) : config.type;
        if (topic_type.empty())
            return makeResult(false, "topic type is required");

        const std::string payload_format = normalizedPayloadFormat(config.payload_format);
        std::string bridge_error;
#ifdef VE_ROS_HAS_DYNAMIC_TYPESUPPORT
        auto bridge = std::make_shared<ve::ros::rclcpp_backend::DynamicTypesupportBridge>();
        if (payload_format != "cdr_hex" && !bridge->initialize(topic_type, bridge_error))
            return makeResult(false, "failed to initialize dynamic bridge: " + bridge_error);
#else
        auto bridge = std::shared_ptr<void>{};
        if (payload_format != "cdr_hex")
            return makeResult(false, "dynamic typesupport not available on Foxy, use payload_format=cdr_hex");
#endif

        auto target_path = config.target_node;
        if (target_path.empty())
            target_path = "ve/ros/runtime/messages/" + config.name;

        auto subscription = node_->create_generic_subscription(
            config.topic,
            topic_type,
            rclcpp::QoS(10),
            [name = config.name,
             topic = config.topic,
             type = topic_type,
             target_path,
             payload_format,
             bridge,
             this](std::shared_ptr<rclcpp::SerializedMessage> message) {
                Node payload("payload");
                const auto decoded = decodedMessageResult(*message, topic, type, payload_format, bridge);
                schema::importAs<schema::VarS>(&payload, Var(decoded), schema::ImportOptions{true, false, true});

                if (!target_path.empty())
                    ve::n(target_path)->copy(&payload, true, true, true);
                auto* rx = ve::n("ve/ros/runtime/subscriptions/" + name + "/messages_rx");
                rx->set(rx->getInt64(0) + 1);
            });

        SubscriptionInfo info;
        info.config = config;
        info.config.type = topic_type;
        info.config.payload_format = payload_format;
        info.target_node = target_path;
        info.subscription = subscription;
        subscriptions_.insertOne(config.name, std::move(info));

        Var::DictV result = makeResult(true, "topic subscribed");
        result["name"] = Var(config.name);
        result["topic"] = Var(config.topic);
        result["type"] = Var(topic_type);
        result["target_node"] = Var(target_path);
        result["payload_format"] = Var(payload_format);
        return result;
    }

    Var::DictV unsubscribeTopic(const std::string& name) override
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (!subscriptions_.has(name))
            return makeResult(false, "subscription not found");
        subscriptions_.erase(name);

        Var::DictV result = makeResult(true, "topic unsubscribed");
        result["name"] = Var(name);
        return result;
    }

    Var::DictV publishTopic(const TopicPublishRequest& request) override
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (!node_)
            return makeResult(false, "rclcpp backend is not started");
        if (request.topic.empty())
            return makeResult(false, "topic is required");

        std::string topic_type = request.type.empty() ? inferTopicTypeLocked(request.topic) : request.type;
        if (topic_type.empty())
            return makeResult(false, "topic type is required");

        const std::string payload_format = normalizedPayloadFormat(request.payload_format);
        rclcpp::SerializedMessage message;
        if (payload_format == "cdr_hex") {
            std::vector<uint8_t> bytes;
            if (!decodeHex(request.payload, bytes))
                return makeResult(false, "invalid cdr_hex payload");
            message.reserve(bytes.size());
            auto & raw = message.get_rcl_serialized_message();
            std::memcpy(raw.buffer, bytes.data(), bytes.size());
            raw.buffer_length = bytes.size();
        } else {
#ifdef VE_ROS_HAS_DYNAMIC_TYPESUPPORT
            auto bridge = std::make_shared<ve::ros::rclcpp_backend::DynamicTypesupportBridge>();
            std::string error;
            if (!bridge->initialize(topic_type, error))
                return makeResult(false, "failed to initialize dynamic bridge: " + error);

            const Var payload = payload_format == "yaml"
                ? ve::ros::yaml::decode(request.payload)
                : ve::ros::yaml::decode(request.payload);
            if (!bridge->serializeFromVar(payload, message, error))
                return makeResult(false, "failed to serialize payload: " + error);
#else
            return makeResult(false, "dynamic typesupport not available on Foxy, use payload_format=cdr_hex");
#endif
        }

        auto publisher = publishers_.value(request.topic, rclcpp::GenericPublisher::SharedPtr{});
        if (!publisher) {
            publisher = node_->create_generic_publisher(request.topic, topic_type, rclcpp::QoS(10));
            publishers_.insertOne(request.topic, publisher);
        }

        publisher->publish(message);

        Var::DictV result = makeResult(true, "topic publish ok");
        result["topic"] = Var(request.topic);
        result["type"] = Var(topic_type);
        result["size"] = Var(static_cast<int64_t>(message.size()));
        result["payload_format"] = Var(payload_format);
        return result;
    }

    Var::DictV onceTopic(const TopicOnceRequest& request) override
    {
        std::shared_ptr<rclcpp::Node> node;
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (!node_)
                return makeResult(false, "rclcpp backend is not started");
            node = node_;
        }

        if (request.topic.empty())
            return makeResult(false, "topic is required");

        const std::string topic_type = request.type.empty()
            ? inferTopicTypeLocked(request.topic)
            : request.type;
        if (topic_type.empty())
            return makeResult(false, "topic type is required");

        const std::string payload_format = normalizedPayloadFormat(request.payload_format);
#ifdef VE_ROS_HAS_DYNAMIC_TYPESUPPORT
        auto bridge = std::make_shared<ve::ros::rclcpp_backend::DynamicTypesupportBridge>();
        std::string bridge_error;
        if (payload_format != "cdr_hex" && !bridge->initialize(topic_type, bridge_error))
            return makeResult(false, "failed to initialize dynamic bridge: " + bridge_error);
#else
        auto bridge = std::shared_ptr<void>{};
        if (payload_format != "cdr_hex")
            return makeResult(false, "dynamic typesupport not available on Foxy, use payload_format=cdr_hex");
#endif

        auto promise = std::make_shared<std::promise<Var::DictV>>();
        auto future = promise->get_future();
        auto delivered = std::make_shared<std::atomic<bool>>(false);

        auto subscription = node->create_generic_subscription(
            request.topic,
            topic_type,
            rclcpp::QoS(10),
            [promise, delivered, topic = request.topic, type = topic_type, payload_format, bridge]
            (std::shared_ptr<rclcpp::SerializedMessage> message) {
                if (delivered->exchange(true))
                    return;
                promise->set_value(decodedMessageResult(*message, topic, type, payload_format, bridge));
            });

        const auto status = future.wait_for(std::chrono::milliseconds(request.timeout_ms));
        if (status != std::future_status::ready)
            return makeResult(false, "topic once timeout");

        auto result = future.get();
        if (result.value("ok").toBool(false) && !request.target_node.empty()) {
            Node payload("payload");
            schema::importAs<schema::VarS>(&payload, Var(result), schema::ImportOptions{true, false, true});
            ve::n(request.target_node)->copy(&payload, true, true, true);
            result["target_node"] = Var(request.target_node);
        }
        result["message"] = Var(result.value("ok").toBool(false) ? "topic once ok" : result.value("message").toString());
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

    Var::DictV callService(const std::string& service, const std::string& type,
                          const std::string& request, const std::string& payload_format) override
    {
        std::shared_ptr<rclcpp::Node> node;
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (!node_)
                return makeResult(false, "rclcpp backend is not started");
            node = node_;
        }

        if (service.empty())
            return makeResult(false, "service name is required");
        if (type.empty())
            return makeResult(false, "service type is required");

        const std::string fmt = normalizedPayloadFormat(payload_format);
#ifndef VE_ROS_HAS_DYNAMIC_TYPESUPPORT
        return makeResult(false, "dynamic typesupport not available on Foxy, service calls require Galactic+");
#else
        auto bridge = std::make_shared<ve::ros::rclcpp_backend::DynamicTypesupportBridge>();
        std::string bridge_error;
        if (fmt != "cdr_hex" && !bridge->initialize(type, bridge_error))
            return makeResult(false, "failed to initialize dynamic bridge: " + bridge_error);

        Var request_var;
        if (fmt == "yaml") {
            request_var = ve::ros::yaml::decode(request);
        } else if (fmt == "var") {
            request_var = ve::ros::yaml::decode(request);
        } else if (fmt == "cdr_hex") {
            return makeResult(false, "cdr_hex format not supported for service request");
        } else {
            return makeResult(false, "unsupported payload format: " + fmt);
        }

        rclcpp::SerializedMessage request_msg;
        if (!bridge->serializeFromVar(request_var, request_msg, bridge_error))
            return makeResult(false, "failed to serialize request: " + bridge_error);

        auto client = node->create_generic_client(service, type);
        if (!client->wait_for_service(std::chrono::seconds(5)))
            return makeResult(false, "service not available: " + service);

        auto& raw_request = request_msg.get_rcl_serialized_message();
        auto future_and_id = client->async_send_request(static_cast<void*>(&raw_request));
        if (future_and_id.future.wait_for(std::chrono::seconds(10)) != std::future_status::ready)
            return makeResult(false, "service call timeout");

        auto response_shared = future_and_id.future.get();
        auto* response_raw = static_cast<rcl_serialized_message_t*>(response_shared.get());
        rclcpp::SerializedMessage response_msg(*response_raw);
        Var response_var;
        if (!bridge->deserializeToVar(response_msg, response_var, bridge_error))
            return makeResult(false, "failed to deserialize response: " + bridge_error);

        Var::DictV result = makeResult(true, "service call ok");
        result["service"] = Var(service);
        result["type"] = Var(type);
        result["payload_format"] = Var(fmt);
        result["response"] = response_var;
        if (fmt == "yaml")
            result["yaml"] = Var(ve::ros::yaml::encode(response_var));
        return result;
#endif
    }

    Var::DictV listParams(const std::string& node_name = "") const override
    {
        std::shared_ptr<rclcpp::Node> node;
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (!node_)
                return makeResult(false, "rclcpp backend is not started");
            node = node_;
        }

        // Specific node: return param names + values
        if (!node_name.empty()) {
            const std::string nn = normalizeRemoteNodeName(node_name);
            auto client = paramClient(node, nn);
            if (!client)
                return makeResult(false, "parameter service not ready");

            auto list_future = client->list_parameters({}, 0);
            if (list_future.wait_for(std::chrono::milliseconds(1000)) != std::future_status::ready)
                return makeResult(false, "param list timeout");

            Var::ListV param_names;
            std::vector<std::string> names_to_get;
            for (const auto& n : list_future.get().names) {
                if (!isInternalParamName(n)) {
                    param_names.push_back(Var(n));
                    names_to_get.push_back(n);
                }
            }

            Var::DictV params_dict;
            if (!names_to_get.empty()) {
                auto get_future = client->get_parameters(names_to_get);
                if (get_future.wait_for(std::chrono::milliseconds(2000)) == std::future_status::ready) {
                    const auto values = get_future.get();
                    for (std::size_t i = 0; i < values.size() && i < names_to_get.size(); ++i)
                        params_dict[names_to_get[i]] = parameterValueToVar(values[i].get_parameter_value());
                }
            }

            Var::DictV result = makeResult(true, "param list ok");
            result["node"] = Var(nn);
            result["params"] = Var(std::move(param_names));
            result["values"] = Var(std::move(params_dict));
            return result;
        }

        // No node specified: just return node names (don't query params)
        Var::DictV result = makeResult(true, "param list ok");
        Var::ListV nodes_list;
        for (const auto& [name, ns] : node->get_node_graph_interface()->get_node_names_and_namespaces()) {
            if (isRuntimeHelperNode(name, ns))
                continue;
            nodes_list.push_back(Var(fqNodeName(name, ns)));
        }
        result["nodes"] = Var(std::move(nodes_list));
        return result;
    }

    Var::DictV getParam(const std::string& node_name, const std::string& name) const override
    {
        std::shared_ptr<rclcpp::Node> node;
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (!node_)
                return makeResult(false, "rclcpp backend is not started");
            node = node_;
        }

        const std::string normalized_node_name = normalizeRemoteNodeName(node_name);
        if (normalized_node_name.empty() || name.empty())
            return makeResult(false, "node/name is required");

        auto client = paramClient(node, normalized_node_name);
        if (!client)
            return makeResult(false, "parameter service not ready");

        auto future = client->get_parameters({name});
        if (future.wait_for(std::chrono::milliseconds(1000)) != std::future_status::ready)
            return makeResult(false, "param get timeout");

        const auto params = future.get();
        Var::DictV result = makeResult(true, "param get ok");
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
        std::shared_ptr<rclcpp::Node> node;
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (!node_)
                return makeResult(false, "rclcpp backend is not started");
            node = node_;
        }

        const std::string normalized_node_name = normalizeRemoteNodeName(node_name);
        if (normalized_node_name.empty() || name.empty())
            return makeResult(false, "node/name is required");

        rclcpp::ParameterValue parameter_value;
        std::string error;
        if (!varToParameterValue(value, parameter_value, error))
            return makeResult(false, error);

        auto client = paramClient(node, normalized_node_name);
        if (!client)
            return makeResult(false, "parameter service not ready");

        auto future = client->set_parameters({rclcpp::Parameter(name, parameter_value)});
        if (future.wait_for(std::chrono::milliseconds(1000)) != std::future_status::ready)
            return makeResult(false, "param set timeout");

        const auto results = future.get();
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
    struct SubscriptionInfo {
        TopicSubscriptionConfig config;
        std::string target_node;
        rclcpp::GenericSubscription::SharedPtr subscription;
    };

    std::string inferTopicTypeLocked(const std::string& topic_name) const
    {
        const auto topics = node_->get_topic_names_and_types();
        auto it = topics.find(topic_name);
        if (it == topics.end() || it->second.empty())
            return "";
        return it->second.front();
    }

    // Get or create a cached AsyncParametersClient for the given node.
    // Caller must NOT hold mu_ (wait_for_service blocks).
    std::shared_ptr<rclcpp::AsyncParametersClient> paramClient(
        const std::shared_ptr<rclcpp::Node>& node,
        const std::string& remote_node_name) const
    {
        {
            std::lock_guard<std::mutex> lock(param_clients_mu_);
            auto it = param_clients_.find(remote_node_name);
            if (it != param_clients_.end())
                return it->second;
        }

        auto client = std::make_shared<rclcpp::AsyncParametersClient>(node, remote_node_name);
        if (!client->wait_for_service(std::chrono::milliseconds(1000)))
            return nullptr;

        {
            std::lock_guard<std::mutex> lock(param_clients_mu_);
            param_clients_[remote_node_name] = client;
        }
        return client;
    }

    mutable std::mutex mu_;
    mutable std::mutex param_clients_mu_;
    rclcpp::Context::SharedPtr context_;
    rclcpp::Node::SharedPtr node_;
    rclcpp::Executor::SharedPtr executor_;
    std::thread spin_thread_;
    std::atomic<bool> spinning_{false};
    std::string node_name_;
    std::string node_namespace_;
    std::string node_full_name_;
    Dict<SubscriptionInfo> subscriptions_;
    Dict<rclcpp::GenericPublisher::SharedPtr> publishers_;
    mutable std::unordered_map<std::string, std::shared_ptr<rclcpp::AsyncParametersClient>> param_clients_;
};

const bool registered = []() {
    registerBackend(std::make_shared<RclcppBackend>());
    return true;
}();

} // namespace

} // namespace ve::ros
