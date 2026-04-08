#include "command_service_fastdds.h"

#include "ve/ros/command_service.h"

#include <fastrtps/types/DynamicData.h>
#include <fastrtps/types/DynamicDataFactory.h>
#include <fastrtps/types/DynamicPubSubType.h>
#include <fastrtps/types/DynamicType.h>
#include <fastrtps/types/DynamicTypeBuilder.h>
#include <fastrtps/types/DynamicTypeBuilderFactory.h>

namespace ve::ros::fastdds {

namespace ftypes = eprosima::fastrtps::types;
namespace fdds = eprosima::fastdds::dds;

struct FastDdsCommandService::Private
{
    Participant* participant = nullptr;
    std::string prefix;
    bool running = false;

    ftypes::DynamicType_ptr cmd_req_type;
    ftypes::DynamicType_ptr cmd_rep_type;
    ftypes::DynamicData* cmd_req_data = nullptr;
    ftypes::DynamicData* cmd_rep_data = nullptr;
};

static void createStringService(
    Participant& participant,
    const std::string& service_name,
    ftypes::DynamicType_ptr req_type,
    ftypes::DynamicType_ptr rep_type,
    ftypes::DynamicData* req_data,
    ftypes::DynamicData* rep_data,
    std::function<void(const std::string&, const std::string&, const std::string&, int&, std::string&)> handler)
{
    fdds::TypeSupport req_ts(new ftypes::DynamicPubSubType(req_type));
    fdds::TypeSupport rep_ts(new ftypes::DynamicPubSubType(rep_type));

    participant.createTopicGroup(Participant::TopicGroupInfo{
        service_name,
        {"rq/" + service_name + "Request", std::move(req_ts)},
        {"rr/" + service_name + "Reply", std::move(rep_ts)},
        [req_data, rep_data, handler](Participant::DataReaderT* reader,
                                      Participant::DataWriterT* writer) -> int {
            fdds::SampleInfo sample_info;
            while (reader->get_unread_count() > 0) {
                auto rc = reader->take_next_sample(req_data, &sample_info);
                if (rc != eprosima::fastrtps::types::ReturnCode_t::RETCODE_OK)
                    continue;
                if (sample_info.instance_state != fdds::ALIVE_INSTANCE_STATE)
                    continue;

                std::string command;
                std::string path;
                std::string args;
                req_data->get_string_value(command, 0);
                req_data->get_string_value(path, 1);
                req_data->get_string_value(args, 2);

                int code = 0;
                std::string result;
                handler(command, path, args, code, result);

                rep_data->set_int32_value(code, 0);
                rep_data->set_string_value(result, 1);

                eprosima::fastrtps::rtps::WriteParams write_params;
                write_params.related_sample_identity().writer_guid(sample_info.sample_identity.writer_guid());
                write_params.related_sample_identity().sequence_number(sample_info.sample_identity.sequence_number());
                writer->write(reinterpret_cast<void*>(rep_data), write_params);
            }
            return 0;
        }
    });
}

FastDdsCommandService::FastDdsCommandService(Participant& participant, const std::string& prefix)
    : _p(new Private)
{
    _p->participant = &participant;
    _p->prefix = prefix;
}

FastDdsCommandService::~FastDdsCommandService()
{
    if (_p->cmd_req_data)
        ftypes::DynamicDataFactory::get_instance()->delete_data(_p->cmd_req_data);
    if (_p->cmd_rep_data)
        ftypes::DynamicDataFactory::get_instance()->delete_data(_p->cmd_rep_data);
    delete _p;
}

void FastDdsCommandService::start()
{
    if (_p->running)
        return;
    _p->running = true;

    auto* factory = ftypes::DynamicTypeBuilderFactory::get_instance();

    auto req_builder = factory->create_struct_builder();
    req_builder->set_name(_p->prefix + "_CommandRequest");
    req_builder->add_member(0, "command", factory->create_string_type());
    req_builder->add_member(1, "path", factory->create_string_type());
    req_builder->add_member(2, "args", factory->create_string_type());
    _p->cmd_req_type = req_builder->build();

    auto rep_builder = factory->create_struct_builder();
    rep_builder->set_name(_p->prefix + "_CommandReply");
    rep_builder->add_member(0, "code", factory->create_int32_type());
    rep_builder->add_member(1, "result", factory->create_string_type());
    _p->cmd_rep_type = rep_builder->build();

    _p->cmd_req_data = ftypes::DynamicDataFactory::get_instance()->create_data(_p->cmd_req_type);
    _p->cmd_rep_data = ftypes::DynamicDataFactory::get_instance()->create_data(_p->cmd_rep_type);

    createStringService(*_p->participant,
                        _p->prefix + "/command",
                        _p->cmd_req_type,
                        _p->cmd_rep_type,
                        _p->cmd_req_data,
                        _p->cmd_rep_data,
                        [](const std::string& command,
                           const std::string& path,
                           const std::string& args,
                           int& out_code,
                           std::string& out_result) {
                            Var::DictV input;
                            input["path"] = Var(path);
                            input["args"] = yaml::decode(args);
                            Result result = ve::command::call(command, Var(std::move(input)));
                            out_code = result.code();
                            out_result = yaml::encode(result.content());
                        });
}

void FastDdsCommandService::stop()
{
    _p->running = false;
}

bool FastDdsCommandService::isRunning() const
{
    return _p->running;
}

} // namespace ve::ros::fastdds
