#include "ve/ros/topic.h"

#include "ve/ros/runtime.h"

namespace ve::ros {

Var::ListV listTopics(const std::string& filter)
{
    if (auto current = activeBackend())
        return current->listTopics(filter);
    return {};
}

Var::DictV topicInfo(const std::string& topic_name)
{
    if (auto current = activeBackend())
        return current->topicInfo(topic_name);

    Var::DictV result;
    result["ok"] = Var(false);
    result["message"] = Var("no active ROS backend");
    result["topic"] = Var(topic_name);
    return result;
}

Var::DictV subscribeTopic(const TopicSubscriptionConfig& config)
{
    if (auto current = activeBackend())
        return current->subscribeTopic(config);

    Var::DictV result;
    result["ok"] = Var(false);
    result["message"] = Var("no active ROS backend");
    result["name"] = Var(config.name);
    result["topic"] = Var(config.topic);
    return result;
}

Var::DictV unsubscribeTopic(const std::string& name)
{
    if (auto current = activeBackend())
        return current->unsubscribeTopic(name);

    Var::DictV result;
    result["ok"] = Var(false);
    result["message"] = Var("no active ROS backend");
    result["name"] = Var(name);
    return result;
}

Var::DictV publishTopic(const TopicPublishRequest& request)
{
    if (auto current = activeBackend())
        return current->publishTopic(request);

    Var::DictV result;
    result["ok"] = Var(false);
    result["message"] = Var("no active ROS backend");
    result["topic"] = Var(request.topic);
    return result;
}

} // namespace ve::ros
