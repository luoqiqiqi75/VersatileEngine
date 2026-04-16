#include "ve/qt/node_qobject.h"
#include "ve/qt/node_signal_bridge.h"

#include "ve/core/node.h"

namespace ve {

NodeQObject::NodeQObject(Node* node, QObject* parent)
    : QObject(parent)
    , bridge_(std::make_unique<NodeSignalBridge>(
          this,
          [this](const QVariant& nv, const QVariant& ov) { emit nodeValueChanged(nv, ov); },
          [this](const QString& key, int overlap) { emit nodeChildAdded(key, overlap); },
          [this](const QString& key, int overlap) { emit nodeChildRemoved(key, overlap); }))
    , node_(node)
{
    bridge_->attach(node_);
}

NodeQObject::~NodeQObject() = default;

} // namespace ve
