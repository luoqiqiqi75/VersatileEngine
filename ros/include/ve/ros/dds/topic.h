// topic.h — ve::dds::Topic<PubSubType>
//
// Header-only typed DDS topic with optional Node bridge.
//
// Usage (IDL approach):
//   Topic<SensorDataPubSubType> imu(participant, "robot/imu");
//   imu.publish(msg);
//   imu.onReceive([](const SensorData& m) { ... });
//   imu.bridgeTo(ve::n("robot/imu"), toMsg, toVar);

#pragma once

#include "ve/ros/dds/participant.h"
#include "ve/core/node.h"

namespace ve::dds {

template<typename PubSubType>
class Topic
{
public:
    using MsgT    = typename PubSubType::type;
    using Handler = std::function<void(const MsgT&)>;
    using ToMsg   = std::function<void(const Var&, MsgT&)>;
    using ToVar   = std::function<Var(const MsgT&)>;

    Topic(Participant& p, const std::string& name,
          bool pub = true, bool sub = false)
        : _participant(p), _name(name)
    {
        Participant::TopicGroupInfo info;
        info.name = name;

        if (sub) {
            info.reader.name = "rt/" + name;
            info.reader.ts   = Participant::TypeSupportT(new PubSubType);
            info.func = [this](Participant::DataReaderT* r,
                               Participant::DataWriterT*) -> int {
                fdds::SampleInfo si;
                MsgT msg;
                while (r->get_unread_count() > 0) {
                    auto rc = r->take_next_sample(&msg, &si);
                    if (rc != eprosima::fastrtps::types::ReturnCode_t::RETCODE_OK)
                        continue;
                    if (si.instance_state != fdds::ALIVE_INSTANCE_STATE)
                        continue;
                    if (_handler) _handler(msg);
                }
                return 0;
            };
        }

        if (pub) {
            info.writer.name = "rt/" + name;
            info.writer.ts   = Participant::TypeSupportT(new PubSubType);
        }

        p.createTopicGroup(std::move(info));
    }

    void publish(MsgT& msg)
    {
        _participant.publish(_name, msg);
    }

    void onReceive(Handler h) { _handler = std::move(h); }

    // Bridge to a Node:
    //   - On NODE_CHANGED: call toMsg, then publish
    //   - On DDS receive:  call toVar, then node->set()
    void bridgeTo(Node* n, ToMsg toMsg, ToVar toVar)
    {
        if (!n) return;

        if (toMsg) {
            _bridgeNode = n;
            _toMsg = std::move(toMsg);
            n->connect<Node::NODE_CHANGED>(n, [this](const Var& newVal, const Var&) {
                MsgT msg{};
                _toMsg(newVal, msg);
                publish(msg);
            });
        }

        if (toVar) {
            _toVar = std::move(toVar);
            onReceive([n, conv = _toVar](const MsgT& msg) {
                n->set(conv(msg));
            });
        }
    }

private:
    Participant& _participant;
    std::string  _name;
    Handler      _handler;

    Node*  _bridgeNode = nullptr;
    ToMsg  _toMsg;
    ToVar  _toVar;
};

} // namespace ve::dds
