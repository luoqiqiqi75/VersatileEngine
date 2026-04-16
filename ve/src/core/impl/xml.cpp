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

// Helper: recursive ve::Node -> pugixml node (reverse of xmlNodeToNode)
static void nodeToXmlNode(const Node* ve_node, pugi::xml_node& xml_node)
{
    if (!ve_node) return;

    // value as text content
    const Var& v = ve_node->get();
    if (v.type() != Var::NONE && !v.isNull()) {
        xml_node.text().set(v.toString().c_str());
    }

    // children (including @attr children)
    for (const auto* child : ve_node->children()) {
        if (!child) continue;
        std::string child_name = child->name();
        
        // Handle attributes
        if (!child_name.empty() && child_name[0] == '@') {
            std::string attr_name = child_name.substr(1);
            const Var& attr_val = child->get();
            if (attr_val.type() != Var::NONE && !attr_val.isNull()) {
                xml_node.append_attribute(attr_name.c_str()) = attr_val.toString().c_str();
            }
        } else {
            // Handle normal children
            if (child_name.empty()) child_name = "item";
            
            // Strip #N suffix for XML tag name
            size_t hash_pos = child_name.find('#');
            if (hash_pos != std::string::npos) {
                child_name = child_name.substr(0, hash_pos);
                if (child_name.empty()) child_name = "item";
            }

            auto xml_child = xml_node.append_child(child_name.c_str());
            nodeToXmlNode(child, xml_child);
        }
    }
}

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

    nodeToXmlNode(node, root);

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
        // trim whitespace to avoid setting empty text nodes
        size_t first = txt.find_first_not_of(" \t\r\n");
        if (first != std::string::npos) {
            ve_node->set(Var(txt));
        }
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

// Helper: fix HTML to be valid XML
static std::string fixHtmlToXml(const std::string& html) {
    std::string xml;
    xml.reserve(html.size());
    std::vector<std::string> stack;
    
    // 1. Remove script and style tags completely as they often contain unescaped < and >
    std::string cleaned = html;
    const char* remove_tags[] = {"script", "style"};
    for (const char* tag : remove_tags) {
        std::string open_tag = "<" + std::string(tag);
        std::string close_tag = "</" + std::string(tag);
        size_t pos = 0;
        while ((pos = cleaned.find(open_tag, pos)) != std::string::npos) {
            // Ensure it's a valid tag boundary
            char next_c = cleaned[pos + open_tag.length()];
            if (next_c != '>' && next_c != ' ' && next_c != '\t' && next_c != '\r' && next_c != '\n' && next_c != '/') {
                pos += open_tag.length();
                continue;
            }

            size_t end = cleaned.find(close_tag, pos);
            if (end != std::string::npos) {
                size_t close_end = cleaned.find(">", end);
                if (close_end != std::string::npos) {
                    cleaned.erase(pos, close_end + 1 - pos);
                } else {
                    cleaned.erase(pos, end + close_tag.length() - pos);
                }
            } else {
                // No closing tag found, just remove the opening tag to avoid erasing the whole file
                size_t tag_end = cleaned.find(">", pos);
                if (tag_end != std::string::npos) {
                    cleaned.erase(pos, tag_end + 1 - pos);
                } else {
                    pos += open_tag.length();
                }
            }
        }
    }

    // 2. Simple state machine to balance tags
    std::vector<std::string> void_tags = {
        "area", "base", "br", "col", "embed", "hr", "img", "input", 
        "link", "meta", "param", "source", "track", "wbr"
    };

    size_t pos = 0;
    while (pos < cleaned.size()) {
        size_t tag_start = cleaned.find("<", pos);
        if (tag_start == std::string::npos) {
            xml += cleaned.substr(pos);
            break;
        }
        
        xml += cleaned.substr(pos, tag_start - pos);
        pos = tag_start;
        
        // Check for comments
        if (cleaned.compare(pos, 4, "<!--") == 0) {
            size_t comment_end = cleaned.find("-->", pos + 4);
            if (comment_end == std::string::npos) {
                break;
            }
            pos = comment_end + 3;
            continue;
        }
        
        // Check for CDATA
        if (cleaned.compare(pos, 9, "<![CDATA[") == 0) {
            size_t cdata_end = cleaned.find("]]>", pos + 9);
            if (cdata_end == std::string::npos) {
                xml += cleaned.substr(pos);
                break;
            }
            xml += cleaned.substr(pos, cdata_end + 3 - pos);
            pos = cdata_end + 3;
            continue;
        }
        
        // Drop any other <! tags (like DOCTYPE)
        if (cleaned.compare(pos, 2, "<!") == 0) {
            size_t end = cleaned.find(">", pos + 2);
            if (end == std::string::npos) {
                break;
            }
            pos = end + 1;
            continue;
        }
        
        // Find end of tag, ignoring > inside quotes
        size_t tag_end = pos + 1;
        bool in_quotes = false;
        char quote_char = 0;
        while (tag_end < cleaned.size()) {
            char c = cleaned[tag_end];
            if (in_quotes) {
                if (c == quote_char) in_quotes = false;
            } else {
                if (c == '"' || c == '\'') {
                    in_quotes = true;
                    quote_char = c;
                } else if (c == '>') {
                    break;
                }
            }
            tag_end++;
        }
        
        if (tag_end >= cleaned.size()) {
            xml += cleaned.substr(pos);
            break;
        }
        
        std::string tag_content = cleaned.substr(pos + 1, tag_end - pos - 1);
        
        if (tag_content.empty()) {
            xml += "<>";
            pos = tag_end + 1;
            continue;
        }
        
        // Ignore processing instructions like <?xml ... ?>
        if (tag_content[0] == '?') {
            pos = tag_end + 1;
            continue;
        }
        
        bool is_closing = (tag_content[0] == '/');
        bool is_self_closing = (tag_content.back() == '/');
        
        std::string tag_name;
        size_t name_start = is_closing ? 1 : 0;
        size_t name_end = tag_content.find_first_of(" \t\r\n/", name_start);
        if (name_end == std::string::npos) {
            tag_name = tag_content.substr(name_start);
        } else {
            tag_name = tag_content.substr(name_start, name_end - name_start);
        }
        
        // Convert tag name to lowercase for comparison
        std::string lower_name = tag_name;
        for (char& c : lower_name) c = std::tolower(c);
        
        if (is_closing) {
            // Find tag in stack
            int match_idx = -1;
            for (int i = (int)stack.size() - 1; i >= 0; --i) {
                if (stack[i] == lower_name) {
                    match_idx = i;
                    break;
                }
            }
            
            if (match_idx != -1) {
                // Close all tags up to match
                for (int i = (int)stack.size() - 1; i > match_idx; --i) {
                    xml += "</" + stack[i] + ">";
                }
                xml += "</" + stack[match_idx] + ">";
                stack.erase(stack.begin() + match_idx, stack.end());
            }
            // If not found, ignore the closing tag
        } else {
            // Opening tag
            bool is_void = false;
            for (const auto& vt : void_tags) {
                if (lower_name == vt) { is_void = true; break; }
            }
            
            if (is_void) {
                // Ensure it is self-closing
                if (!is_self_closing) {
                    xml += "<" + tag_content + "/>";
                } else {
                    xml += "<" + tag_content + ">";
                }
            } else {
                xml += "<" + tag_content + ">";
                if (!is_self_closing) {
                    stack.push_back(lower_name);
                }
            }
        }
        
        pos = tag_end + 1;
    }
    
    // Close remaining tags
    for (int i = (int)stack.size() - 1; i >= 0; --i) {
        xml += "</" + stack[i] + ">";
    }
    
    return "<root>" + xml + "</root>";
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

    // Try standard parsing first
    unsigned int parse_flags = pugi::parse_default | pugi::parse_fragment | pugi::parse_comments | pugi::parse_declaration | pugi::parse_doctype;
    pugi::xml_parse_result result = doc.load_string(xml.c_str(), parse_flags);

    if (!result) {
        veLogW << "XML parse failed: " << result.description() << " at offset " << result.offset << ". Trying HTML fallback...";
        
        std::string fixed_xml = fixHtmlToXml(xml);
        
        result = doc.load_string(fixed_xml.c_str(), parse_flags);
        if (!result) {
            veLogE << "Fallback XML parse also failed: " << result.description() << " at offset " << result.offset;
            return false;
        }
    }

    Node parsed("xml_import");
    // Use first real element (skip DOCTYPE, comments, etc.)
    if (auto root = doc.first_child()) {
        // Skip non-element nodes (DOCTYPE, comments, <?xml>)
        while (root && root.type() != pugi::node_element) {
            root = root.next_sibling();
        }
        if (root) {
            xmlNodeToNode(root, &parsed);
        }
    }

    node->copy(&parsed, options.auto_insert, options.auto_remove, options.auto_update);
    return true;
}

} // namespace xml
} // namespace ve
