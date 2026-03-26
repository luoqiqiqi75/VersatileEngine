// ----------------------------------------------------------------------------
// schema_qt.h — ve::schema::Json <-> QJsonDocument (UTF-8)
// ----------------------------------------------------------------------------
#pragma once

#include "ve/core/node.h"
#include "ve/core/schema.h"

#include <QByteArray>
#include <QJsonDocument>

namespace ve::qt {

inline QJsonDocument nodeToQJsonDoc(const ve::Node* node, int indent = 2)
{
    const std::string s = ve::schema::exportAs<ve::schema::JsonS>(node, indent);
    return QJsonDocument::fromJson(QByteArray::fromStdString(s));
}

inline bool importQJsonInto(ve::Node* node, const QJsonDocument& doc)
{
    const QByteArray b = doc.toJson(QJsonDocument::Compact);
    return ve::schema::importAs<ve::schema::JsonS>(node, std::string(b.constData(), static_cast<std::size_t>(b.size())));
}

} // namespace ve::qt
