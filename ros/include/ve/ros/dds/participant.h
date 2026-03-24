// participant.h — ve::dds::Participant
//
// Wraps a FastDDS DomainParticipant with Publisher + Subscriber.
// Provides typed (IDL) and untyped topic/service creation.
//
// Usage:
//   auto& p = ve::dds::Participant::instance();
//   p.createPublisher<MyMsgPubSubType>("rt/my_topic");
//   p.publish("rt/my_topic", msg);

#pragma once

#include "ve/global.h"
#include "ve/core/base.h"
#include "ve/core/log.h"

#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/publisher/DataWriterListener.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/DataReaderListener.hpp>
#include <fastdds/dds/subscriber/SampleInfo.hpp>
#include <fastdds/dds/subscriber/qos/DataReaderQos.hpp>
#include <fastdds/dds/topic/Topic.hpp>

namespace ve::dds {

namespace fdds = eprosima::fastdds::dds;

// ============================================================================
// Participant — DDS DomainParticipant lifecycle + topic management
// ============================================================================

class VE_API Participant
{
    VE_DECLARE_PRIVATE

public:
    using TypeSupportT = fdds::TypeSupport;
    using DataReaderT  = fdds::DataReader;
    using DataWriterT  = fdds::DataWriter;
    using ListenerF    = std::function<int(DataReaderT*, DataWriterT*)>;

    explicit Participant(int domain_id = 0);
    ~Participant();

    int domainId() const;
    fdds::DomainParticipant* raw() const;
    fdds::Publisher*  publisher() const;
    fdds::Subscriber* subscriber() const;

    // --- Low-level topic group (reader + writer + optional listener) ---

    struct TopicGroupInfo {
        std::string name;
        struct { std::string name; TypeSupportT ts; } reader;
        struct { std::string name; TypeSupportT ts; } writer;
        ListenerF func;
    };

    void createTopicGroup(TopicGroupInfo&& info);
    DataWriterT* writer(const std::string& name) const;
    DataReaderT* reader(const std::string& name) const;

    // --- IDL typed helpers (template, header-only) ---

    template<typename ReqPST, typename RepPST, typename F>
    void createService(const std::string& srv_name, F srv_func);

    template<typename PST>
    void createPublisher(const std::string& topic_name);

    template<typename PST, typename F>
    void createSubscriber(const std::string& topic_name, F msg_func);

    template<typename T>
    bool publish(const std::string& topic_name, T& data);

    // --- Singleton per domain_id ---
    static Participant& instance(int domain_id = 0);
    static void destroy(int domain_id = 0);

private:
    int error(const std::string& what, int code = -1);
};

// ============================================================================
// Template implementations
// ============================================================================

template<typename ReqPST, typename RepPST, typename F>
void Participant::createService(const std::string& srv_name, F srv_func)
{
    using RqT = basic::_t_remove_rc<typename basic::FnTraits<F>::ArgsT::FirstT>;
    using RrT = basic::_t_remove_rc<typename basic::FnTraits<F>::ArgsT::SecondT>;

    createTopicGroup(TopicGroupInfo{
        srv_name,
        {"rq/" + srv_name + "Request", TypeSupportT(new ReqPST)},
        {"rr/" + srv_name + "Reply",   TypeSupportT(new RepPST)},
        [this, srv_func](DataReaderT* r, DataWriterT* w) -> int {
            fdds::SampleInfo si;
            RqT rq;
            while (r->get_unread_count() > 0) {
                auto rc = r->take_next_sample(&rq, &si);
                if (rc != eprosima::fastrtps::types::ReturnCode_t::RETCODE_OK)
                    return error("service read failed", rc());
                if (si.instance_state != fdds::ALIVE_INSTANCE_STATE)
                    return error("service state error", si.instance_state);
                RrT rr;
                int res = srv_func(rq, rr);
                if (res != 0) error("service handler returned error", res);
                eprosima::fastrtps::rtps::WriteParams wp;
                wp.related_sample_identity().writer_guid(si.sample_identity.writer_guid());
                wp.related_sample_identity().sequence_number(si.sample_identity.sequence_number());
                if (!w->write(reinterpret_cast<void*>(&rr), wp))
                    return error("service reply write failed");
            }
            return 0;
        }
    });
}

template<typename PST>
void Participant::createPublisher(const std::string& topic_name)
{
    createTopicGroup(TopicGroupInfo{
        topic_name,
        {"", TypeSupportT()},
        {"rt/" + topic_name, TypeSupportT(new PST)},
        nullptr
    });
}

template<typename PST, typename F>
void Participant::createSubscriber(const std::string& topic_name, F msg_func)
{
    using MsgT = basic::_t_remove_rc<typename basic::FnTraits<F>::ArgsT::FirstT>;

    createTopicGroup(TopicGroupInfo{
        topic_name,
        {"rt/" + topic_name, TypeSupportT(new PST)},
        {"", TypeSupportT()},
        [this, msg_func](DataReaderT* r, DataWriterT*) -> int {
            fdds::SampleInfo si;
            MsgT msg;
            while (r->get_unread_count() > 0) {
                auto rc = r->take_next_sample(&msg, &si);
                if (rc != eprosima::fastrtps::types::ReturnCode_t::RETCODE_OK)
                    return error("subscriber read failed", rc());
                if (si.instance_state != fdds::ALIVE_INSTANCE_STATE)
                    return error("subscriber state error", si.instance_state);
                int res = msg_func(msg);
                if (res != 0) error("subscriber handler returned error", res);
            }
            return 0;
        }
    });
}

template<typename T>
bool Participant::publish(const std::string& topic_name, T& data)
{
    auto* w = writer(topic_name);
    if (!w) { error("publish: no writer for " + topic_name); return false; }
    return w->write(reinterpret_cast<void*>(&data));
}

} // namespace ve::dds
