// ----------------------------------------------------------------------------
// node_signal_bridge.h — shared Node signal -> Qt thread marshaling
// ----------------------------------------------------------------------------
#pragma once

#include "ve/global.h"

#include <QObject>
#include <QVariant>
#include <functional>
#include <memory>

namespace ve {

class Node;

class VE_API NodeSignalBridge
{
public:
    using ChangedFn = std::function<void(const QVariant& newVal, const QVariant& oldVal)>;
    using ChildFn   = std::function<void(const QString& key, int overlap)>;

    explicit NodeSignalBridge(QObject* target,
                              ChangedFn onChanged = {},
                              ChildFn onAdded = {},
                              ChildFn onRemoved = {});
    ~NodeSignalBridge();

    void attach(Node* node);
    void detach();

    Node* node() const;

    ChangedFn onChanged;
    ChildFn   onAdded;
    ChildFn   onRemoved;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ve
