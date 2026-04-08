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
    Var::DictV topicInfo(const std::string& topic) const override
    {
        Var::DictV dict;
        dict["ok"] = Var(false);
        dict["message"] = Var("Fast DDS topic discovery is not implemented in v1");
        dict["topic"] = Var(topic);
        return dict;
    }
    Var::DictV subscribeTopic(const TopicSubscriptionConfig& config) override
    {
        Var::DictV dict;
        dict["ok"] = Var(false);
        dict["message"] = Var("Fast DDS generic subscribe is not implemented in v1");
        dict["name"] = Var(config.name);
        dict["topic"] = Var(config.topic);
        return dict;
    }
    Var::DictV unsubscribeTopic(const std::string& name) override
    {
        Var::DictV dict;
        dict["ok"] = Var(false);
        dict["message"] = Var("Fast DDS generic unsubscribe is not implemented in v1");
        dict["name"] = Var(name);
        return dict;
    }
    Var::DictV publishTopic(const TopicPublishRequest& request) override
    {
        Var::DictV dict;
        dict["ok"] = Var(false);
        dict["message"] = Var("Fast DDS generic publish is not implemented in v1");
        dict["topic"] = Var(request.topic);
        return dict;
    }

    Var::ListV listServices(const std::string&) const override { return {}; }
    Var::DictV serviceInfo(const std::string& service) const override
    {
        Var::DictV dict;
        dict["ok"] = Var(false);
        dict["message"] = Var("Fast DDS service discovery is not implemented in v1");
        dict["service"] = Var(service);
        return dict;
    }

    Var::DictV listParams(const std::string&) const override
    {
        Var::DictV dict;
        dict["ok"] = Var(false);
        dict["message"] = Var("Fast DDS parameter APIs are not implemented in v1");
        return dict;
    }

    Var::DictV getParam(const std::string&, const std::string&) const override
    {
        Var::DictV dict;
        dict["ok"] = Var(false);
        dict["message"] = Var("Fast DDS parameter APIs are not implemented in v1");
        return dict;
    }

    Var::DictV setParam(const std::string&, const std::string&, const Var&) const override
    {
        Var::DictV dict;
        dict["ok"] = Var(false);
        dict["message"] = Var("Fast DDS parameter APIs are not implemented in v1");
        return dict;
    }

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
