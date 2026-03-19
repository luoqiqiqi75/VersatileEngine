// participant.cpp — ve::dds::Participant implementation

#include "ve/ros/dds/participant.h"

namespace ve::dds {

namespace fdds = eprosima::fastdds::dds;

// ============================================================================
// Listener for DataReader events
// ============================================================================

class ReaderListener : public fdds::DataReaderListener
{
public:
    Participant::DataReaderT*  reader = nullptr;
    Participant::DataWriterT*  writer = nullptr;
    Participant::ListenerF     func;

    void on_subscription_matched(fdds::DataReader*, const fdds::SubscriptionMatchedStatus& info) override
    {
        if (info.current_count_change == 1)
            veLogIs("ve::dds", "subscriber matched, total:", info.total_count);
        else if (info.current_count_change == -1)
            veLogIs("ve::dds", "subscriber unmatched, total:", info.total_count);
    }

    void on_data_available(fdds::DataReader*) override
    {
        if (func) func(reader, writer);
    }
};

// ============================================================================
// Private data
// ============================================================================

struct Participant::Private
{
    int domain_id = 0;
    fdds::DomainParticipant* participant = nullptr;
    fdds::Publisher*  pub = nullptr;
    fdds::Subscriber* sub = nullptr;

    fdds::DataReaderQos reader_qos;
    fdds::DataWriterQos writer_qos;

    Dict<fdds::DataReader*>  readers;
    Dict<fdds::DataWriter*>  writers;
    Vector<ReaderListener*>  listeners;
};

// ============================================================================
// Participant lifecycle
// ============================================================================

Participant::Participant(int domain_id)
    : _p(new Private)
{
    _p->domain_id = domain_id;

    _p->reader_qos.endpoint().history_memory_policy =
        eprosima::fastrtps::rtps::DYNAMIC_RESERVE_MEMORY_MODE;
    _p->reader_qos.reliability().kind = fdds::RELIABLE_RELIABILITY_QOS;
    _p->reader_qos.history().kind     = fdds::KEEP_LAST_HISTORY_QOS;
    _p->reader_qos.history().depth    = 1;

    _p->writer_qos.reliability().kind = fdds::RELIABLE_RELIABILITY_QOS;
    _p->writer_qos.history().kind     = fdds::KEEP_LAST_HISTORY_QOS;
    _p->writer_qos.history().depth    = 1;

    _p->participant = fdds::DomainParticipantFactory::get_instance()
        ->create_participant(domain_id, fdds::PARTICIPANT_QOS_DEFAULT);
    if (!_p->participant) {
        error("failed to create DomainParticipant");
        return;
    }

    _p->pub = _p->participant->create_publisher(fdds::PUBLISHER_QOS_DEFAULT);
    _p->sub = _p->participant->create_subscriber(fdds::SUBSCRIBER_QOS_DEFAULT);

    if (!_p->pub || !_p->sub) {
        error("failed to create Publisher/Subscriber");
        return;
    }

    veLogIs("ve::dds", "Participant created, domain:", domain_id);
}

Participant::~Participant()
{
    if (_p->participant) {
        _p->participant->delete_contained_entities();
        fdds::DomainParticipantFactory::get_instance()
            ->delete_participant(_p->participant);
    }

    for (auto* l : _p->listeners)
        delete l;

    veLogIs("ve::dds", "Participant destroyed, domain:", _p->domain_id);
    delete _p;
}

int Participant::domainId() const { return _p->domain_id; }

fdds::DomainParticipant* Participant::raw() const { return _p->participant; }
fdds::Publisher*  Participant::publisher()  const { return _p->pub; }
fdds::Subscriber* Participant::subscriber() const { return _p->sub; }

// ============================================================================
// Topic group
// ============================================================================

void Participant::createTopicGroup(TopicGroupInfo&& info)
{
    fdds::DataReader* r = nullptr;
    fdds::DataWriter* w = nullptr;
    ReaderListener*   l = nullptr;

    if (!info.reader.ts.empty()) {
        _p->participant->register_type(info.reader.ts);
        auto* t = _p->participant->create_topic(
            info.reader.name, info.reader.ts.get_type_name(),
            fdds::TOPIC_QOS_DEFAULT);
        if (info.func) {
            l = new ReaderListener;
            l->func = info.func;
            r = _p->sub->create_datareader(t, _p->reader_qos, l);
        } else {
            r = _p->sub->create_datareader(t, _p->reader_qos);
        }
    }

    if (!info.writer.ts.empty()) {
        _p->participant->register_type(info.writer.ts);
        auto* t = _p->participant->create_topic(
            info.writer.name, info.writer.ts.get_type_name(),
            fdds::TOPIC_QOS_DEFAULT);
        w = _p->pub->create_datawriter(t, _p->writer_qos, nullptr);
    }

    if (l) {
        l->reader = r;
        l->writer = w;
        _p->listeners.push_back(l);
    }

    if (r) _p->readers.insertOne(info.name, r);
    if (w) _p->writers.insertOne(info.name, w);

    veLogIs("ve::dds", "topic group created:", info.name);
}

fdds::DataWriter* Participant::writer(const std::string& name) const
{
    return _p->writers.value(name, nullptr);
}

fdds::DataReader* Participant::reader(const std::string& name) const
{
    return _p->readers.value(name, nullptr);
}

// ============================================================================
// Error helper
// ============================================================================

int Participant::error(const std::string& what, int code)
{
    if (code == -1)
        veLogEs("ve::dds", "ERROR:", what);
    else
        veLogEs("ve::dds", "ERROR:", what, "code:", code);
    return code;
}

// ============================================================================
// Singleton management
// ============================================================================

static Dict<Participant*>& instanceMap()
{
    static Dict<Participant*> m;
    return m;
}

Participant& Participant::instance(int domain_id)
{
    auto key = std::to_string(domain_id);
    auto& m = instanceMap();
    auto* p = m.value(key, nullptr);
    if (!p) {
        p = new Participant(domain_id);
        m.insertOne(key, p);
    }
    return *p;
}

void Participant::destroy(int domain_id)
{
    auto key = std::to_string(domain_id);
    auto& m = instanceMap();
    auto* p = m.value(key, nullptr);
    if (p) {
        m.erase(key);
        delete p;
    }
}

} // namespace ve::dds
