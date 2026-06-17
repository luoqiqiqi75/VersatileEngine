#include "participant.h"

#include "ve/ros/backend.h"

namespace ve::ros::fastdds {

namespace {

class FastDdsBackend : public Backend
{
public:
    std::string key() const override { return "fastdds"; }
    std::string displayName() const override { return "Fast DDS"; }
    std::string transport() const override { return "native-dds"; }
    int priority() const override { return 80; }
    std::string summary() const override
    {
        return "Native Fast DDS backend with participant lifecycle.";
    }

    bool isAvailable() const override { return true; }
    bool isEnabled() const override
    {
        return !env("FASTRTPS_DEFAULT_PROFILES_FILE").empty();
    }

    Var::DictV details() const override
    {
        Var::DictV dict;
        dict["domain_id"] = Var(static_cast<int64_t>(domain_id_));
        dict["fastdds_profile"] = Var(env("FASTRTPS_DEFAULT_PROFILES_FILE"));
        return dict;
    }

    bool start(Node*, std::string&) override
    {
        if (participant_)
            return true;

        domain_id_ = std::atoi(env("ROS_DOMAIN_ID", "0").c_str());
        participant_ = std::make_unique<Participant>(domain_id_);
        return participant_ && participant_->raw();
    }

    void stop() override
    {
        participant_.reset();
    }

    bool isStarted() const override
    {
        return participant_ && participant_->raw();
    }

    Var::ListV listNodes(const std::string& filter) const override
    {
        Var::ListV items;
        if (!participant_ || !participant_->raw())
            return items;

        std::vector<eprosima::fastrtps::rtps::InstanceHandle_t> handles;
        participant_->raw()->get_discovered_participants(handles);
        auto names = participant_->raw()->get_participant_names();

        int idx = 0;
        for (const auto& handle : handles) {
            (void)handle;
            std::string name = idx < static_cast<int>(names.size()) ? names[static_cast<std::size_t>(idx)] : "";
            if (name.empty())
                name = "(anonymous)";
            if (!filter.empty() && name.find(filter) == std::string::npos)
                continue;

            Var::DictV item;
            item["index"] = Var(static_cast<int64_t>(idx));
            item["name"] = Var(name);
            item["namespace"] = Var("");
            item["full_name"] = Var(name);
            items.push_back(Var(std::move(item)));
            ++idx;
        }
        return items;
    }

    Var::ListV listTopics(const std::string&) const override { return {}; }
    Result topicInfo(const std::string&, Node*) const override { return Result::fail("Fast DDS topic discovery not implemented in v1"); }
    Result subscribeTopic(const TopicSubscriptionConfig&, Node*) override { return Result::fail("Fast DDS generic subscribe not implemented in v1"); }
    Result unsubscribeTopic(const std::string&, Node*) override { return Result::fail("Fast DDS generic unsubscribe not implemented in v1"); }
    Result publishTopic(const TopicPublishRequest&, Node*) override { return Result::fail("Fast DDS generic publish not implemented in v1"); }
    Result onceTopic(const TopicOnceRequest&, Node*) override { return Result::fail("Fast DDS generic once not implemented in v1"); }

    Var::ListV listServices(const std::string&) const override { return {}; }
    Result serviceInfo(const std::string&, Node*) const override { return Result::fail("Fast DDS service discovery not implemented in v1"); }
    Result callService(const ServiceCallRequest&, Node*) override { return Result::fail("Fast DDS service call not implemented in v1"); }

    Result listParams(const std::string&, Node*) const override { return Result::fail("Fast DDS parameter APIs not implemented in v1"); }
    Result getParam(const std::string&, const std::string&, Node*) const override { return Result::fail("Fast DDS parameter APIs not implemented in v1"); }
    Result setParam(const std::string&, const std::string&, const Var&, Node*) const override { return Result::fail("Fast DDS parameter APIs not implemented in v1"); }

private:
    int domain_id_ = 0;
    std::unique_ptr<Participant> participant_;
};

const bool registered = []() {
    registerBackend(std::make_shared<FastDdsBackend>());
    return true;
}();

} // namespace

} // namespace ve::ros::fastdds
