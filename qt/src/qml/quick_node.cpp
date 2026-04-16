// ----------------------------------------------------------------------------
// quick_node.cpp — QML bridge for ve::Node tree (slash-path access)
// ----------------------------------------------------------------------------

#include "ve/qt/qml/quick_node.h"
#include "ve/qt/node_signal_bridge.h"
#include "ve/qt/schema_qt.h"

#include "ve/core/node.h"
#include "ve/qt/var_qt.h"

#include <QJSValue>
#include <QTimer>

namespace ve {

static QVariant unwrapJsValue(const QVariant& var)
{
    return qstrcmp(var.typeName(), "QJSValue") == 0 ? var.value<QJSValue>().toVariant() : var;
}

// ============================================================================
// QuickNode — single reactive node (registered as VEData in QML)
// ============================================================================

struct QuickNode::Private
{
    QuickNode* qobj = nullptr;
    NodeSignalBridge bridge;
    QString path;

    explicit Private(QuickNode* q)
        : qobj(q), bridge(q)
    {
        bridge.onChanged = [this](const QVariant& nv, const QVariant&) {
            if (qobj) {
                emit qobj->changed(nv);
            }
        };
        bridge.onAdded = [this](const QString& key, int) {
            if (qobj) {
                emit qobj->added(key);
            }
        };
        bridge.onRemoved = [this](const QString& key, int) {
            if (qobj) {
                emit qobj->removed(key);
            }
        };
    }
};

QuickNode::QuickNode(QObject* parent) : QObject(parent), _p(std::make_unique<Private>(this)) {}

QuickNode::QuickNode(Node* node, QObject* parent) : QuickNode(parent)
{
    _p->bridge.attach(node);
}

QuickNode::QuickNode(const QString& path, QObject* parent) : QuickNode(parent)
{
    setPath(path);
}

QuickNode::~QuickNode() = default;

Node* QuickNode::veNode() const { return _p->bridge.node(); }

bool QuickNode::valid() const { return _p->bridge.node() != nullptr; }

QString QuickNode::path() const { return _p->path; }

void QuickNode::setPath(const QString& path)
{
    _p->path = path;
    Node* node = path.isEmpty() ? nullptr : ve::n(qt::qStringToUtf8(path));
    _p->bridge.attach(node);
    emit pathChanged(path);
}

QVariant QuickNode::value() const
{
    if (!_p->bridge.node()) {
        return {};
    }
    return qt::varToQVariant(_p->bridge.node()->get());
}

void QuickNode::setValue(const QVariant& value)
{
    if (_p->bridge.node()) {
        _p->bridge.node()->set(qt::qVariantToVar(unwrapJsValue(value)));
    }
}

void QuickNode::trigger()
{
    if (_p->bridge.node()) {
        emit changed(qt::varToQVariant(_p->bridge.node()->get()));
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
    if (!_p->bridge.node()) {
        return;
    }
    Node* child = _p->bridge.node()->at(qt::qStringToUtf8(subPath));
    child->set(qt::qVariantToVar(unwrapJsValue(var)));
    if (auto_trigger) {
        trigger();
    }
}

QVariant QuickNode::toVar(const QString& subPath) const
{
    if (!_p->bridge.node()) {
        return {};
    }
    Node* child = _p->bridge.node()->find(qt::qStringToUtf8(subPath));
    if (!child) {
        return {};
    }
    return qt::varToQVariant(child->get());
}

void QuickNode::fromProperties(QObject* obj)
{
    if (!obj || !_p->bridge.node()) {
        return;
    }
    QVariantMap map;
    for (auto* child = _p->bridge.node()->first(); child; child = child->next()) {
        const std::string& nm = child->name();
        if (nm.empty()) {
            continue;
        }
        QVariant v = obj->property(nm.c_str());
        if (v.isValid()) {
            map.insert(qt::utf8ToQString(nm), v);
        }
    }
    schema::importAs<schema::QVariantS>(_p->bridge.node(), QVariant(map));
}

void QuickNode::toProperties(QObject* obj) const
{
    if (!obj || !_p->bridge.node()) {
        return;
    }
    for (auto* child = _p->bridge.node()->first(); child; child = child->next()) {
        const std::string& nm = child->name();
        if (nm.empty()) {
            continue;
        }
        obj->setProperty(nm.c_str(), qt::varToQVariant(child->get()));
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
        _p->n = ve::n(qt::qStringToUtf8(path));
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
    Node* child = _p->n->at(qt::qStringToUtf8(path));
    auto* sub = new QuickRootNode(const_cast<QuickRootNode*>(this));
    sub->_p->n = child;
    return sub;
}

QVariant QuickRootNode::get(const QString& path) const
{
    if (!_p->n) {
        return {};
    }
    Node* child = _p->n->find(qt::qStringToUtf8(path));
    if (!child) {
        return {};
    }
    return qt::varToQVariant(child->get());
}

QVariant QuickRootNode::get(const QString& path, const QVariant& default_var) const
{
    if (!_p->n) {
        return default_var;
    }
    Node* child = _p->n->find(qt::qStringToUtf8(path));
    if (!child || child->get().isNull()) {
        return default_var;
    }
    return qt::varToQVariant(child->get());
}

void QuickRootNode::set(const QString& path, const QVariant& var) const
{
    if (!_p->n) {
        return;
    }
    _p->n->at(qt::qStringToUtf8(path))->set(qt::qVariantToVar(var));
}

void QuickRootNode::trigger(const QString& path) const
{
    if (!_p->n) {
        return;
    }
    Node* child = _p->n->find(qt::qStringToUtf8(path));
    if (child) {
        child->set(child->get());
    }
}

QVariant QuickRootNode::exportAsVar(const QString& path) const
{
    if (!_p->n) {
        return {};
    }
    Node* child = _p->n->find(qt::qStringToUtf8(path));
    if (!child) {
        return {};
    }
    return schema::exportAs<schema::QVariantS>(child);
}

void QuickRootNode::importFromVar(const QString& path, const QVariant& var) const
{
    if (!_p->n) {
        return;
    }
    Node* child = _p->n->at(qt::qStringToUtf8(path));
    schema::importAs<schema::QVariantS>(child, var);
}

} // namespace ve
