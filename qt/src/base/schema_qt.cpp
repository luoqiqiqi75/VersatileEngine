// schema_qt.cpp — QJsonS / QVariantS SchemaTraits implementation
#include "ve/qt/schema_qt.h"

namespace ve::schema {

// ============================================================================
// QJsonS — delegates to VarS via varToQJsonValue / qJsonValueToVar
// ============================================================================

QJsonValue SchemaTraits<QJsonS>::exportNode(const Node* node)
{
    return exportNode(node, {});
}

QJsonValue SchemaTraits<QJsonS>::exportNode(const Node* node, const ExportOptions& options)
{
    const Var v = SchemaTraits<VarS>::exportNode(node, {options.auto_ignore});
    return qt::varToQJsonValue(v);
}

bool SchemaTraits<QJsonS>::importNode(Node* node, const QJsonValue& data)
{
    return importNode(node, data, Node::COPY_DEFAULT);
}

bool SchemaTraits<QJsonS>::importNode(Node* node, const QJsonValue& data, int copy_flags)
{
    if (!node) {
        return false;
    }
    const Var v = qt::qJsonValueToVar(data);
    return SchemaTraits<VarS>::importNode(node, v, copy_flags);
}

// ============================================================================
// QVariantS — delegates to VarS via varToQVariant / qVariantToVar
// ============================================================================

QVariant SchemaTraits<QVariantS>::exportNode(const Node* node)
{
    return exportNode(node, {});
}

QVariant SchemaTraits<QVariantS>::exportNode(const Node* node, const ExportOptions& options)
{
    const Var v = SchemaTraits<VarS>::exportNode(node, {options.auto_ignore});
    return qt::varToQVariant(v);
}

bool SchemaTraits<QVariantS>::importNode(Node* node, const QVariant& data)
{
    return importNode(node, data, Node::COPY_DEFAULT);
}

bool SchemaTraits<QVariantS>::importNode(Node* node, const QVariant& data, int copy_flags)
{
    if (!node) {
        return false;
    }
    const Var v = qt::qVariantToVar(data);
    return SchemaTraits<VarS>::importNode(node, v, copy_flags);
}

} // namespace ve::schema
