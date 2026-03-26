// ----------------------------------------------------------------------------
// xml.cpp — pugixml based XML export/import for Node
// ----------------------------------------------------------------------------
// Attributes are child nodes with "@" prefix.
// Text content sets node value().
// Uses recursive Node::copy() for import (merge semantics).
// ----------------------------------------------------------------------------
#include "ve/core/impl/xml.h"
#include "ve/core/node.h"
#include "ve/core/schema.h"
#include "ve/core/log.h"
#include <pugixml.hpp>
#include <sstream>
#include <string>

namespace ve {
namespace impl::xml {

std::string exportTree(const Node* node, int indent)
{
    if (!node) return "";
    schema::ExportOptions opts;
    opts.indent = indent;
    return exportTree(node, opts);
}

std::string exportTree(const Node* node, const schema::ExportOptions& options)
{
    if (!node) return "";

    pugi::xml_document doc;
    auto root = doc.append_child(node->name().empty() ? "root" : node->name().c_str());

    // value as text content
    if (!node->get().isNull() && !node->get().isNull()) {
        root.text().set(node->get().toString().c_str());
    }

    // children (including @attr children)
    for (const auto* child : node->children()) {
        if (!child) continue;
        std::string child_name = child->name();
        if (child_name.empty()) child_name = "item";

        auto xml_child = root.append_child(child_name.c_str());
        // recursive
        // TODO: full recursive would call a helper; stub for now
        if (!child->get().isNull()) {
            xml_child.text().set(child->get().toString().c_str());
        }
    }

    std::ostringstream oss;
    doc.save(oss, options.indent > 0 ? "  " : "", pugi::format_default);
    return oss.str();
}

// Helper: recursive pugixml node -> ve::Node (attrs as @key children)
static void xmlNodeToNode(const pugi::xml_node& xml_node, Node* ve_node)
{
    if (!ve_node) return;

    // text content -> value
    if (auto text = xml_node.text()) {
        std::string txt = text.get();
        if (!txt.empty()) ve_node->set(Var(txt));
    }

    // attributes -> @attr children
    for (auto attr : xml_node.attributes()) {
        std::string key = "@" + std::string(attr.name());
        ve_node->append(key)->set(Var(attr.value()));
    }

    // children
    for (auto child = xml_node.first_child(); child; child = child.next_sibling()) {
        if (child.type() == pugi::node_element) {
            std::string name = child.name();
            Node* child_node = ve_node->append(name);
            xmlNodeToNode(child, child_node);
        }
    }
}

bool importTree(Node* node, const std::string& xml)
{
    schema::ImportOptions opts;
    return importTree(node, xml, opts);
}

bool importTree(Node* node, const std::string& xml, const schema::ImportOptions& options)
{
    if (!node || xml.empty()) return false;

    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_string(xml.c_str());
    if (!result) {
        veLogE << "XML parse failed: " << result.description();
        return false;
    }

    Node parsed("xml_import");
    if (auto root = doc.first_child()) {
        xmlNodeToNode(root, &parsed);
    }

    node->copy(&parsed, options.auto_insert, options.auto_remove, options.auto_update);
    return true;
}

} // namespace xml
} // namespace ve
