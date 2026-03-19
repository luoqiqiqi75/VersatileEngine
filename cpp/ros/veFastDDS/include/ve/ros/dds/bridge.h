// bridge.h — ve::dds::Bridge
//
// High-level Node <-> DDS automatic bridge.
// Exposes Node subtrees as DDS topics (Dynamic Types), subscribes DDS
// topics into Nodes, and exposes ve::command as DDS services.
//
// Usage:
//   auto& p = ve::dds::Participant::instance();
//   ve::dds::Bridge bridge(ve::node::root(), p);
//
//   bridge.expose("robot/imu");          // NODE_CHANGED → DDS publish
//   bridge.subscribe("sensor/lidar", "robot/lidar");  // DDS → Node
//   bridge.exposeCommand("get", "ve/get_service");    // DDS srv → command

#pragma once

#include "dynamic.h"
#include "ve/core/command.h"

namespace ve::dds {

class VE_API Bridge
{
    VE_DECLARE_PRIVATE

public:
    Bridge(Node* root, Participant& participant);
    ~Bridge();

    // Expose a Node subtree as a DDS topic using Dynamic Types.
    // On NODE_CHANGED, the subtree is serialized and published.
    // topic defaults to node_path if empty.
    void expose(const std::string& node_path, const std::string& topic = "");

    // Subscribe to a DDS topic and populate a Node subtree.
    // The Node must already have a schema (children with typed values).
    void subscribe(const std::string& topic, const std::string& node_path);

    // Expose a registered ve::command as a DDS service.
    // Request payload: DDS string field "input" → command::call(cmd_key, Var(input))
    // Reply payload: DDS string field "result" + int32 "code"
    void exposeCommand(const std::string& cmd_key,
                       const std::string& srv_name = "");

    int exposedCount() const;
    int subscribedCount() const;
};

} // namespace ve::dds
