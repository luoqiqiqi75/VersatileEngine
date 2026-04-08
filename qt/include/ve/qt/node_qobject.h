// ----------------------------------------------------------------------------
// node_qobject.h — forward ve::Node signals to Qt (QObject)
// ----------------------------------------------------------------------------
#pragma once

#include "ve/global.h"

#include <QObject>
#include <QVariant>
#include <memory>

namespace ve {

class Node;
class NodeSignalBridge;

class VE_API NodeQObject : public QObject
{
    Q_OBJECT

public:
    explicit NodeQObject(Node* node, QObject* parent = nullptr);
    ~NodeQObject() override;

    Node* watchedNode() const { return node_; }

signals:
    void nodeValueChanged(const QVariant& newValue, const QVariant& oldValue);
    void nodeChildAdded(const QString& key, int overlap);
    void nodeChildRemoved(const QString& key, int overlap);

private:
    std::unique_ptr<NodeSignalBridge> bridge_;
    Node* node_ = nullptr;
};

} // namespace ve
