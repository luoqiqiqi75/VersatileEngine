// ----------------------------------------------------------------------------
// json.cpp — JSON serialization for Var and Node (simdjson parse + C++ stringify)
// ----------------------------------------------------------------------------
#include "ve/core/impl/json.h"
#include "ve/core/node.h"

#include <simdjson.h>

namespace ve::impl::json {

using JsonOpts = ExportOpts;

// ============================================================================
// Stringify helpers (pure C++)
// ============================================================================
static void nodeToJsonImpl(const Node* node, std::string& out, const JsonOpts& options, int depth, int currentDepth);

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

static void doubleToJson(double d, std::string& out, const JsonOpts& options)
{
    if (!std::isfinite(d)) { out += "null"; return; }
    if (std::isfinite(options.zero_epsilon) && options.zero_epsilon > 0.0
        && std::abs(d) <= options.zero_epsilon) {
        out += '0';
        return;
    }

    // Fixed notation would expand large values (1e300 -> 300+ digits) while their
    // fractional digits carry no information; keep shortest output for them.
    // Fixed output is then at most sign + 16 integer digits + '.' + 324 fractional digits.
    char buf[384];
    const int places = std::abs(d) < 1e15 ? std::min(options.decimal_places, 324) : -1;
    auto result = places < 0
        ? std::to_chars(buf, buf + sizeof(buf), d)
        : std::to_chars(buf, buf + sizeof(buf), d, std::chars_format::fixed, places);
    if (result.ec != std::errc{}) { out += "null"; return; }

    char* end = result.ptr;
    if (places > 0) {
        while (end > buf && end[-1] == '0') { --end; }
        if (end > buf && end[-1] == '.') --end;
    }
    if (places >= 0 && end - buf == 2 && buf[0] == '-' && buf[1] == '0') {
        out += '0';
        return;
    }
    out.append(buf, end - buf);
}

static void varToJson(const Var& v, std::string& out, const JsonOpts& options)
{
    switch (v.type()) {
        case Var::NONE:    out += "null"; break;
        case Var::BOOL:    out += v.toBool() ? "true" : "false"; break;
        case Var::INT:     out += std::to_string(v.toInt64()); break;
        case Var::DOUBLE:  doubleToJson(v.toDouble(), out, options); break;
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
                varToJson(list[i], out, options);
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
                escape(kv.first, out);
                out += "\":";
                varToJson(kv.second, out, options);
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

static bool isIgnoredChild(const Node* child, const JsonOpts& options)
{
    return options.auto_ignore
        && child
        && !child->name().empty()
        && child->name()[0] == '_';
}

// Emits `nl` between lines and indent-based padding only when `nl` is non-empty
// (padding without line breaks would just bloat single-line output).
static void nodeToJsonImpl(const Node* node, std::string& out, const JsonOpts& options, int depth, int currentDepth)
{
    const std::string& nl = options.newline;
    const int indent = options.indent;
    auto padding = [&](int level) {
        return nl.empty() ? std::string() : std::string(level * indent, ' ');
    };
    const std::string pad  = padding(depth);
    const std::string pad1 = padding(depth + 1);

    auto all_children = node->children();
    Vector<const Node*> visible_children;
    visible_children.reserve(all_children.sizeAsInt());
    for (auto* child : all_children) {
        if (!isIgnoredChild(child, options))
            visible_children.push_back(child);
    }

    const int nch = visible_children.sizeAsInt();

    auto emitValue = [&]() {
        if (!node->get().isNull()) {
            varToJson(node->get(), out, options);
        } else {
            out += "null";
        }
    };

    // Depth limit reached: output truncation marker
    if (options.max_depth >= 0 && currentDepth >= options.max_depth) {
        if (nch == 0) emitValue();
        else          out += "\"...\"";
        return;
    }

    if (nch == 0) {
        emitValue();
        return;
    }

    // First child has empty name => JSON array (all children as ordered elements).
    if (visible_children[0]->name().empty()) {
        out += "[" + nl;
        int i = 0;
        for (auto* c : visible_children) {
            out += pad1;
            nodeToJsonImpl(c, out, options, depth + 1, currentDepth + 1);
            if (++i < nch) out += ",";
            out += nl;
        }
        out += pad + "]";
        return;
    }

    const bool hasVal = !node->get().isNull();
    out += "{" + nl;
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
        varToJson(node->get(), out, options);
        if (++fieldIdx < totalFields) out += ",";
        out += nl;
    }

    for (auto& nm : names) {
        auto& group = named_groups[nm];
        out += pad1 + "\"";
        escape(nm, out);
        out += "\": ";
        nodeToJsonImpl(group[0], out, options, depth + 1, currentDepth + 1);
        if (++fieldIdx < totalFields) out += ",";
        out += nl;
    }

    if (anonCnt > 0) {
        out += pad1 + "\"\": [" + nl;
        const std::string pad2 = padding(depth + 2);
        for (int i = 0; i < anonCnt; ++i) {
            out += pad2;
            nodeToJsonImpl(anonymous_children[i], out, options, depth + 2, currentDepth + 1);
            if (i + 1 < anonCnt) out += ",";
            out += nl;
        }
        out += pad1 + "]" + nl;
    }

    out += pad + "}";
}

// ============================================================================
// Public stringify API
// ============================================================================

std::string stringify(const Var& v)
{
    std::string out;
    varToJson(v, out, JsonOpts{});
    return out;
}

std::string exportTree(const Node* node, const ExportOpts& opts)
{
    if (!node) return "null";
    std::string out;
    out.reserve(256);
    nodeToJsonImpl(node, out, opts, 0, 0);
    out += opts.tail;
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

static void domToNode(simdjson::ondemand::value val, Node* node);

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

static void importObjectField(Node* node, const std::string& key, simdjson::ondemand::value child_val)
{
    if (key == "_value") {
        node->set(domToVar(child_val));
        return;
    }

    if (key.empty()) {
        clearAnonymousChildren(node);
        auto ct = child_val.type().value();
        if (ct == simdjson::ondemand::json_type::array) {
            for (auto elem : child_val.get_array()) {
                auto* c = node->append();
                domToNode(elem.value(), c);
            }
        }
        return;
    }

    auto* c = ensureSchemaChild(node, key);
    if (!c) return;

    auto ct = child_val.type().value();
    if (ct == simdjson::ondemand::json_type::array) {
        for (auto elem : child_val.get_array()) {
            auto* slot = c->append();
            domToNode(elem.value(), slot);
        }
        return;
    }

    domToNode(child_val, c);
}

static void domToNode(simdjson::ondemand::value val, Node* node)
{
    auto t = val.type().value();
    if (t == simdjson::ondemand::json_type::object) {
        for (auto field : val.get_object()) {
            importObjectField(node, std::string(field.unescaped_key().value()), field.value());
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

    try {
        simdjson::ondemand::parser parser;
        simdjson::padded_string padded(json);
        auto doc = parser.iterate(padded);
        if (doc.error()) return Var();

        simdjson::ondemand::value val;
        auto err = doc.get_value().get(val);
        if (!err) return domToVar(val);

        // Fallback: simdjson ondemand may not support bare values
        std::string s = json;
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
        if (s.empty()) return Var();
        if (s == "null") return Var();
        if (s == "true") return Var(true);
        if (s == "false") return Var(false);
        if (s.front() == '"' && s.back() == '"' && s.size() >= 2) {
            return Var(s.substr(1, s.size() - 2));
        }
        try {
            if (s.find('.') != std::string::npos || s.find('e') != std::string::npos || s.find('E') != std::string::npos) {
                return Var(std::stod(s));
            }
            return Var(static_cast<int64_t>(std::stoll(s)));
        } catch (...) {}
        return Var();
    } catch (...) {
        // simdjson's on-demand API may report malformed trailing structure via
        // exceptions from value()/iteration. Never let a third-party parser
        // exception cross the JSON adapter boundary.
        return Var();
    }
}

bool importTree(Node* node, const std::string& json)
{
    if (!node || json.empty()) return false;

    try {
        simdjson::ondemand::parser parser;
        simdjson::padded_string padded(json);
        auto doc = parser.iterate(padded);
        if (doc.error()) return false;

        simdjson::ondemand::value val;
        auto err = doc.get_value().get(val);
        if (err) return false;

        Node parsed("json_import");
        domToNode(val, &parsed);
        node->copy(&parsed);
        return true;
    } catch (...) {
        return false;
    }
}

bool importTree(Node* node, const std::string& json, int copy_flags)
{
    if (!node || json.empty()) return false;

    try {
        simdjson::ondemand::parser parser;
        simdjson::padded_string padded(json);
        auto doc = parser.iterate(padded);
        if (doc.error()) return false;

        simdjson::ondemand::value val;
        auto err = doc.get_value().get(val);
        if (err) return false;

        Node parsed("json_import");
        domToNode(val, &parsed);
        node->copy(&parsed, copy_flags);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace ve::json
