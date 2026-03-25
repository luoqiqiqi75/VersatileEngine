// ----------------------------------------------------------------------------
// json.cpp — JSON serialization for Var and Node (simdjson parse + C++ stringify)
// ----------------------------------------------------------------------------
#include "ve/core/impl/json.h"
#include "ve/core/node.h"

#include <simdjson.h>

namespace ve::impl::json {

// ============================================================================
// Stringify helpers (pure C++)
// ============================================================================

static void escape(const std::string& s, std::string& out)
{
    out.reserve(out.size() + s.size() + 2);
    for (char c : s) {
        switch (c) {
            case '\"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", (unsigned)c);
                    out += buf;
                } else {
                    out += c;
                }
                break;
        }
    }
}

static void varToJson(const Var& v, std::string& out)
{
    switch (v.type()) {
        case Var::NONE:    out += "null"; break;
        case Var::BOOL:    out += v.toBool() ? "true" : "false"; break;
        case Var::INT:     out += std::to_string(v.toInt64()); break;
        case Var::DOUBLE: {
            double d = v.toDouble();
            if (std::isnan(d))       { out += "null"; break; }
            if (std::isinf(d))       { out += "null"; break; }
            char buf[32];
            int n = snprintf(buf, sizeof(buf), "%.17g", d);
            out.append(buf, n);
            break;
        }
        case Var::STRING:
            out += '"';
            escape(v.toString(), out);
            out += '"';
            break;
        case Var::BIN: {
            auto bytes = v.toBin();
            out += '"';
            escape(std::string(bytes.begin(), bytes.end()), out);
            out += '"';
            break;
        }
        case Var::LIST: {
            auto& list = v.toList();
            out += '[';
            for (size_t i = 0; i < list.size(); ++i) {
                if (i) out += ',';
                varToJson(list[i], out);
            }
            out += ']';
            break;
        }
        case Var::DICT: {
            auto& dict = v.toDict();
            out += '{';
            bool first = true;
            for (auto& kv : dict) {
                if (!first) out += ',';
                first = false;
                out += '"';
                escape(kv.key, out);
                out += "\":";
                varToJson(kv.value, out);
            }
            out += '}';
            break;
        }
        default:
            out += '"';
            escape(v.toString(), out);
            out += '"';
            break;
    }
}

static bool isIgnoredChild(const Node* child, const schema::ExportOptions& options)
{
    return options.auto_ignore
        && child
        && !child->name().empty()
        && child->name()[0] == '_';
}

static void nodeToJsonImpl(const Node* node, std::string& out, const schema::ExportOptions& options, int depth)
{
    const int indent = options.indent;
    std::string pad(depth * indent, ' ');
    std::string pad1((depth + 1) * indent, ' ');

    Vector<const Node*> visible_children;
    visible_children.reserve(node->count());
    for (auto* child : *node) {
        if (!isIgnoredChild(child, options))
            visible_children.push_back(child);
    }

    const int nch = visible_children.sizeAsInt();
    if (nch == 0) {
        if (!node->get().isNull()) {
            varToJson(node->get(), out);
        } else {
            out += "null";
        }
        return;
    }

    // First child has empty name => JSON array (all children as ordered elements).
    if (visible_children[0]->name().empty()) {
        out += "[\n";
        int i = 0;
        for (auto* c : visible_children) {
            out += pad1;
            nodeToJsonImpl(c, out, options, depth + 1);
            if (++i < nch) {
                out += ",";
            }
            out += "\n";
        }
        out += pad + "]";
        return;
    }

    const bool hasVal = !node->get().isNull();
    out += "{\n";
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

    const int anonCnt = anonymous_children.sizeAsInt();

    int fieldIdx = 0;
    int totalFields = (int)names.size() + (anonCnt > 0 ? 1 : 0) + (hasVal ? 1 : 0);

    if (hasVal) {
        out += pad1 + "\"_value\": ";
        varToJson(node->get(), out);
        if (++fieldIdx < totalFields) out += ",";
        out += "\n";
    }

    for (auto& nm : names) {
        auto& group = named_groups[nm];
        int cnt = group.sizeAsInt();
        out += pad1 + "\"";
        escape(nm, out);
        out += "\": ";
        if (cnt == 1) {
            nodeToJsonImpl(group[0], out, options, depth + 1);
        } else {
            out += "[\n";
            std::string pad2((depth + 2) * indent, ' ');
            for (int i = 0; i < cnt; ++i) {
                out += pad2;
                nodeToJsonImpl(group[i], out, options, depth + 2);
                if (i + 1 < cnt) out += ",";
                out += "\n";
            }
            out += pad1 + "]";
        }
        if (++fieldIdx < totalFields) out += ",";
        out += "\n";
    }

    if (anonCnt > 0) {
        out += pad1 + "\"\": [\n";
        std::string pad2((depth + 2) * indent, ' ');
        for (int i = 0; i < anonCnt; ++i) {
            out += pad2;
            nodeToJsonImpl(anonymous_children[i], out, options, depth + 2);
            if (i + 1 < anonCnt) out += ",";
            out += "\n";
        }
        out += pad1 + "]\n";
    }

    out += pad + "}";
}

// ============================================================================
// Public stringify API
// ============================================================================

std::string stringify(const Var& v)
{
    std::string out;
    varToJson(v, out);
    return out;
}

std::string exportTree(const Node* node, int indent)
{
    schema::ExportOptions options;
    options.indent = indent;
    return exportTree(node, options);
}

std::string exportTree(const Node* node, const schema::ExportOptions& options)
{
    if (!node) return "null";
    std::string out;
    out.reserve(256);
    nodeToJsonImpl(node, out, options, 0);
    out += "\n";
    return out;
}

// ============================================================================
// Parse helpers (simdjson on-demand)
// ============================================================================

static Var domToVar(simdjson::ondemand::value val)
{
    auto t = val.type().value();
    switch (t) {
        case simdjson::ondemand::json_type::null:
            val.is_null().value();
            return Var();
        case simdjson::ondemand::json_type::boolean:
            return Var(val.get_bool().value());
        case simdjson::ondemand::json_type::number: {
            if (val.is_integer().value()) {
                return Var(static_cast<int64_t>(val.get_int64().value()));
            }
            return Var(val.get_double().value());
        }
        case simdjson::ondemand::json_type::string:
            return Var(std::string(val.get_string().value()));
        case simdjson::ondemand::json_type::array: {
            Var::ListV list;
            for (auto elem : val.get_array()) {
                list.push_back(domToVar(elem.value()));
            }
            return Var(std::move(list));
        }
        case simdjson::ondemand::json_type::object: {
            Var::DictV dict;
            for (auto field : val.get_object()) {
                std::string key(field.unescaped_key().value());
                dict[key] = domToVar(field.value());
            }
            return Var(std::move(dict));
        }
        default:
            return Var();
    }
}

static void domToNode(simdjson::ondemand::value val, Node* node)
{
    auto t = val.type().value();
    if (t == simdjson::ondemand::json_type::object) {
        for (auto field : val.get_object()) {
            std::string key(field.unescaped_key().value());
            simdjson::ondemand::value child_val = field.value();

            if (key == "_value") {
                node->set(domToVar(child_val));
                continue;
            }

            if (key.empty()) {
                auto ct = child_val.type().value();
                if (ct == simdjson::ondemand::json_type::array) {
                    for (auto elem : child_val.get_array()) {
                        auto* c = node->append();
                        domToNode(elem.value(), c);
                    }
                }
                continue;
            }

            auto ct = child_val.type().value();
            if (ct == simdjson::ondemand::json_type::array) {
                // JSON object keys are unique: one child named `key`, array elements as
                // anonymous children underneath (e.g. plugins/#0/path, not plugins#0/path).
                auto* container = node->append(key);
                for (auto elem : child_val.get_array()) {
                    auto* slot = container->append();
                    domToNode(elem.value(), slot);
                }
            } else {
                auto* c = node->append(key);
                domToNode(child_val, c);
            }
        }
    } else if (t == simdjson::ondemand::json_type::array) {
        for (auto elem : val.get_array()) {
            auto* c = node->append();
            domToNode(elem.value(), c);
        }
    } else {
        node->set(domToVar(val));
    }
}

// ============================================================================
// Public parse API
// ============================================================================

Var parse(const std::string& json)
{
    if (json.empty()) return Var();

    simdjson::ondemand::parser parser;
    simdjson::padded_string padded(json);
    auto doc = parser.iterate(padded);
    if (doc.error()) return Var();

    simdjson::ondemand::value val;
    auto err = doc.get_value().get(val);
    if (err) return Var();

    return domToVar(val);
}

bool importTree(Node* node, const std::string& json)
{
    if (!node || json.empty()) return false;

    simdjson::ondemand::parser parser;
    simdjson::padded_string padded(json);
    auto doc = parser.iterate(padded);
    if (doc.error()) return false;

    simdjson::ondemand::value val;
    auto err = doc.get_value().get(val);
    if (err) return false;

    domToNode(val, node);
    return true;
}

bool importTree(Node* node, const std::string& json, const schema::ImportOptions& options)
{
    if (!node || json.empty()) return false;

    simdjson::ondemand::parser parser;
    simdjson::padded_string padded(json);
    auto doc = parser.iterate(padded);
    if (doc.error()) return false;

    simdjson::ondemand::value val;
    auto err = doc.get_value().get(val);
    if (err) return false;

    Node parsed("json_import");
    domToNode(val, &parsed);
    node->copy(&parsed, options.auto_insert, options.auto_remove, options.auto_replace);
    return true;
}

} // namespace ve::json
