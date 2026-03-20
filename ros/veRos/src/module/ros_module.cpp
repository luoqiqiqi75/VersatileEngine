// ros_module.cpp  - ve::RosModule (ve.ros)
//
// DDS adapter entry point:
//   constructor: load default config, create Participant
//   init():      scan DDS domain for discovered participants
//   ready():     read final config, start CommandService
//   deinit():    stop CommandService, destroy Participant

#include "ve/core/module.h"
#include "ve/core/log.h"
#include "ve/ros/dds/participant.h"
#include "ve/ros/service/command_service.h"

#include <fastdds/dds/domain/DomainParticipant.hpp>

namespace ve {

class RosModule : public Module
{
    dds::Participant* participant_ = nullptr;
    std::unique_ptr<dds::CommandService> cmdService_;

public:
    explicit RosModule(const std::string& name) : Module(name)
    {
        node()->ensure("config/domain_id")->set(Var(0));
        node()->ensure("config/service_prefix")->set(Var("ve"));
        node()->ensure("config/command_service")->set(Var(true));

        int domain_id = node()->resolve("config/domain_id")->get<int>(0);

        participant_ = &dds::Participant::instance(domain_id);

        n("ve/ros/domain_id")->set(Var(domain_id));
        n("ve/ros/state")->set(Var("created"));

        veLogI << "[ve.ros] Participant created, domain_id=" << domain_id;
    }

protected:
    void init() override
    {
        scanDomain();
        n("ve/ros/state")->set(Var("init"));
    }

    void ready() override
    {
        bool auto_service = node()->resolve("config/command_service")->get<bool>(true);
        std::string prefix = node()->resolve("config/service_prefix")->get<std::string>("ve");

        if (auto_service && participant_) {
            cmdService_ = std::make_unique<dds::CommandService>(*participant_, prefix);
            cmdService_->start();
            veLogI << "[ve.ros] CommandService started (" << prefix << "/command, "
                   << prefix << "/data_*)";
        }

        n("ve/ros/state")->set(Var("ready"));
        veLogI << "[ve.ros] ready";
    }

    void deinit() override
    {
        if (cmdService_) {
            cmdService_->stop();
            cmdService_.reset();
            veLogI << "[ve.ros] CommandService stopped";
        }

        if (participant_) {
            int did = participant_->domainId();
            dds::Participant::destroy(did);
            participant_ = nullptr;
            veLogI << "[ve.ros] Participant destroyed (domain " << did << ")";
        }

        n("ve/ros/state")->set(Var("stopped"));
    }

private:
    void scanDomain()
    {
        if (!participant_ || !participant_->raw()) return;

        namespace fdds = eprosima::fastdds::dds;
        auto* dp = participant_->raw();

        std::vector<eprosima::fastrtps::rtps::InstanceHandle_t> handles;
        dp->get_discovered_participants(handles);

        auto* scanNode = n("ve/ros/scan");
        scanNode->clear();

        veLogI << "[ve.ros] DDS domain scan: " << handles.size()
               << " remote participant(s)";

        int idx = 0;
        for (auto& h : handles) {
            fdds::builtin::ParticipantBuiltinTopicData pd;
            if (dp->get_discovered_participant_data(pd, h) ==
                eprosima::fastrtps::types::ReturnCode_t::RETCODE_OK)
            {
                std::string pname(pd.participant_name.to_string());
                if (pname.empty()) pname = "(anonymous)";

                auto* pn = scanNode->append(std::to_string(idx));
                pn->set("name", Var(pname));

                veLogI << "[ve.ros]   [" << idx << "] " << pname;
            }
            ++idx;
        }

        n("ve/ros/scan_count")->set(Var(static_cast<int>(handles.size())));

        if (participant_->publisher())
            veLogI << "[ve.ros] local publisher active";
        if (participant_->subscriber())
            veLogI << "[ve.ros] local subscriber active";
    }
};

} // namespace ve

VE_REGISTER_PRIORITY_MODULE(ve.ros, ve::RosModule, 40, 1)
