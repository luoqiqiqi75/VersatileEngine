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

namespace ve::ros::fastdds {

namespace fdds = eprosima::fastdds::dds;

class VE_API Participant
{
    VE_DECLARE_PRIVATE

public:
    using TypeSupportT = fdds::TypeSupport;
    using DataReaderT = fdds::DataReader;
    using DataWriterT = fdds::DataWriter;
    using ListenerF = std::function<int(DataReaderT*, DataWriterT*)>;

    explicit Participant(int domain_id = 0);
    ~Participant();

    int domainId() const;
    fdds::DomainParticipant* raw() const;
    fdds::Publisher* publisher() const;
    fdds::Subscriber* subscriber() const;

    struct TopicGroupInfo {
        std::string name;
        struct { std::string name; TypeSupportT ts; } reader;
        struct { std::string name; TypeSupportT ts; } writer;
        ListenerF func;
    };

    void createTopicGroup(TopicGroupInfo&& info);
    DataWriterT* writer(const std::string& name) const;
    DataReaderT* reader(const std::string& name) const;

private:
    int error(const std::string& what, int code = -1);
};

} // namespace ve::ros::fastdds
