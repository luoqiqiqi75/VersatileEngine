///////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2023
//////////////////////////////////////////////////////////////////////////////

#pragma once

#include "fastdds/dds/domain/DomainParticipant.hpp"
#include "fastdds/dds/domain/DomainParticipantFactory.hpp"
#include "fastdds/dds/publisher/Publisher.hpp"
#include "fastdds/dds/publisher/DataWriter.hpp"
#include "fastdds/dds/publisher/DataWriterListener.hpp"
#include "fastdds/dds/subscriber/Subscriber.hpp"
#include "fastdds/dds/subscriber/SampleInfo.hpp"
#include "fastdds/dds/subscriber/DataReader.hpp"
#include "fastdds/dds/subscriber/DataReaderListener.hpp"
#include "fastdds/dds/subscriber/qos/DataReaderQos.hpp"
#include "fastdds/dds/topic/Topic.hpp"

#include "ve/core/base.h"

namespace eprosima::fastdds::dds {
class TypeSupport;
class DataReader;
class DataWriter;
}

namespace hemera::service {

using Result = int;

class Ros2FastDDSServer
{
    VE_DECLARE_PRIVATE

public:
    using TypeSupportT = eprosima::fastdds::dds::TypeSupport;
    using DataReaderT = eprosima::fastdds::dds::DataReader;
    using DataWriterT = eprosima::fastdds::dds::DataWriter;
    using ListenerF = std::function<Result(DataReaderT*, DataWriterT*)>;

    struct TopicInfo
    {
        std::string name;
        TypeSupportT ts;
    };

    struct TopicGroupInfo
    {
        std::string name;
        TopicInfo reader;
        TopicInfo writer;
        ListenerF func = NULL;
    };

public:
    Ros2FastDDSServer(int domain_id = 0);
    ~Ros2FastDDSServer();

protected:
    int error(const std::string& what, int code = -1);

public:
    void createTopicGroup(TopicGroupInfo&& info);

    DataWriterT* messageWriter(const std::string& name) const;

public:
    template<typename RequestPubSubType, typename ReplyPubSubType, typename ServiceFunction>
    void createServiceByIDL(const std::string& srv_name, ServiceFunction srv_func)
    {
        using RqT = ve::basic::_t_remove_rc<typename ve::basic::FInfo<ServiceFunction>::ArgsT::FirstT>;
        using RrT = ve::basic::_t_remove_rc<typename ve::basic::FInfo<ServiceFunction>::ArgsT::SecondT>;

        createTopicGroup(TopicGroupInfo {
            srv_name,
            TopicInfo {"rq/" + srv_name + "Request", TypeSupportT(new RequestPubSubType)},
            TopicInfo {"rr/" + srv_name + "Reply", TypeSupportT(new ReplyPubSubType)},
            [=] (DataReaderT* r, DataWriterT* w) -> Result {
                using namespace eprosima::fastdds::dds;
                SampleInfo si;
                RqT rq;
                while (r->get_unread_count() > 0) {
                    auto rc = r->take_next_sample(&rq, &si);
                    if (rc != eprosima::fastrtps::types::ReturnCode_t::RETCODE_OK) return error("service read failed", rc());
                    if (si.instance_state != InstanceStateKind::ALIVE_INSTANCE_STATE) return error("service state error", si.instance_state);
                    RrT rr;
                    Result res = srv_func(rq, rr);
                    if (res != 0) {
                        error("service handle failed", res);
                    }
                    eprosima::fastrtps::rtps::WriteParams wp;
                    wp.related_sample_identity().writer_guid(si.sample_identity.writer_guid());
                    wp.related_sample_identity().sequence_number(si.sample_identity.sequence_number());
                    auto wc = w->write(reinterpret_cast<void *>(&rr), wp);
                    if (!wc) return error("service reply failed");
                }
                return 0;
            }
        });
    }

    template<typename NotifyPubSubType>
    void createNotifyByIDL(const std::string& msg_name)
    {
        createTopicGroup(TopicGroupInfo {
            msg_name,
            TopicInfo {"", TypeSupportT()},
            TopicInfo {"rt/" + msg_name, TypeSupportT(new NotifyPubSubType)},
            NULL
        });
    }

    template<typename SubscribePubSubType, typename SubscribeFunction>
    void createSubscribeByIDL(const std::string& msg_name, SubscribeFunction&& msg_func)
    {
        using MsgT = ve::basic::_t_remove_rc<typename ve::basic::FInfo<SubscribeFunction>::ArgsT::FirstT>;

        createTopicGroup(TopicGroupInfo {
            msg_name,
            TopicInfo {"rt/" + msg_name, TypeSupportT(new SubscribePubSubType)},
            TopicInfo {"", TypeSupportT()},
            [=] (DataReaderT* r, DataWriterT*) -> Result {
                using namespace eprosima::fastdds::dds;
                SampleInfo si;
                MsgT rq;
                while (r->get_unread_count() > 0) {
                    auto rc = r->take_next_sample(&rq, &si);
                    if (rc != eprosima::fastrtps::types::ReturnCode_t::RETCODE_OK) return error("service read failed", rc());
                    if (si.instance_state != InstanceStateKind::ALIVE_INSTANCE_STATE) return error("service state error", si.instance_state);
                    Result res = msg_func(rq);
                    if (res != 0) {
                        error("service handle failed", res);
                    }
                }
                return 0;
            }
        });
    }

    template<typename NotifyT>
    Result writeMessage(const std::string& message_name, NotifyT& message_data)
    {
        auto w = messageWriter(message_name);
        if (!w) return error("message no created writer");

        auto wc = w->write(reinterpret_cast<void *>(&message_data));
        if (!wc) return error("message notify failed");
        return 0;
    }
};

Ros2FastDDSServer* globalRos2FastDDSServer(int domain_id = 0);
void closeGlobalRos2FastDDSServer(int domain_id = 0);

}
