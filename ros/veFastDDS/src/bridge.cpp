// bridge.cpp — ve::dds::Bridge implementation

#include "ve/ros/dds/bridge.h"

namespace ve::dds {

namespace ftypes = eprosima::fastrtps::types;
namespace fdds   = eprosima::fastdds::dds;

// ============================================================================
// Private
// ============================================================================

struct BridgeEntry {
    std::string            node_path;
    std::string            topic;
    std::unique_ptr<DynPublisher>  pub;
    std::unique_ptr<DynSubscriber> sub;
};

struct Bridge::Private
{
    Node*        root = nullptr;
    Participant* participant = nullptr;

    Vector<BridgeEntry> exposed;
    Vector<BridgeEntry> subscribed;
};

// ============================================================================
// Lifecycle
// ============================================================================

Bridge::Bridge(Node* root, Participant& participant)
    : _p(new Private)
{
    _p->root = root;
    _p->participant = &participant;
}

Bridge::~Bridge()
{
    delete _p;
}

// ============================================================================
// expose — Node → DDS
// ============================================================================

void Bridge::expose(const std::string& node_path, const std::string& topic)
{
    auto* n = _p->root->resolve(node_path, false);
    if (!n) {
        veLogEs("ve::dds::Bridge", "expose: node not found:", node_path);
        return;
    }

    auto topicName = topic.empty() ? node_path : topic;

    BridgeEntry entry;
    entry.node_path = node_path;
    entry.topic     = topicName;
    entry.pub = std::make_unique<DynPublisher>(*_p->participant, topicName, n);

    auto* pub = entry.pub.get();

    // Publish current state immediately
    pub->publish(n);

    // Watch for changes: publish on any child value change
    n->watch(true);
    n->connect<Node::NODE_ACTIVATED>(n, [pub, n]() {
        pub->publish(n);
    });

    // Also publish when the node's own value changes
    n->connect<Node::NODE_CHANGED>(n, [pub, n]() {
        pub->publish(n);
    });

    _p->exposed.push_back(std::move(entry));

    veLogIs("ve::dds::Bridge", "exposed:", node_path, "→ rt/" + topicName);
}

// ============================================================================
// subscribe — DDS → Node
// ============================================================================

void Bridge::subscribe(const std::string& topic, const std::string& node_path)
{
    auto* n = _p->root->ensure(node_path);
    if (!n) {
        veLogEs("ve::dds::Bridge", "subscribe: cannot ensure node:", node_path);
        return;
    }

    BridgeEntry entry;
    entry.node_path = node_path;
    entry.topic     = topic;
    entry.sub = std::make_unique<DynSubscriber>(*_p->participant, topic, n);
    entry.sub->bridgeTo(n);

    _p->subscribed.push_back(std::move(entry));

    veLogIs("ve::dds::Bridge", "subscribed: rt/" + topic, "→", node_path);
}

// ============================================================================
// exposeCommand — ve::command → DDS service
// ============================================================================

void Bridge::exposeCommand(const std::string& cmd_key,
                           const std::string& srv_name)
{
    auto srvName = srv_name.empty() ? ("ve/" + cmd_key) : srv_name;

    // Build a simple request/reply type using Dynamic Types:
    //   Request:  struct { string input; }
    //   Reply:    struct { int32 code; string result; }
    auto* factory = ftypes::DynamicTypeBuilderFactory::get_instance();

    // Request type
    auto reqBuilder = factory->create_struct_builder();
    reqBuilder->set_name(srvName + "_Request");
    reqBuilder->add_member(0, "input", factory->create_string_type());
    auto reqType = reqBuilder->build();

    // Reply type
    auto repBuilder = factory->create_struct_builder();
    repBuilder->set_name(srvName + "_Reply");
    repBuilder->add_member(0, "code", factory->create_int32_type());
    repBuilder->add_member(1, "result", factory->create_string_type());
    auto repType = repBuilder->build();

    fdds::TypeSupport reqTs(new ftypes::DynamicPubSubType(reqType));
    fdds::TypeSupport repTs(new ftypes::DynamicPubSubType(repType));

    auto* reqData = ftypes::DynamicDataFactory::get_instance()->create_data(reqType);
    auto* repData = ftypes::DynamicDataFactory::get_instance()->create_data(repType);

    auto cmdKey = cmd_key;

    _p->participant->createTopicGroup(Participant::TopicGroupInfo{
        srvName,
        {"rq/" + srvName + "Request", std::move(reqTs)},
        {"rr/" + srvName + "Reply",   std::move(repTs)},
        [cmdKey, reqData, repData](Participant::DataReaderT* r,
                                   Participant::DataWriterT* w) -> int {
            fdds::SampleInfo si;
            while (r->get_unread_count() > 0) {
                auto rc = r->take_next_sample(reqData, &si);
                if (rc != eprosima::fastrtps::types::ReturnCode_t::RETCODE_OK)
                    continue;
                if (si.instance_state != fdds::ALIVE_INSTANCE_STATE)
                    continue;

                std::string input;
                reqData->get_string_value(input, 0);

                Result result = command::call(cmdKey, Var(input));

                repData->set_int32_value(result.code(), 0);
                repData->set_string_value(result.content().toString(), 1);

                eprosima::fastrtps::rtps::WriteParams wp;
                wp.related_sample_identity().writer_guid(
                    si.sample_identity.writer_guid());
                wp.related_sample_identity().sequence_number(
                    si.sample_identity.sequence_number());
                w->write(reinterpret_cast<void*>(repData), wp);
            }
            return 0;
        }
    });

    veLogIs("ve::dds::Bridge", "exposeCommand:", cmd_key, "→", srvName);
}

// ============================================================================
// Counts
// ============================================================================

int Bridge::exposedCount()   const { return (int)_p->exposed.size(); }
int Bridge::subscribedCount() const { return (int)_p->subscribed.size(); }

} // namespace ve::dds
