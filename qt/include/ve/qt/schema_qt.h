// ----------------------------------------------------------------------------
// schema_qt.h — ve::schema QJsonS / QVariantS format structs + helpers
// ----------------------------------------------------------------------------
#pragma once

#include "ve/core/node.h"
#include "ve/core/schema.h"
#include "ve/qt/var_qt.h"

#include <QJsonDocument>
#include <QJsonValue>
#include <QVariant>

namespace ve::schema {

struct QJsonS
{
    struct ExportOptions { bool auto_ignore = true; };

    VE_API static QJsonValue exportNode(const Node* node);
    VE_API static QJsonValue exportNode(const Node* node, const ExportOptions& options);
    VE_API static bool importNode(Node* node, const QJsonValue& data);
    VE_API static bool importNode(Node* node, const QJsonValue& data, int copy_flags);
};

struct QVariantS
{
    struct ExportOptions { bool auto_ignore = true; };

    VE_API static QVariant exportNode(const Node* node);
    VE_API static QVariant exportNode(const Node* node, const ExportOptions& options);
    VE_API static bool importNode(Node* node, const QVariant& data);
    VE_API static bool importNode(Node* node, const QVariant& data, int copy_flags);
};

} // namespace ve::schema

// --- Convenience helpers ---------------------------------------------------

namespace ve::qt {

inline QJsonDocument nodeToQJsonDoc(const ve::Node* node, int indent = 2)
{
    const QJsonValue jv = ve::schema::exportAs<ve::schema::QJsonS>(node);
    if (jv.isObject()) {
        return QJsonDocument(jv.toObject());
    }
    if (jv.isArray()) {
        return QJsonDocument(jv.toArray());
    }
    // scalar: wrap in object
    QJsonObject obj;
    obj["_value"] = jv;
    return QJsonDocument(obj);
}

inline bool importQJsonInto(ve::Node* node, const QJsonDocument& doc)
{
    QJsonValue jv;
    if (doc.isObject()) {
        jv = doc.object();
    } else if (doc.isArray()) {
        jv = doc.array();
    }
    return ve::schema::importAs<ve::schema::QJsonS>(node, jv);
}

} // namespace ve::qt
