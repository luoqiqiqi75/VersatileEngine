#include "ve/qt/node_signal_bridge.h"

#include "ve/core/node.h"
#include "ve/core/object.h"
#include "ve/qt/var_qt.h"

#include <QPointer>
#include <QTimer>

namespace ve {

struct NodeSignalBridge::Impl : Object
{
    NodeSignalBridge* owner = nullptr;
    QPointer<QObject> target;
    Node* n = nullptr;

    explicit Impl(NodeSignalBridge* o, QObject* t)
        : Object("node_signal_bridge"), owner(o), target(t) {}

    void attach(Node* node)
    {
        if (n) {
            n->disconnect(this);
        }
        n = node;
        if (!n || !target) {
            return;
        }

        if (owner->onChanged) {
            n->connect<Node::NODE_CHANGED>(this, [this](const Var& nv, const Var& ov) {
                if (!target || !owner->onChanged) {
                    return;
                }
                const QVariant qn = qt::varToQVariant(nv);
                const QVariant qo = qt::varToQVariant(ov);
                auto cb = owner->onChanged;
                QPointer<QObject> tp = target;
                QTimer::singleShot(0, tp.data(), [tp, cb, qn, qo]() {
                    if (tp) {
                        cb(qn, qo);
                    }
                });
            });
        }

        if (owner->onAdded) {
            n->connect<Node::NODE_ADDED>(this, [this](const std::string& key, int overlap) {
                if (!target || !owner->onAdded) {
                    return;
                }
                const QString qk = qt::utf8ToQString(key);
                auto cb = owner->onAdded;
                QPointer<QObject> tp = target;
                QTimer::singleShot(0, tp.data(), [tp, cb, qk, overlap]() {
                    if (tp) {
                        cb(qk, overlap);
                    }
                });
            });
        }

        if (owner->onRemoved) {
            n->connect<Node::NODE_REMOVED>(this, [this](const std::string& key, int overlap) {
                if (!target || !owner->onRemoved) {
                    return;
                }
                const QString qk = qt::utf8ToQString(key);
                auto cb = owner->onRemoved;
                QPointer<QObject> tp = target;
                QTimer::singleShot(0, tp.data(), [tp, cb, qk, overlap]() {
                    if (tp) {
                        cb(qk, overlap);
                    }
                });
            });
        }
    }

    ~Impl()
    {
        if (n) {
            n->disconnect(this);
        }
    }
};

NodeSignalBridge::NodeSignalBridge(QObject* target,
                                   ChangedFn onChanged_,
                                   ChildFn onAdded_,
                                   ChildFn onRemoved_)
    : onChanged(std::move(onChanged_))
    , onAdded(std::move(onAdded_))
    , onRemoved(std::move(onRemoved_))
    , impl_(std::make_unique<Impl>(this, target))
{
}

NodeSignalBridge::~NodeSignalBridge() = default;

void NodeSignalBridge::attach(Node* node)
{
    impl_->attach(node);
}

void NodeSignalBridge::detach()
{
    impl_->attach(nullptr);
}

Node* NodeSignalBridge::node() const
{
    return impl_->n;
}

} // namespace ve
