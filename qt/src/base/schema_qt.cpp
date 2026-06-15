// schema_qt.cpp — QJsonS / QVariantS format implementation
#include "ve/qt/schema_qt.h"

namespace ve::schema {

// ============================================================================
// QJsonS — delegates to VarS via varToQJsonValue / qJsonValueToVar
// ============================================================================

QJsonValue QJsonS::exportNode(const Node* node)
{
    return exportNode(node, {});
}

QJsonValue QJsonS::exportNode(const Node* node, const ExportOptions& options)
{
    const Var v = VarS::exportNode(node, {options.auto_ignore});
    return qt::varToQJsonValue(v);
}

bool QJsonS::importNode(Node* node, const QJsonValue& data)
{
    return importNode(node, data, Node::COPY_DEFAULT);
}

bool QJsonS::importNode(Node* node, const QJsonValue& data, int copy_flags)
{
    if (!node) {
        return false;
    }
    const Var v = qt::qJsonValueToVar(data);
    return VarS::importNode(node, v, copy_flags);
}

// ============================================================================
// QVariantS — delegates to VarS via varToQVariant / qVariantToVar
// ============================================================================

QVariant QVariantS::exportNode(const Node* node)
{
    return exportNode(node, {});
}

QVariant QVariantS::exportNode(const Node* node, const ExportOptions& options)
{
    const Var v = VarS::exportNode(node, {options.auto_ignore});
    return qt::varToQVariant(v);
}

bool QVariantS::importNode(Node* node, const QVariant& data)
{
    return importNode(node, data, Node::COPY_DEFAULT);
}

bool QVariantS::importNode(Node* node, const QVariant& data, int copy_flags)
{
    if (!node) {
        return false;
    }
    const Var v = qt::qVariantToVar(data);
    return VarS::importNode(node, v, copy_flags);
}

} // namespace ve::schema
