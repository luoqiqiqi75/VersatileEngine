// ----------------------------------------------------------------------------
// quick_node.h — QML bridge for ve::Data tree
// ----------------------------------------------------------------------------
// Ported from mxhelper (src/channel/qml/quick_node.h)
// Provides QuickNode / QuickRootNode for QML ↔ ve::Data interop.
// ----------------------------------------------------------------------------

#pragma once

#include "ve_qml_global.h"
#include "ve/core/data.h"

#include <QObject>
#include <QVariant>

namespace ve {

// ---------------------------------------------------------------------------
// QuickNode — a single ve::Data node exposed to QML
// ---------------------------------------------------------------------------
class VE_QML_API QuickNode : public QObject
{
    Q_OBJECT

public:
    explicit QuickNode(QObject* parent = nullptr);
    explicit QuickNode(ve::Data* node, QObject* parent);
    explicit QuickNode(const QString& path, QObject* parent);

    ~QuickNode() override;

    ve::Data* D() const;

    Q_PROPERTY(bool valid READ valid)
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(QVariant value READ value WRITE setValue NOTIFY changed)

    bool valid() const;

    QString path() const;
    void setPath(const QString& path);

    QVariant value() const;
    void setValue(const QVariant& value);

signals:
    void pathChanged(const QString& path);
    void changed(const QVariant& value);
    void added(const QString& rname);
    void removed(const QString& rname);

public slots:
    void trigger();

    void fromVar(const QVariant& var);
    QVariant toVar() const;

    void fromVar(const QString& path, const QVariant& var, bool auto_trigger = false);
    QVariant toVar(const QString& path) const;

    void fromProperties(QObject* obj);
    void toProperties(QObject* obj) const;

    QuickNode* test(const QVariant& var);

private:
    VE_DECLARE_UNIQUE_PRIVATE
};

// ---------------------------------------------------------------------------
// QuickRootNode — a root accessor for a sub-tree, exposed to QML
// ---------------------------------------------------------------------------
class VE_QML_API QuickRootNode : public QObject
{
    Q_OBJECT

public:
    explicit QuickRootNode(QObject* parent = nullptr);
    explicit QuickRootNode(const QString& path, QObject* parent = nullptr);
    ~QuickRootNode() override;

    ve::Data* D() const;

    Q_PROPERTY(bool valid READ valid)

    bool valid() const;

public slots:
    QuickNode* data() const;

    QuickRootNode* at(const QString& path) const;

    QVariant get(const QString& path) const;
    QVariant get(const QString& path, const QVariant& default_var) const;

    void set(const QString& path, const QVariant& var) const;

    void trigger(const QString& path) const;

    QVariant exportAsVar(const QString& path) const;
    void importFromVar(const QString& path, const QVariant& var) const;

private:
    VE_DECLARE_UNIQUE_PRIVATE
};

} // namespace ve
