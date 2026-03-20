// dynamic.h — ve::dds Dynamic Types support
//
// Build DDS types at runtime from ve::Node schemas — no IDL code generation.
// Provides Var <-> DynamicData conversion and DynPublisher / DynSubscriber.
//
// Mapping:
//   Var::BOOL   → DDS boolean
//   Var::INT    → DDS int64
//   Var::DOUBLE → DDS float64
//   Var::STRING → DDS string
//   Node children → DDS struct fields (recursive)

#pragma once

#include "participant.h"
#include "ve/core/node.h"

#include <fastrtps/types/DynamicTypeBuilderFactory.h>
#include <fastrtps/types/DynamicTypeBuilder.h>
#include <fastrtps/types/DynamicType.h>
#include <fastrtps/types/DynamicPubSubType.h>
#include <fastrtps/types/DynamicData.h>
#include <fastrtps/types/DynamicDataFactory.h>

namespace ve::dds {

namespace ftypes = eprosima::fastrtps::types;

// ============================================================================
// DynType — runtime DDS type from Node schema
// ============================================================================

// Build a DDS DynamicType that mirrors a Node's child structure.
// Each child with a value becomes a struct field; children with children
// become nested structs (recursive).
//
// Example Node:
//   robot/
//     name  = "arm1"      → string field
//     x     = 1.5         → float64 field
//     y     = 2.0         → float64 field
//     active = true       → bool field
//
// Becomes DDS struct:
//   struct robot { string name; double x; double y; boolean active; };
//
VE_API ftypes::DynamicType_ptr buildDynType(Node* schema,
                                            const std::string& type_name = "");

// ============================================================================
// Var <-> DynamicData conversion
// ============================================================================

VE_API void nodeToData(Node* n, ftypes::DynamicData* d);
VE_API void dataToNode(ftypes::DynamicData* d, Node* n);

// ============================================================================
// DynPublisher — publish Node data as DDS dynamic type
// ============================================================================

class VE_API DynPublisher
{
    VE_DECLARE_PRIVATE

public:
    // schema defines the struct layout; topic is the DDS topic name
    DynPublisher(Participant& p, const std::string& topic, Node* schema);
    ~DynPublisher();

    void publish(Node* data);
};

// ============================================================================
// DynSubscriber — subscribe to DDS topic, populate Node
// ============================================================================

class VE_API DynSubscriber
{
    VE_DECLARE_PRIVATE

public:
    using Handler = std::function<void(Node*)>;

    DynSubscriber(Participant& p, const std::string& topic, Node* schema);
    ~DynSubscriber();

    void onReceive(Handler h);

    // Convenience: automatically update target Node on each receive
    void bridgeTo(Node* target);
};

} // namespace ve::dds
