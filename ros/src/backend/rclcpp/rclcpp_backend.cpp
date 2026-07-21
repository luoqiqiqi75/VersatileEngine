#include "ve/ros/backend.h"

#include "ve/core/schema.h"
#include "ve/ros/yaml_schema.h"

#include <rclcpp/rclcpp.hpp>
#include <rclcpp/parameter_client.hpp>

#ifdef VE_ROS_HAS_DYNAMIC_TYPESUPPORT
#include "dynamic_typesupport_bridge.h"
#endif

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

rclcpp::QoS makeQos(const QosProfile& q)
{
    const int depth = q.depth > 0 ? q.depth : 10;
    rclcpp::QoS qos = (q.history == "keep_all")
        ? rclcpp::QoS(rclcpp::KeepAll())
        : rclcpp::QoS(rclcpp::KeepLast(depth));

    if (q.reliability == "best_effort")
        qos.best_effort();
    else
        qos.reliable();

    if (q.durability == "transient_local")
        qos.transient_local();
    else
        qos.durability_volatile();

    return qos;
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

Result decodedMessageResult(const rclcpp::SerializedMessage& message,
                            const std::string& topic,
                            const std::string& type,
                            const std::string& payload_format,
#ifdef VE_ROS_HAS_DYNAMIC_TYPESUPPORT
                            const std::shared_ptr<ve::ros::rclcpp_backend::DynamicTypesupportBridge>& bridge,
#else
                            const std::shared_ptr<void>& bridge,
#endif
                            Node* out)
{
    out->set("topic", Var(topic));
    out->set("type", Var(type));
    out->set("payload_format", Var(payload_format));
    out->set("size", Var(static_cast<int64_t>(message.size())));

    const auto& raw = message.get_rcl_serialized_message();
    if (payload_format == "cdr_hex") {
        out->set("data", Var(encodeHex(raw.buffer, raw.buffer_length)));
        return Result::ok();
    }

#ifdef VE_ROS_HAS_DYNAMIC_TYPESUPPORT
    Var decoded;
    std::string error;
    if (bridge && bridge->deserializeToVar(message, decoded, error)) {
        schema::VarS::toNode(out->at("value"), decoded);
        if (payload_format == "yaml")
            out->set("yaml", Var(ve::ros::yaml::encode(decoded)));
        return Result::ok();
    }

    out->set("data", Var(encodeHex(raw.buffer, raw.buffer_length)));
    out->set("fallback_format", Var(std::string("cdr_hex")));
    return Result::fail(error);
#else
    (void)bridge;
    out->set("data", Var(encodeHex(raw.buffer, raw.buffer_length)));
    out->set("fallback_format", Var(std::string("cdr_hex")));
    return Result::fail("dynamic typesupport not available (ROS 2 Foxy), use cdr_hex format");
#endif
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
#ifdef VE_ROS_HAS_GENERIC_PUBSUB
        publishers_.clear();
#ifdef VE_ROS_HAS_DYNAMIC_TYPESUPPORT
        publisher_bridges_.clear();
#endif
#endif
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

    Result topicInfo(const std::string& topic, Node* out) const override
    {
        if (!out) return Result::fail("no output node");

        std::lock_guard<std::mutex> lock(mu_);
        if (!node_) return Result::fail("rclcpp backend is not started");

        const auto topics = node_->get_topic_names_and_types();
        auto it = topics.find(topic);
        if (it == topics.end()) return Result::fail("topic not found");

        out->set("topic", Var(topic));

        Var::ListV type_list;
        for (const auto& type : it->second)
            type_list.push_back(Var(type));
        schema::VarS::toNode(out->at("types"), Var(std::move(type_list)));

        Var::ListV publishers;
        for (const auto& endpoint : node_->get_publishers_info_by_topic(topic)) {
            Var::DictV item;
            item["node_name"] = Var(endpoint.node_name());
            item["node_namespace"] = Var(endpoint.node_namespace());
            item["topic_type"] = Var(endpoint.topic_type());
            publishers.push_back(Var(std::move(item)));
        }
        schema::VarS::toNode(out->at("publishers"), Var(std::move(publishers)));

        Var::ListV subscriptions;
        for (const auto& endpoint : node_->get_subscriptions_info_by_topic(topic)) {
            Var::DictV item;
            item["node_name"] = Var(endpoint.node_name());
            item["node_namespace"] = Var(endpoint.node_namespace());
            item["topic_type"] = Var(endpoint.topic_type());
            subscriptions.push_back(Var(std::move(item)));
        }
        schema::VarS::toNode(out->at("subscriptions"), Var(std::move(subscriptions)));

        out->set("publisher_count", Var(static_cast<int64_t>(node_->count_publishers(topic))));
        out->set("subscriber_count", Var(static_cast<int64_t>(node_->count_subscribers(topic))));
        return Result::ok();
    }

    Result subscribeTopic(const TopicSubscriptionConfig& config, Node* out) override
    {
#ifndef VE_ROS_HAS_GENERIC_PUBSUB
        return Result::fail("GenericSubscription not available on Foxy, requires Galactic+");
#else
        std::lock_guard<std::mutex> lock(mu_);
        if (!node_) return Result::fail("rclcpp backend is not started");
        if (config.name.empty() || config.topic.empty())
            return Result::fail("name/topic is required");

        std::string topic_type = config.type.empty() ? inferTopicTypeLocked(config.topic) : config.type;
        if (topic_type.empty()) return Result::fail("topic type is required");

        const std::string payload_format = normalizedPayloadFormat(config.payload_format);
        std::string bridge_error;
#ifdef VE_ROS_HAS_DYNAMIC_TYPESUPPORT
        auto bridge = std::make_shared<ve::ros::rclcpp_backend::DynamicTypesupportBridge>();
        if (payload_format != "cdr_hex" && !bridge->initialize(topic_type, bridge_error))
            return Result::fail("failed to initialize dynamic bridge: " + bridge_error);
#else
        auto bridge = std::shared_ptr<void>{};
        if (payload_format != "cdr_hex")
            return Result::fail("dynamic typesupport not available on Foxy, use payload_format=cdr_hex");
#endif

        auto target_path = config.target_node;
        if (target_path.empty())
            target_path = "ve/ros/runtime/messages/" + config.name;

        auto subscription = node_->create_generic_subscription(
            config.topic,
            topic_type,
            makeQos(config.qos),
            [name = config.name,
             topic = config.topic,
             type = topic_type,
             target_path,
             payload_format,
             bridge](std::shared_ptr<rclcpp::SerializedMessage> message) {
                Node payload("payload");
                decodedMessageResult(*message, topic, type, payload_format, bridge, &payload);

                if (!target_path.empty())
                    ve::n(target_path)->copy(&payload, Node::COPY_STRICT);
                auto* rx = ve::n("ve/ros/runtime/subscriptions/" + name + "/messages_rx");
                rx->set(Var(rx->getInt64(0) + 1));
            });

        SubscriptionInfo info;
        info.config = config;
        info.config.type = topic_type;
        info.config.payload_format = payload_format;
        info.target_node = target_path;
        info.subscription = subscription;
        subscriptions_.insertOne(config.name, std::move(info));

        if (out) {
            out->set("name", Var(config.name));
            out->set("topic", Var(config.topic));
            out->set("type", Var(topic_type));
            out->set("target_node", Var(target_path));
            out->set("payload_format", Var(payload_format));
        }
        return Result::ok();
#endif
    }

    Result unsubscribeTopic(const std::string& name, Node* out) override
    {
#ifndef VE_ROS_HAS_GENERIC_PUBSUB
        return Result::fail("GenericSubscription not available on Foxy, requires Galactic+");
#else
        std::lock_guard<std::mutex> lock(mu_);
        if (!subscriptions_.has(name))
            return Result::fail("subscription not found");
        subscriptions_.erase(name);
        if (out)
            out->set("name", Var(name));
        return Result::ok();
#endif
    }

    Result publishTopic(const TopicPublishRequest& request, Node* out) override
    {
#ifndef VE_ROS_HAS_GENERIC_PUBSUB
        return Result::fail("GenericPublisher not available on Foxy, requires Galactic+");
#else
        if (request.topic.empty()) return Result::fail("topic is required");

        const std::string payload_format = normalizedPayloadFormat(request.payload_format);
        std::string topic_type;
        rclcpp::GenericPublisher::SharedPtr publisher;
#ifdef VE_ROS_HAS_DYNAMIC_TYPESUPPORT
        std::shared_ptr<ve::ros::rclcpp_backend::DynamicTypesupportBridge> bridge;
#endif

        {
            std::lock_guard<std::mutex> lock(mu_);
            if (!node_) return Result::fail("rclcpp backend is not started");

            topic_type = request.type.empty()
                ? inferTopicTypeLocked(request.topic)
                : request.type;
            if (topic_type.empty()) return Result::fail("topic type is required");

            publisher = publishers_.value(
                request.topic, rclcpp::GenericPublisher::SharedPtr{});
            if (!publisher) {
                publisher = node_->create_generic_publisher(
                    request.topic, topic_type, makeQos(request.qos));
                publishers_.insertOne(request.topic, publisher);
            }

#ifdef VE_ROS_HAS_DYNAMIC_TYPESUPPORT
            if (payload_format != "cdr_hex") {
                bridge = publisher_bridges_.value(
                    request.topic,
                    std::shared_ptr<ve::ros::rclcpp_backend::DynamicTypesupportBridge>{});
                if (!bridge || bridge->type() != topic_type) {
                    bridge = std::make_shared<
                        ve::ros::rclcpp_backend::DynamicTypesupportBridge>();
                    std::string error;
                    if (!bridge->initialize(topic_type, error))
                        return Result::fail(
                            "failed to initialize dynamic bridge: " + error);
                    publisher_bridges_.insertOne(request.topic, bridge);
                }
            }
#endif
        }

        rclcpp::SerializedMessage message;
        if (payload_format == "cdr_hex") {
            std::vector<uint8_t> bytes;
            if (!decodeHex(request.payload, bytes))
                return Result::fail("invalid cdr_hex payload");
            message.reserve(bytes.size());
            auto& raw = message.get_rcl_serialized_message();
            std::memcpy(raw.buffer, bytes.data(), bytes.size());
            raw.buffer_length = bytes.size();
        } else {
#ifdef VE_ROS_HAS_DYNAMIC_TYPESUPPORT
            const Var payload = payload_format == "var"
                ? request.value
                : ve::ros::yaml::decode(request.payload);
            std::string error;
            if (!bridge->serializeFromVar(payload, message, error))
                return Result::fail("failed to serialize payload: " + error);
#else
            return Result::fail(
                "dynamic typesupport not available on Foxy, use payload_format=cdr_hex");
#endif
        }

        publisher->publish(message);

        if (out) {
            out->set("topic", Var(request.topic));
            out->set("type", Var(topic_type));
            out->set("size", Var(static_cast<int64_t>(message.size())));
            out->set("payload_format", Var(payload_format));
        }
        return Result::ok();
#endif
    }

    Result onceTopic(const TopicOnceRequest& request, Node* out) override
    {
#ifndef VE_ROS_HAS_GENERIC_PUBSUB
        return Result::fail("GenericSubscription not available on Foxy, requires Galactic+");
#else
        std::shared_ptr<rclcpp::Node> node;
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (!node_) return Result::fail("rclcpp backend is not started");
            node = node_;
        }

        if (request.topic.empty()) return Result::fail("topic is required");

        const std::string topic_type = request.type.empty()
            ? inferTopicTypeLocked(request.topic)
            : request.type;
        if (topic_type.empty()) return Result::fail("topic type is required");

        const std::string payload_format = normalizedPayloadFormat(request.payload_format);
#ifdef VE_ROS_HAS_DYNAMIC_TYPESUPPORT
        auto bridge = std::make_shared<ve::ros::rclcpp_backend::DynamicTypesupportBridge>();
        std::string bridge_error;
        if (payload_format != "cdr_hex" && !bridge->initialize(topic_type, bridge_error))
            return Result::fail("failed to initialize dynamic bridge: " + bridge_error);
#else
        auto bridge = std::shared_ptr<void>{};
        if (payload_format != "cdr_hex")
            return Result::fail("dynamic typesupport not available on Foxy, use payload_format=cdr_hex");
#endif

        auto temp = std::make_shared<Node>("temp");
        auto promise = std::make_shared<std::promise<Result>>();
        auto future = promise->get_future();
        auto delivered = std::make_shared<std::atomic<bool>>(false);

        auto subscription = node->create_generic_subscription(
            request.topic,
            topic_type,
            makeQos(request.qos),
            [promise, delivered, temp, topic = request.topic, type = topic_type, payload_format, bridge]
            (std::shared_ptr<rclcpp::SerializedMessage> message) {
                if (delivered->exchange(true))
                    return;
                promise->set_value(decodedMessageResult(*message, topic, type, payload_format, bridge, temp.get()));
            });

        const auto status = future.wait_for(std::chrono::milliseconds(request.timeout_ms));
        if (status != std::future_status::ready)
            return Result::fail("topic once timeout");

        auto r = future.get();
        if (r && !request.target_node.empty()) {
            ve::n(request.target_node)->copy(temp.get(), Node::COPY_STRICT);
            temp->set("target_node", Var(request.target_node));
        }
        if (out)
            out->copy(temp.get());
        return r;
#endif
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
#ifdef VE_ROS_HAS_GENERIC_PUBSUB
            item["server_count"] = Var(static_cast<int64_t>(node_->count_services(name)));
            item["client_count"] = Var(static_cast<int64_t>(node_->count_clients(name)));
#else
            item["server_count"] = Var(static_cast<int64_t>(0));
            item["client_count"] = Var(static_cast<int64_t>(0));
#endif

            Var::ListV type_list;
            for (const auto& type : types)
                type_list.push_back(Var(type));
            item["types"] = Var(std::move(type_list));
            items.push_back(Var(std::move(item)));
        }
        return items;
    }

    Result serviceInfo(const std::string& service, Node* out) const override
    {
        if (!out) return Result::fail("no output node");

        std::lock_guard<std::mutex> lock(mu_);
        if (!node_) return Result::fail("rclcpp backend is not started");

        const auto services = node_->get_service_names_and_types();
        auto it = services.find(service);
        if (it == services.end()) return Result::fail("service not found");

        out->set("service", Var(service));

        Var::ListV type_list;
        for (const auto& type : it->second)
            type_list.push_back(Var(type));
        schema::VarS::toNode(out->at("types"), Var(std::move(type_list)));

#ifdef VE_ROS_HAS_GENERIC_PUBSUB
        out->set("server_count", Var(static_cast<int64_t>(node_->count_services(service))));
        out->set("client_count", Var(static_cast<int64_t>(node_->count_clients(service))));
#else
        out->set("server_count", Var(static_cast<int64_t>(0)));
        out->set("client_count", Var(static_cast<int64_t>(0)));
#endif
        return Result::ok();
    }

    Result callService(const ServiceCallRequest& request, Node* out) override
    {
        std::shared_ptr<rclcpp::Node> node;
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (!node_) return Result::fail("rclcpp backend is not started");
            node = node_;
        }

        const std::string& service = request.service;
        const std::string& type = request.type;
        if (service.empty()) return Result::fail("service name is required");
        if (type.empty()) return Result::fail("service type is required");

        const std::string fmt = normalizedPayloadFormat(request.payload_format);
#ifndef VE_ROS_HAS_DYNAMIC_TYPESUPPORT
        return Result::fail("dynamic typesupport not available on Foxy, service calls require Galactic+");
#else
        if (fmt == "cdr_hex")
            return Result::fail("cdr_hex payload format is not supported for service calls; use yaml or var");
        if (fmt != "yaml" && fmt != "var")
            return Result::fail("unsupported payload format: " + fmt);

        auto bridge = std::make_shared<ve::ros::rclcpp_backend::DynamicTypesupportBridge>();
        std::string bridge_error;
        if (!bridge->initializeService(type, bridge_error))
            return Result::fail("failed to initialize dynamic bridge: " + bridge_error);

        const Var request_var = ve::ros::yaml::decode(request.request);

        auto request_msg = bridge->requestFromVar(request_var, bridge_error);
        if (!request_msg)
            return Result::fail("failed to build request: " + bridge_error);

        auto client = node->create_generic_client(service, type);
        if (!client->wait_for_service(std::chrono::milliseconds(request.timeout_wait_ms)))
            return Result::fail("service not available: " + service);

        auto future_and_id = client->async_send_request(request_msg.get());
        if (future_and_id.future.wait_for(std::chrono::milliseconds(request.timeout_response_ms)) != std::future_status::ready)
            return Result::fail("service call timeout");

        auto response_shared = future_and_id.future.get();
        Var response_var;
        if (!bridge->responseToVar(response_shared.get(), response_var, bridge_error))
            return Result::fail("failed to deserialize response: " + bridge_error);

        if (out) {
            out->set("service", Var(service));
            out->set("type", Var(type));
            out->set("payload_format", Var(fmt));
            schema::VarS::toNode(out->at("response"), response_var);
            if (fmt == "yaml")
                out->set("yaml", Var(ve::ros::yaml::encode(response_var)));
        }
        return Result::ok();
#endif
    }

    Result listParams(const std::string& node_name, Node* out) const override
    {
        if (!out) return Result::fail("no output node");

        std::shared_ptr<rclcpp::Node> node;
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (!node_) return Result::fail("rclcpp backend is not started");
            node = node_;
        }

        if (!node_name.empty()) {
            const std::string nn = normalizeRemoteNodeName(node_name);
            auto client = paramClient(node, nn);
            if (!client) return Result::fail("parameter service not ready");

            auto list_future = client->list_parameters({}, 0);
            if (list_future.wait_for(std::chrono::milliseconds(1000)) != std::future_status::ready)
                return Result::fail("param list timeout");

            Var::ListV param_names;
            std::vector<std::string> names_to_get;
            for (const auto& n : list_future.get().names) {
                if (!isInternalParamName(n)) {
                    param_names.push_back(Var(n));
                    names_to_get.push_back(n);
                }
            }

            out->set("node", Var(nn));
            schema::VarS::toNode(out->at("params"), Var(std::move(param_names)));

            if (!names_to_get.empty()) {
                auto get_future = client->get_parameters(names_to_get);
                if (get_future.wait_for(std::chrono::milliseconds(2000)) == std::future_status::ready) {
                    const auto values = get_future.get();
                    auto* vals = out->at("values");
                    for (std::size_t i = 0; i < values.size() && i < names_to_get.size(); ++i)
                        vals->set(names_to_get[i], parameterValueToVar(values[i].get_parameter_value()));
                }
            }
            return Result::ok();
        }

        Var::ListV nodes_list;
        for (const auto& [name, ns] : node->get_node_graph_interface()->get_node_names_and_namespaces()) {
            if (isRuntimeHelperNode(name, ns))
                continue;
            nodes_list.push_back(Var(fqNodeName(name, ns)));
        }
        schema::VarS::toNode(out->at("nodes"), Var(std::move(nodes_list)));
        return Result::ok();
    }

    Result getParam(const std::string& node_name, const std::string& name, Node* out) const override
    {
        if (!out) return Result::fail("no output node");

        std::shared_ptr<rclcpp::Node> node;
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (!node_) return Result::fail("rclcpp backend is not started");
            node = node_;
        }

        const std::string nn = normalizeRemoteNodeName(node_name);
        if (nn.empty() || name.empty()) return Result::fail("node/name is required");

        auto client = paramClient(node, nn);
        if (!client) return Result::fail("parameter service not ready");

        auto future = client->get_parameters({name});
        if (future.wait_for(std::chrono::milliseconds(1000)) != std::future_status::ready)
            return Result::fail("param get timeout");

        const auto params = future.get();
        if (params.empty()) return Result::fail("parameter not found");

        out->set("node", Var(nn));
        out->set("name", Var(name));
        out->set("value", parameterValueToVar(params.front().get_parameter_value()));
        return Result::ok();
    }

    Result setParam(const std::string& node_name, const std::string& name, const Var& value, Node* out) const override
    {
        std::shared_ptr<rclcpp::Node> node;
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (!node_) return Result::fail("rclcpp backend is not started");
            node = node_;
        }

        const std::string nn = normalizeRemoteNodeName(node_name);
        if (nn.empty() || name.empty()) return Result::fail("node/name is required");

        rclcpp::ParameterValue parameter_value;
        std::string error;
        if (!varToParameterValue(value, parameter_value, error))
            return Result::fail(error);

        auto client = paramClient(node, nn);
        if (!client) return Result::fail("parameter service not ready");

        auto future = client->set_parameters({rclcpp::Parameter(name, parameter_value)});
        if (future.wait_for(std::chrono::milliseconds(1000)) != std::future_status::ready)
            return Result::fail("param set timeout");

        const auto results = future.get();
        if (results.empty() || !results.front().successful)
            return Result::fail(results.empty() ? "parameter set failed" : results.front().reason);

        if (out) {
            out->set("node", Var(nn));
            out->set("name", Var(name));
            out->set("value", value);
        }
        return Result::ok();
    }

private:
    struct SubscriptionInfo {
        TopicSubscriptionConfig config;
        std::string target_node;
#ifdef VE_ROS_HAS_GENERIC_PUBSUB
        rclcpp::GenericSubscription::SharedPtr subscription;
#endif
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
#ifdef VE_ROS_HAS_GENERIC_PUBSUB
    Dict<rclcpp::GenericPublisher::SharedPtr> publishers_;
#ifdef VE_ROS_HAS_DYNAMIC_TYPESUPPORT
    Dict<std::shared_ptr<ve::ros::rclcpp_backend::DynamicTypesupportBridge>>
        publisher_bridges_;
#endif
#endif
    mutable std::unordered_map<std::string, std::shared_ptr<rclcpp::AsyncParametersClient>> param_clients_;
};

const bool registered = []() {
    registerBackend(std::make_shared<RclcppBackend>());
    return true;
}();

} // namespace

} // namespace ve::ros
