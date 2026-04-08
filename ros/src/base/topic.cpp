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

} // namespace ve::ros
