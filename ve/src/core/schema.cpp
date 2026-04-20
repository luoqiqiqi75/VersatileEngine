// ----------------------------------------------------------------------------
// schema.cpp — Schema::build + SchemaTraits<Json/Bin> implementations
// ----------------------------------------------------------------------------
#include "ve/core/schema.h"
#include "ve/core/node.h"
#include "ve/core/impl/json.h"
#include "ve/core/impl/bin.h"
#include "ve/core/impl/xml.h"  // for SchemaTraits<Xml>
#include "ve/core/impl/md.h"   // for SchemaTraits<Md>

namespace ve {

// ============================================================================
// Schema::build
// ============================================================================

void Schema::build(Node* node) const
{
    if (!node) return;
    for (auto& f : fields) {
        if (f.sub) f.sub->build(node->append(f.name));
    }
}

// ============================================================================
// SchemaTraits<JsonS>
// ============================================================================

namespace schema {

std::string SchemaTraits<JsonS>::exportNode(const Node* node, int indent)
{
    return impl::json::exportTree(node, indent);
}

std::string SchemaTraits<JsonS>::exportNode(const Node* node, const ExportOptions& options)
{
    return impl::json::exportTree(node, options);
}

bool SchemaTraits<JsonS>::importNode(Node* node, const std::string& data)
{
    return impl::json::importTree(node, data);
}

bool SchemaTraits<JsonS>::importNode(Node* node, const std::string& data, const ImportOptions& options)
{
    return impl::json::importTree(node, data, options);
}

// ============================================================================
// SchemaTraits<BinS>
// ============================================================================

Bytes SchemaTraits<BinS>::exportNode(const Node* node)
{
    return impl::bin::exportTree(node);
}

Bytes SchemaTraits<BinS>::exportNode(const Node* node, const ExportOptions& options)
{
    return impl::bin::exportTree(node, options);
}

bool SchemaTraits<BinS>::importNode(Node* node, const uint8_t* data, size_t len)
{
    return impl::bin::importTree(node, data, len);
}

bool SchemaTraits<BinS>::importNode(Node* node, const uint8_t* data, size_t len, const ImportOptions& options)
{
    return impl::bin::importTree(node, data, len, options);
}

// ============================================================================
// SchemaTraits<Xml>
// ============================================================================

std::string SchemaTraits<XmlS>::exportNode(const Node* node, int indent)
{
    return impl::xml::exportTree(node, indent);
}

std::string SchemaTraits<XmlS>::exportNode(const Node* node, const ExportOptions& options)
{
    return impl::xml::exportTree(node, options);
}

bool SchemaTraits<XmlS>::importNode(Node* node, const std::string& data)
{
    return impl::xml::importTree(node, data);
}

bool SchemaTraits<XmlS>::importNode(Node* node, const std::string& data, const ImportOptions& options)
{
    return impl::xml::importTree(node, data, options);
}

// ============================================================================
// SchemaTraits<Var>
// ============================================================================

static Var nodeToVarImpl(const Node* node, const ExportOptions& options)
{
    if (!node) return Var();

    auto all_children = node->children();
    Vector<const Node*> visible_children;
    visible_children.reserve(all_children.sizeAsInt());
    for (auto* child : all_children) {
        if (options.auto_ignore && child && !child->name().empty() && child->name()[0] == '_') {
            continue;
        }
        visible_children.push_back(child);
    }

    const int nch = visible_children.sizeAsInt();
    if (nch == 0) {
        return node->get();
    }

    // First child has empty name => List
    if (visible_children[0]->name().empty()) {
        Var::ListV list;
        list.reserve(nch);
        for (auto* c : visible_children) {
            list.push_back(nodeToVarImpl(c, options));
        }
        return Var(std::move(list));
    }

    // Otherwise => Dict
    Var::DictV dict;
    const bool hasVal = !node->get().isNull();
    
    if (hasVal) {
        dict["_value"] = node->get();
    }

    Strings names;
    Hash<Vector<const Node*>> named_groups;
    Vector<const Node*> anonymous_children;
    Hash<char> seen;
    for (auto* child : visible_children) {
        if (child->name().empty()) {
            anonymous_children.push_back(child);
            continue;
        }
        if (!seen.count(child->name())) {
            seen[child->name()] = 0;
            names.push_back(child->name());
        }
        named_groups[child->name()].push_back(child);
    }

    for (auto& nm : names) {
        auto& group = named_groups[nm];
        dict[nm] = nodeToVarImpl(group[0], options);
    }

    if (!anonymous_children.empty()) {
        Var::ListV list;
        list.reserve(anonymous_children.size());
        for (auto* c : anonymous_children) {
            list.push_back(nodeToVarImpl(c, options));
        }
        dict[""] = Var(std::move(list));
    }

    return Var(std::move(dict));
}

Var SchemaTraits<VarS>::exportNode(const Node* node)
{
    ExportOptions options;
    return exportNode(node, options);
}

Var SchemaTraits<VarS>::exportNode(const Node* node, const ExportOptions& options)
{
    return nodeToVarImpl(node, options);
}

static void resetNodeContent(Node* node)
{
    if (!node) return;
    node->clear();
    node->set(Var());
}

static void clearAnonymousChildren(Node* node)
{
    if (!node) return;
    for (int i = node->count() - 1; i >= 0; --i) {
        auto* child = node->child(i);
        if (child && child->name().empty())
            node->remove(child);
    }
}

static Node* ensureSchemaChild(Node* node, const std::string& key)
{
    if (!node) return nullptr;

    while (node->count(key) > 1)
        node->remove(key, node->count(key) - 1);

    if (auto* child = node->child(key)) {
        resetNodeContent(child);
        return child;
    }
    return node->append(key);
}

static void varToNodeImpl(const Var& var, Node* node)
{
    if (!node) return;

    if (var.type() == Var::DICT) {
        auto& dict = var.toDict();
        for (auto& kv : dict) {
            if (kv.first == "_value") {
                node->set(kv.second);
            } else if (kv.first.empty()) {
                // Anonymous children list
                clearAnonymousChildren(node);
                if (kv.second.type() == Var::LIST) {
                    for (auto& elem : kv.second.toList()) {
                        varToNodeImpl(elem, node->append());
                    }
                }
            } else {
                // Named child
                Node* c = ensureSchemaChild(node, kv.first);
                if (!c) continue;
                
                if (kv.second.type() == Var::LIST) {
                    for (auto& elem : kv.second.toList()) {
                        varToNodeImpl(elem, c->append());
                    }
                } else {
                    varToNodeImpl(kv.second, c);
                }
            }
        }
    } else if (var.type() == Var::LIST) {
        auto& list = var.toList();
        for (auto& elem : list) {
            varToNodeImpl(elem, node->append());
        }
    } else {
        node->set(var);
    }
}

bool SchemaTraits<VarS>::importNode(Node* node, const Var& data)
{
    if (!node) return false;
    varToNodeImpl(data, node);
    return true;
}

bool SchemaTraits<VarS>::importNode(Node* node, const Var& data, const ImportOptions& options)
{
    if (!node) return false;
    Node parsed("var_import");
    varToNodeImpl(data, &parsed);
    node->copy(&parsed, options.auto_insert, options.auto_remove, options.auto_update);
    return true;
}

// ============================================================================
// SchemaTraits<MdS>
// ============================================================================

std::string SchemaTraits<MdS>::exportNode(const Node* node, int indent)
{
    return impl::md::exportTree(node, indent);
}

std::string SchemaTraits<MdS>::exportNode(const Node* node, const ExportOptions& options)
{
    return impl::md::exportTree(node, options);
}

bool SchemaTraits<MdS>::importNode(Node* node, const std::string& data)
{
    return impl::md::importTree(node, data);
}

bool SchemaTraits<MdS>::importNode(Node* node, const std::string& data, const ImportOptions& options)
{
    return impl::md::importTree(node, data, options);
}

// ============================================================================
// Runtime format registry
// ============================================================================

static Hash<SchemaFormatHandler>& formatRegistry()
{
    static Hash<SchemaFormatHandler> registry;
    return registry;
}

void registerSchemaFormat(const std::string& name, SchemaFormatHandler handler)
{
    formatRegistry()[name] = std::move(handler);
}

bool hasSchemaFormat(const std::string& name)
{
    return formatRegistry().count(name) > 0;
}

std::vector<std::string> schemaFormatNames()
{
    std::vector<std::string> names;
    for (auto& [k, _] : formatRegistry())
        names.push_back(k);
    return names;
}

std::string exportSchemaFormat(const std::string& name, const Node* node)
{
    auto& reg = formatRegistry();
    if (reg.count(name) == 0) return {};
    return reg[name].exportFn(node);
}

bool importSchemaFormat(const std::string& name, Node* node, const std::string& data)
{
    auto& reg = formatRegistry();
    if (reg.count(name) == 0) return false;
    return reg[name].importFn(node, data);
}

} // namespace schema
} // namespace ve
