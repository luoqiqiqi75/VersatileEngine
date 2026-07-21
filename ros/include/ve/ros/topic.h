#pragma once

#include "ve/core/command.h"
#include "ve/core/var.h"

namespace ve::ros {

struct QosProfile {
    std::string reliability = "reliable";     // "reliable" | "best_effort"
    std::string durability  = "volatile";     // "volatile" | "transient_local"
    std::string history     = "keep_last";    // "keep_last" | "keep_all"
    int depth = 10;
};

struct TopicSubscriptionConfig {
    std::string name;
    std::string topic;
    std::string type;
    std::string target_node;
    std::string payload_format = "yaml";
    QosProfile qos;
};

struct TopicPublishRequest {
    std::string topic;
    std::string type;
    std::string payload;
    Var value;
    std::string payload_format = "cdr_hex";
    QosProfile qos;
};

struct TopicOnceRequest {
    std::string topic;
    std::string type;
    std::string target_node;
    std::string payload_format = "yaml";
    int timeout_ms = 3000;
    QosProfile qos;
};

VE_API Var::ListV listTopics(const std::string& filter = "");
VE_API Result topicInfo(const std::string& topic_name, Node* out = nullptr);
VE_API Result subscribeTopic(const TopicSubscriptionConfig& config, Node* out = nullptr);
VE_API Result unsubscribeTopic(const std::string& name, Node* out = nullptr);
VE_API Result publishTopic(const TopicPublishRequest& request, Node* out = nullptr);
VE_API Result onceTopic(const TopicOnceRequest& request, Node* out = nullptr);

} // namespace ve::ros
