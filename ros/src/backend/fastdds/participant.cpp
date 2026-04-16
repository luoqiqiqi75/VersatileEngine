#include "participant.h"

namespace ve::ros::fastdds {

class ReaderListener : public fdds::DataReaderListener
{
public:
    Participant::DataReaderT* reader = nullptr;
    Participant::DataWriterT* writer = nullptr;
    Participant::ListenerF func;

    void on_subscription_matched(fdds::DataReader*,
                                 const fdds::SubscriptionMatchedStatus& info) override
    {
        if (info.current_count_change == 1)
            veLogIs("ve::ros.fastdds", "subscriber matched, total:", info.total_count);
        else if (info.current_count_change == -1)
            veLogIs("ve::ros.fastdds", "subscriber unmatched, total:", info.total_count);
    }

    void on_data_available(fdds::DataReader*) override
    {
        if (func)
            func(reader, writer);
    }
};

struct Participant::Private
{
    int domain_id = 0;
    fdds::DomainParticipant* participant = nullptr;
    fdds::Publisher* pub = nullptr;
    fdds::Subscriber* sub = nullptr;

    fdds::DataReaderQos reader_qos;
    fdds::DataWriterQos writer_qos;

    Dict<fdds::DataReader*> readers;
    Dict<fdds::DataWriter*> writers;
    Vector<ReaderListener*> listeners;
};

Participant::Participant(int domain_id)
    : _p(new Private)
{
    _p->domain_id = domain_id;

    _p->reader_qos.endpoint().history_memory_policy =
        eprosima::fastrtps::rtps::DYNAMIC_RESERVE_MEMORY_MODE;
    _p->reader_qos.reliability().kind = fdds::RELIABLE_RELIABILITY_QOS;
    _p->reader_qos.history().kind = fdds::KEEP_LAST_HISTORY_QOS;
    _p->reader_qos.history().depth = 1;

    _p->writer_qos.reliability().kind = fdds::RELIABLE_RELIABILITY_QOS;
    _p->writer_qos.history().kind = fdds::KEEP_LAST_HISTORY_QOS;
    _p->writer_qos.history().depth = 1;

    _p->participant = fdds::DomainParticipantFactory::get_instance()
        ->create_participant(domain_id, fdds::PARTICIPANT_QOS_DEFAULT);
    if (!_p->participant) {
        error("failed to create DomainParticipant");
        return;
    }

    _p->pub = _p->participant->create_publisher(fdds::PUBLISHER_QOS_DEFAULT);
    _p->sub = _p->participant->create_subscriber(fdds::SUBSCRIBER_QOS_DEFAULT);

    if (!_p->pub || !_p->sub)
        error("failed to create Publisher/Subscriber");

    veLogIs("ve::ros.fastdds", "Participant created, domain:", domain_id);
}

Participant::~Participant()
{
    if (_p->participant) {
        _p->participant->delete_contained_entities();
        fdds::DomainParticipantFactory::get_instance()->delete_participant(_p->participant);
    }

    for (auto* listener : _p->listeners)
        delete listener;

    veLogIs("ve::ros.fastdds", "Participant destroyed, domain:", _p->domain_id);
    delete _p;
}

int Participant::domainId() const { return _p->domain_id; }
fdds::DomainParticipant* Participant::raw() const { return _p->participant; }
fdds::Publisher* Participant::publisher() const { return _p->pub; }
fdds::Subscriber* Participant::subscriber() const { return _p->sub; }

void Participant::createTopicGroup(TopicGroupInfo&& info)
{
    fdds::DataReader* reader = nullptr;
    fdds::DataWriter* writer = nullptr;
    ReaderListener* listener = nullptr;

    if (!info.reader.ts.empty()) {
        _p->participant->register_type(info.reader.ts);
        auto* topic = _p->participant->create_topic(
            info.reader.name, info.reader.ts.get_type_name(), fdds::TOPIC_QOS_DEFAULT);
        if (info.func) {
            listener = new ReaderListener;
            listener->func = info.func;
            reader = _p->sub->create_datareader(topic, _p->reader_qos, listener);
        } else {
            reader = _p->sub->create_datareader(topic, _p->reader_qos);
        }
    }

    if (!info.writer.ts.empty()) {
        _p->participant->register_type(info.writer.ts);
        auto* topic = _p->participant->create_topic(
            info.writer.name, info.writer.ts.get_type_name(), fdds::TOPIC_QOS_DEFAULT);
        writer = _p->pub->create_datawriter(topic, _p->writer_qos, nullptr);
    }

    if (listener) {
        listener->reader = reader;
        listener->writer = writer;
        _p->listeners.push_back(listener);
    }

    if (reader)
        _p->readers.insertOne(info.name, reader);
    if (writer)
        _p->writers.insertOne(info.name, writer);

    veLogIs("ve::ros.fastdds", "topic group created:", info.name);
}

Participant::DataWriterT* Participant::writer(const std::string& name) const
{
    return _p->writers.value(name, nullptr);
}

Participant::DataReaderT* Participant::reader(const std::string& name) const
{
    return _p->readers.value(name, nullptr);
}

int Participant::error(const std::string& what, int code)
{
    if (code == -1)
        veLogEs("ve::ros.fastdds", "ERROR:", what);
    else
        veLogEs("ve::ros.fastdds", "ERROR:", what, "code:", code);
    return code;
}

} // namespace ve::ros::fastdds
