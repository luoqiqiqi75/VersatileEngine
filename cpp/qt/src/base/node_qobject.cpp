#include "ve/qt/node_qobject.h"

#include "ve/core/node.h"
#include "ve/core/object.h"
#include "ve/qt/var_qt.h"

#include <QPointer>
#include <QTimer>

namespace ve {

struct NodeQObject::Bridge : Object
{
    NodeQObject* qobj = nullptr;
    Node* n = nullptr;

    explicit Bridge(NodeQObject* q) : Object("node_qobject_bridge"), qobj(q) {}

    void attach(Node* node)
    {
        if (n) {
            n->disconnect(this);
        }
        n = node;
        if (!n || !qobj) {
            return;
        }
        n->connect<Node::NODE_CHANGED>(this, [this](const Var& nv, const Var& ov) {
            QPointer<NodeQObject> qp(qobj);
            if (!qp) {
                return;
            }
            const QVariant qn = qt::varToQVariant(nv);
            const QVariant qo = qt::varToQVariant(ov);
            QTimer::singleShot(0, qp.data(), [qp, qn, qo]() {
                if (qp) {
                    emit qp->nodeValueChanged(qn, qo);
                }
            });
        });
        n->connect<Node::NODE_ADDED>(this, [this](const std::string& key, int overlap) {
            QPointer<NodeQObject> qp(qobj);
            if (!qp) {
                return;
            }
            const QString qk = qt::utf8ToQString(key);
            QTimer::singleShot(0, qp.data(), [qp, qk, overlap]() {
                if (qp) {
                    emit qp->nodeChildAdded(qk, overlap);
                }
            });
        });
        n->connect<Node::NODE_REMOVED>(this, [this](const std::string& key, int overlap) {
            QPointer<NodeQObject> qp(qobj);
            if (!qp) {
                return;
            }
            const QString qk = qt::utf8ToQString(key);
            QTimer::singleShot(0, qp.data(), [qp, qk, overlap]() {
                if (qp) {
                    emit qp->nodeChildRemoved(qk, overlap);
                }
            });
        });
    }

    ~Bridge()
    {
        if (n) {
            n->disconnect(this);
        }
    }
};

NodeQObject::NodeQObject(Node* node, QObject* parent) : QObject(parent), bridge_(std::make_unique<Bridge>(this)), node_(node)
{
    bridge_->attach(node_);
}

NodeQObject::~NodeQObject() = default;

} // namespace ve
