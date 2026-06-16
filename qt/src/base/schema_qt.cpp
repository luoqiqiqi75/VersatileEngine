// schema_qt.cpp — QJsonS / QVariantS format implementation
#include "ve/qt/schema_qt.h"

namespace ve::schema {

// ============================================================================
// QJsonS — delegates to VarS via varToQJsonValue / qJsonValueToVar
// ============================================================================

QJsonValue QJsonS::fromNode(const Node* node)
{
    return fromNode(node, {});
}

QJsonValue QJsonS::fromNode(const Node* node, const ExportOptions& options)
{
    const Var v = VarS::fromNode(node, {options.auto_ignore});
    return qt::varToQJsonValue(v);
}

bool QJsonS::toNode(Node* node, const QJsonValue& data)
{
    return toNode(node, data, Node::COPY_DEFAULT);
}

bool QJsonS::toNode(Node* node, const QJsonValue& data, int copy_flags)
{
    if (!node) {
        return false;
    }
    const Var v = qt::qJsonValueToVar(data);
    return VarS::toNode(node, v, copy_flags);
}

// ============================================================================
// QVariantS — delegates to VarS via varToQVariant / qVariantToVar
// ============================================================================

QVariant QVariantS::fromNode(const Node* node)
{
    return fromNode(node, {});
}

QVariant QVariantS::fromNode(const Node* node, const ExportOptions& options)
{
    const Var v = VarS::fromNode(node, {options.auto_ignore});
    return qt::varToQVariant(v);
}

bool QVariantS::toNode(Node* node, const QVariant& data)
{
    return toNode(node, data, Node::COPY_DEFAULT);
}

bool QVariantS::toNode(Node* node, const QVariant& data, int copy_flags)
{
    if (!node) {
        return false;
    }
    const Var v = qt::qVariantToVar(data);
    return VarS::toNode(node, v, copy_flags);
}

} // namespace ve::schema
