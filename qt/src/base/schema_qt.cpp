// schema_qt.cpp — QJsonS / QVariantS SchemaTraits implementation
#include "ve/qt/schema_qt.h"

namespace ve::schema {

// ============================================================================
// QJsonS — delegates to VarS via varToQJsonValue / qJsonValueToVar
// ============================================================================

QJsonValue SchemaTraits<QJsonS>::exportNode(const Node* node)
{
    return exportNode(node, ExportOptions{});
}

QJsonValue SchemaTraits<QJsonS>::exportNode(const Node* node, const ExportOptions& options)
{
    const Var v = SchemaTraits<VarS>::exportNode(node, options);
    return qt::varToQJsonValue(v);
}

bool SchemaTraits<QJsonS>::importNode(Node* node, const QJsonValue& data)
{
    return importNode(node, data, ImportOptions{});
}

bool SchemaTraits<QJsonS>::importNode(Node* node, const QJsonValue& data, const ImportOptions& options)
{
    if (!node) {
        return false;
    }
    const Var v = qt::qJsonValueToVar(data);
    return SchemaTraits<VarS>::importNode(node, v, options);
}

// ============================================================================
// QVariantS — delegates to VarS via varToQVariant / qVariantToVar
// ============================================================================

QVariant SchemaTraits<QVariantS>::exportNode(const Node* node)
{
    return exportNode(node, ExportOptions{});
}

QVariant SchemaTraits<QVariantS>::exportNode(const Node* node, const ExportOptions& options)
{
    const Var v = SchemaTraits<VarS>::exportNode(node, options);
    return qt::varToQVariant(v);
}

bool SchemaTraits<QVariantS>::importNode(Node* node, const QVariant& data)
{
    return importNode(node, data, ImportOptions{});
}

bool SchemaTraits<QVariantS>::importNode(Node* node, const QVariant& data, const ImportOptions& options)
{
    if (!node) {
        return false;
    }
    const Var v = qt::qVariantToVar(data);
    return SchemaTraits<VarS>::importNode(node, v, options);
}

} // namespace ve::schema
