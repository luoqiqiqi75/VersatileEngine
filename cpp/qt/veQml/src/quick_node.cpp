// ----------------------------------------------------------------------------
// quick_node.cpp — QML bridge for ve::Data tree
// ----------------------------------------------------------------------------
// Ported from mxhelper (src/channel/qml/quick_node.cpp)
// ----------------------------------------------------------------------------

#include "ve/qml/quick_node.h"

#include <QJSValue>

namespace ve {

// ===========================================================================
// QuickNode
// ===========================================================================

struct QuickNode::Private
{
    ve::Data* n_ = ve::data::manager().nullData();

    void setData(QuickNode* context, ve::Data* new_n) {
        if (n_ == new_n) return;

        if (n_ && !n_->isNull()) {
            disconnect(n_, &Data::changed, context, &QuickNode::changed);
            disconnect(n_, &Data::added, context, &QuickNode::added);
            disconnect(n_, &Data::removed, context, &QuickNode::removed);
        }

        if (new_n && !new_n->isNull()) {
            connect(new_n, &Data::changed, context, &QuickNode::changed);
            connect(new_n, &Data::added, context, &QuickNode::added);
            connect(new_n, &Data::removed, context, &QuickNode::removed);
        }

        n_ = new_n;
    }
};

QuickNode::QuickNode(QObject* parent) : QObject(parent), _p(new Private) {}
QuickNode::QuickNode(Data* node, QObject* parent) : QuickNode(parent) { _p->setData(this, node); }
QuickNode::QuickNode(const QString& path, QObject* parent) : QuickNode(ve::d(path), parent) {}

QuickNode::~QuickNode() {}

Data* QuickNode::D() const { return _p->n_; }

bool QuickNode::valid() const { return !_p->n_->isNull(); }

QString QuickNode::path() const
{
    return _p->n_->fullName();
}

void QuickNode::setPath(const QString& path)
{
    _p->setData(this, ve::d(path));
    emit pathChanged(path);
}

QVariant QuickNode::value() const { return _p->n_->get(); }
void QuickNode::setValue(const QVariant& value) { _p->n_->set(this, value); }

void QuickNode::trigger() { _p->n_->trigger(); }

static QVariant jsVar2StdVar(const QVariant& var)
{
    return qstrcmp(var.typeName(), "QJSValue") == 0 ? var.value<QJSValue>().toVariant() : var;
}

void QuickNode::fromVar(const QVariant& var) { _p->n_->importFromVariant(this, jsVar2StdVar(var)); }
QVariant QuickNode::toVar() const { return _p->n_->exportToVariant(false); }

void QuickNode::fromVar(const QString& path, const QVariant& var, bool auto_trigger)
{
    if (path.isEmpty()) return fromVar(var);
    _p->n_->r(path)->importFromVariant(this, jsVar2StdVar(var));
    if (auto_trigger) _p->n_->trigger();
}

QVariant QuickNode::toVar(const QString& path) const
{
    return _p->n_->r(path)->exportToVariant(false);
}

void QuickNode::fromProperties(QObject* obj)
{
    if (!obj) {
        qCritical() << "<ve.qml> from null object to node:" << _p->n_->fullName();
        return;
    }
    Data* node = _p->n_->first();
    if (node->isRelative()) {
        qCritical() << "<ve.qml> from" << obj << "to unsupported list-like node:" << _p->n_->fullName();
        return;
    }
    for (; !node->isNull(); node = node->next()) {
        QVariant v = obj->property(node->name().toStdString().c_str());
        if (v.isValid()) node->set(this, v);
    }
}

void QuickNode::toProperties(QObject* obj) const
{
    if (!obj) {
        qCritical() << "<ve.qml> to null object from node:" << _p->n_->fullName();
        return;
    }
    Data* node = _p->n_->first();
    if (node->isRelative()) {
        qCritical() << "<ve.qml> to" << obj << "from unsupported list-like node:" << _p->n_->fullName();
        return;
    }
    for (; !node->isNull(); node = node->next()) {
        obj->setProperty(node->name().toStdString().c_str(), node->get());
    }
}

QuickNode* QuickNode::test(const QVariant& var)
{
    qDebug() << "test" << var.typeName() << var;
    auto qn = new QuickNode(this);
    qn->setObjectName("test!!");

    if (auto obj = var.value<QObject*>()) {
        qDebug() << obj->metaObject()->className() << obj->objectName() << "value:" << obj->property("value");
    }

    return qn;
}

// ===========================================================================
// QuickRootNode
// ===========================================================================

struct QuickRootNode::Private {
    ve::Data* d_ = ve::data::manager().rootMobj();
};

QuickRootNode::QuickRootNode(QObject* parent) : QObject(parent), _p(new Private) {}
QuickRootNode::QuickRootNode(const QString& path, QObject* parent) : QuickRootNode(parent) { _p->d_ = ve::data::at(path); }
QuickRootNode::~QuickRootNode() = default;

ve::Data* QuickRootNode::D() const { return _p->d_; }

bool QuickRootNode::valid() const { return _p->d_ != nullptr && !_p->d_->isNull(); }

QuickNode* QuickRootNode::data() const { return new QuickNode(_p->d_, _p->d_); }

QuickRootNode* QuickRootNode::at(const QString& path) const { return new QuickRootNode(_p->d_->r(path)->fullName(), _p->d_); }

QVariant QuickRootNode::get(const QString& path) const { return _p->d_->r(path)->get(); }
QVariant QuickRootNode::get(const QString& path, const QVariant& default_var) const { return _p->d_->r(path)->get(default_var); }
void QuickRootNode::set(const QString& path, const QVariant& var) const { _p->d_->set(_p->d_, path, var); }
void QuickRootNode::trigger(const QString& path) const { _p->d_->r(path)->trigger(); }

QVariant QuickRootNode::exportAsVar(const QString& path) const { return _p->d_->r(path)->exportToVariant(); }
void QuickRootNode::importFromVar(const QString& path, const QVariant& var) const { _p->d_->r(path)->importFromVariant(_p->d_, var); }

} // namespace ve
