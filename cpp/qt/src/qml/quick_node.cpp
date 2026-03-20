// ----------------------------------------------------------------------------
// quick_node.cpp — QML bridge for ve::Node tree (slash-path access)
// ----------------------------------------------------------------------------

#include "ve/qt/qml/quick_node.h"

#include "ve/core/node.h"
#include "ve/core/object.h"
#include "ve/qt/var_qt.h"

#include <QJSValue>
#include <QPointer>
#include <QTimer>

namespace ve {

static std::string toUtf8(const QString& s)
{
    QByteArray b = s.toUtf8();
    return std::string(b.constData(), static_cast<std::size_t>(b.size()));
}

static QVariant unwrapJsValue(const QVariant& var)
{
    return qstrcmp(var.typeName(), "QJSValue") == 0 ? var.value<QJSValue>().toVariant() : var;
}

// ============================================================================
// QuickNode — single reactive node (registered as VEData in QML)
// ============================================================================

struct QuickNode::Private : Object
{
    QuickNode* qobj = nullptr;
    Node* n = nullptr;
    QString path;

    explicit Private(QuickNode* q) : Object("quick_node_bridge"), qobj(q) {}

    void attach(Node* node)
    {
        if (n) {
            n->disconnect(this);
        }
        n = node;
        if (!n || !qobj) {
            return;
        }
        n->connect<Node::NODE_CHANGED>(this, [this](const Var& nv, const Var&) {
            QPointer<QuickNode> qp(qobj);
            if (!qp) {
                return;
            }
            const QVariant qn = qt::varToQVariant(nv);
            QTimer::singleShot(0, qp.data(), [qp, qn]() {
                if (qp) {
                    emit qp->changed(qn);
                }
            });
        });
        n->connect<Node::NODE_ADDED>(this, [this](const std::string& key, int) {
            QPointer<QuickNode> qp(qobj);
            if (!qp) {
                return;
            }
            const QString qk = qt::utf8ToQString(key);
            QTimer::singleShot(0, qp.data(), [qp, qk]() {
                if (qp) {
                    emit qp->added(qk);
                }
            });
        });
        n->connect<Node::NODE_REMOVED>(this, [this](const std::string& key, int) {
            QPointer<QuickNode> qp(qobj);
            if (!qp) {
                return;
            }
            const QString qk = qt::utf8ToQString(key);
            QTimer::singleShot(0, qp.data(), [qp, qk]() {
                if (qp) {
                    emit qp->removed(qk);
                }
            });
        });
    }

    ~Private()
    {
        if (n) {
            n->disconnect(this);
        }
    }
};

QuickNode::QuickNode(QObject* parent) : QObject(parent), _p(std::make_unique<Private>(this)) {}

QuickNode::QuickNode(Node* node, QObject* parent) : QuickNode(parent)
{
    _p->n = node;
    _p->attach(node);
}

QuickNode::QuickNode(const QString& path, QObject* parent) : QuickNode(parent)
{
    setPath(path);
}

QuickNode::~QuickNode() = default;

Node* QuickNode::veNode() const { return _p->n; }

bool QuickNode::valid() const { return _p->n != nullptr; }

QString QuickNode::path() const { return _p->path; }

void QuickNode::setPath(const QString& path)
{
    _p->path = path;
    Node* node = path.isEmpty() ? nullptr : ve::n(toUtf8(path));
    _p->attach(node);
    emit pathChanged(path);
}

QVariant QuickNode::value() const
{
    if (!_p->n) {
        return {};
    }
    return qt::varToQVariant(_p->n->value());
}

void QuickNode::setValue(const QVariant& value)
{
    if (_p->n) {
        _p->n->set(qt::qVariantToVar(unwrapJsValue(value)));
    }
}

void QuickNode::trigger()
{
    if (_p->n && _p->n->hasValue()) {
        emit changed(qt::varToQVariant(_p->n->value()));
    }
}

void QuickNode::fromVar(const QVariant& var)
{
    setValue(var);
}

QVariant QuickNode::toVar() const
{
    return value();
}

void QuickNode::fromVar(const QString& subPath, const QVariant& var, bool auto_trigger)
{
    if (subPath.isEmpty()) {
        fromVar(var);
        return;
    }
    if (!_p->n) {
        return;
    }
    Node* child = _p->n->ensure(toUtf8(subPath));
    child->set(qt::qVariantToVar(unwrapJsValue(var)));
    if (auto_trigger) {
        trigger();
    }
}

QVariant QuickNode::toVar(const QString& subPath) const
{
    if (!_p->n) {
        return {};
    }
    Node* child = _p->n->resolve(toUtf8(subPath));
    if (!child) {
        return {};
    }
    return qt::varToQVariant(child->value());
}

void QuickNode::fromProperties(QObject* obj)
{
    if (!obj || !_p->n) {
        return;
    }
    for (auto* child = _p->n->first(); child; child = child->next()) {
        const std::string& nm = child->name();
        if (nm.empty()) {
            continue;
        }
        QVariant v = obj->property(nm.c_str());
        if (v.isValid()) {
            child->set(qt::qVariantToVar(v));
        }
    }
}

void QuickNode::toProperties(QObject* obj) const
{
    if (!obj || !_p->n) {
        return;
    }
    for (auto* child = _p->n->first(); child; child = child->next()) {
        const std::string& nm = child->name();
        if (nm.empty()) {
            continue;
        }
        obj->setProperty(nm.c_str(), qt::varToQVariant(child->value()));
    }
}

// ============================================================================
// QuickRootNode — root accessor (registered as VENode singleton in QML)
// ============================================================================

struct QuickRootNode::Private {
    Node* n = nullptr;
};

QuickRootNode::QuickRootNode(QObject* parent) : QObject(parent), _p(std::make_unique<Private>())
{
    _p->n = node::root();
}

QuickRootNode::QuickRootNode(const QString& path, QObject* parent) : QuickRootNode(parent)
{
    if (!path.isEmpty()) {
        _p->n = ve::n(toUtf8(path));
    }
}

QuickRootNode::~QuickRootNode() = default;

Node* QuickRootNode::veNode() const { return _p->n; }

bool QuickRootNode::valid() const { return _p->n != nullptr; }

QuickNode* QuickRootNode::data() const
{
    return new QuickNode(_p->n, const_cast<QuickRootNode*>(this));
}

QuickRootNode* QuickRootNode::at(const QString& path) const
{
    if (!_p->n) {
        return new QuickRootNode(const_cast<QuickRootNode*>(this));
    }
    Node* child = _p->n->ensure(toUtf8(path));
    auto* sub = new QuickRootNode(const_cast<QuickRootNode*>(this));
    sub->_p->n = child;
    return sub;
}

QVariant QuickRootNode::get(const QString& path) const
{
    if (!_p->n) {
        return {};
    }
    Node* child = _p->n->resolve(toUtf8(path));
    if (!child) {
        return {};
    }
    return qt::varToQVariant(child->value());
}

QVariant QuickRootNode::get(const QString& path, const QVariant& default_var) const
{
    if (!_p->n) {
        return default_var;
    }
    Node* child = _p->n->resolve(toUtf8(path));
    if (!child || !child->hasValue()) {
        return default_var;
    }
    return qt::varToQVariant(child->value());
}

void QuickRootNode::set(const QString& path, const QVariant& var) const
{
    if (!_p->n) {
        return;
    }
    _p->n->ensure(toUtf8(path))->set(qt::qVariantToVar(var));
}

void QuickRootNode::trigger(const QString& path) const
{
    if (!_p->n) {
        return;
    }
    Node* child = _p->n->resolve(toUtf8(path));
    if (child && child->hasValue()) {
        child->set(child->value());
    }
}

QVariant QuickRootNode::exportAsVar(const QString& path) const
{
    return get(path);
}

void QuickRootNode::importFromVar(const QString& path, const QVariant& var) const
{
    set(path, var);
}

} // namespace ve
