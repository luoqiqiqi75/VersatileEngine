#include "ve/ros/topic.h"

#include "ve/ros/runtime.h"

namespace ve::ros {

Var::ListV listTopics(const std::string& filter)
{
    if (auto current = activeBackend())
        return current->listTopics(filter);
    return {};
}

Result topicInfo(const std::string& topic_name, Node* out)
{
    auto current = activeBackend();
    if (!current) return Result::fail("no active ROS backend");
    return current->topicInfo(topic_name, out);
}

Result subscribeTopic(const TopicSubscriptionConfig& config, Node* out)
{
    auto current = activeBackend();
    if (!current) return Result::fail("no active ROS backend");
    return current->subscribeTopic(config, out);
}

Result unsubscribeTopic(const std::string& name, Node* out)
{
    auto current = activeBackend();
    if (!current) return Result::fail("no active ROS backend");
    return current->unsubscribeTopic(name, out);
}

Result publishTopic(const TopicPublishRequest& request, Node* out)
{
    auto current = activeBackend();
    if (!current) return Result::fail("no active ROS backend");
    return current->publishTopic(request, out);
}

Result onceTopic(const TopicOnceRequest& request, Node* out)
{
    auto current = activeBackend();
    if (!current) return Result::fail("no active ROS backend");
    return current->onceTopic(request, out);
}

} // namespace ve::ros
