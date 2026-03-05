#include "ve_fastdds/ros2fastdds_server.h"

#include "ve/core/data.h"
#include "ve/core/log.h"

using namespace eprosima::fastdds::dds;

namespace hemera::service {

class PrivateRos2FastDDSServiceListener : public DataReaderListener
{
public:
    int matched = 0;
    DataReader* reader = nullptr;
    DataWriter* writer = nullptr;
    Ros2FastDDSServer::ListenerF func = NULL;

public:
    void on_subscription_matched(DataReader* reader, const SubscriptionMatchedStatus& info) override;
    void on_data_available(DataReader* reader) override;
};

void PrivateRos2FastDDSServiceListener::on_subscription_matched(DataReader* reader, const SubscriptionMatchedStatus& info)
{
    if (info.current_count_change == 1) {
        matched = info.total_count;
        std::cout << "Subscriber matched." << std::endl;
    } else if (info.current_count_change == -1) {
        matched = info.total_count;
        std::cout << "Subscriber unmatched." << std::endl;
    } else {
        std::cout << info.current_count_change <<
                  " is not a valid value for SubscriptionMatchedStatus current count change" << std::endl;
    }
}

void PrivateRos2FastDDSServiceListener::on_data_available(DataReader* r)
{
    Result res = func(reader, writer);
    //todo res
}

struct Ros2FastDDSServer::Private
{
    DomainParticipant* participant = nullptr;
    Publisher* publisher = nullptr;
    Subscriber* subscriber = nullptr;

    ve::Dict<DataReader*> readers;
    ve::Dict<DataWriter*> writers;

    DataReaderQos reader_qos;
    DataWriterQos writer_qos;
};

Ros2FastDDSServer::Ros2FastDDSServer(int domain_id) : _p(new Private)
{
    ve::log::is("<ve::fastdds>", "create participant", domain_id);

    _p->reader_qos.endpoint().history_memory_policy = eprosima::fastrtps::rtps::DYNAMIC_RESERVE_MEMORY_MODE;
    _p->reader_qos.reliability().kind = RELIABLE_RELIABILITY_QOS;
    _p->reader_qos.history().kind = KEEP_LAST_HISTORY_QOS;
    _p->reader_qos.history().depth = 1;

    _p->writer_qos.reliability().kind = RELIABLE_RELIABILITY_QOS;
    _p->writer_qos.history().kind = KEEP_LAST_HISTORY_QOS;
    _p->writer_qos.history().depth = 1;

    _p->participant = DomainParticipantFactory::get_instance()->create_participant(domain_id, PARTICIPANT_QOS_DEFAULT);
    _p->publisher = _p->participant->create_publisher(PUBLISHER_QOS_DEFAULT);
    _p->subscriber = _p->participant->create_subscriber(SUBSCRIBER_QOS_DEFAULT);
    if (!_p->participant || !_p->publisher) {
        // todo
    }
}

Ros2FastDDSServer::~Ros2FastDDSServer()
{
    ve::log::is("<ve::fastdds>", "delete participant", _p->participant->get_domain_id());

    if (_p->participant) {
        _p->participant->delete_contained_entities();
        DomainParticipantFactory::get_instance()->delete_participant(_p->participant);
    }

    for (const auto& kv : _p->readers) {
        delete kv.second;
    }
    for (const auto& kv : _p->writers) {
        delete kv.second;
    }
}

int Ros2FastDDSServer::error(const std::string& what, int code)
{
    if (code == -1) {
        ve::log::es("<ve::fastdds>", "ERROR", what);
    } else {
        ve::log::es("<ve::fastdds>", "ERROR", what, "code [", code, "]");
    }
    return code;
}

void Ros2FastDDSServer::createTopicGroup(TopicGroupInfo&& info)
{
    DataReader* r = nullptr;
    DataWriter* w = nullptr;
    PrivateRos2FastDDSServiceListener* l = nullptr;

    if (!info.reader.ts.empty()) {
        auto rc = _p->participant->register_type(info.reader.ts);
        auto t = _p->participant->create_topic(info.reader.name, info.reader.ts.get_type_name(), TOPIC_QOS_DEFAULT);
        if (info.func == NULL) {
            r = _p->subscriber->create_datareader(t, _p->reader_qos);
        } else {
            l = new PrivateRos2FastDDSServiceListener;
            l->func = info.func;
            r = _p->subscriber->create_datareader(t, _p->reader_qos, l);
        }
    }

    if (!info.writer.ts.empty()) {
        auto rc = _p->participant->register_type(info.writer.ts);
        auto t = _p->participant->create_topic(info.writer.name, info.writer.ts.get_type_name(), TOPIC_QOS_DEFAULT);
        w = _p->publisher->create_datawriter(t, _p->writer_qos, nullptr);
    }

    if (l) {
        l->reader = r;
        l->writer = w;
    }

    if (r) _p->readers.insertOne(info.name, r);
    if (w) _p->writers.insertOne(info.name, w);

    ve::log::is("<ve::fastdds>", "create topic group", info.name, "successfully");
}

DataWriter* Ros2FastDDSServer::messageWriter(const std::string& name) const
{
    if (auto w = _p->writers.value(name)) {
        return w;
    }
    return nullptr;
}

ve::UnorderedHashMap<int, Ros2FastDDSServer*>* g_ros2_fastdss_server_map = nullptr;

Ros2FastDDSServer* globalRos2FastDDSServer(int domain_id)
{
    static ve::UnorderedHashMap<int, Ros2FastDDSServer*> i;
    if (auto s = i.value(domain_id)) return s;

    g_ros2_fastdss_server_map = &i;
    auto s = new Ros2FastDDSServer(domain_id);
    i.insertOne(domain_id, s);
    return s;
}

void closeGlobalRos2FastDDSServer(int domain_id)
{
    if (g_ros2_fastdss_server_map) {
        if (auto s = g_ros2_fastdss_server_map->value(domain_id)) {
            g_ros2_fastdss_server_map->erase(domain_id);
            delete s;
        }
    }
}

}
