#pragma once

#include "ve/core/var.h"

namespace ve::ros {

struct TopicSubscriptionConfig {
    std::string name;
    std::string topic;
    std::string type;
    std::string target_node;
    std::string payload_format = "cdr_hex";
};

struct TopicPublishRequest {
    std::string topic;
    std::string type;
    std::string payload;
    std::string payload_format = "cdr_hex";
};

VE_API Var::ListV listTopics(const std::string& filter = "");
VE_API Var::DictV topicInfo(const std::string& topic_name);
VE_API Var::DictV subscribeTopic(const TopicSubscriptionConfig& config);
VE_API Var::DictV unsubscribeTopic(const std::string& name);
VE_API Var::DictV publishTopic(const TopicPublishRequest& request);

} // namespace ve::ros
