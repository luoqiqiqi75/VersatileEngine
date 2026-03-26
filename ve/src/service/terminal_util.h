// terminal_util.h — shared utilities for builtin commands and terminal session
#pragma once

#include "ve/core/node.h"
#include <algorithm>
#include <string>
#include <vector>

namespace ve::detail {

inline bool isInt(const std::string& s)
{
    if (s.empty()) return false;
    size_t start = (s[0] == '-' || s[0] == '+') ? 1 : 0;
    if (start >= s.size()) return false;
    return std::all_of(s.begin() + start, s.end(), ::isdigit);
}

inline bool isDouble(const std::string& s)
{
    if (s.empty()) return false;
    bool dot = false;
    size_t start = (s[0] == '-' || s[0] == '+') ? 1 : 0;
    if (start >= s.size()) return false;
    for (size_t i = start; i < s.size(); ++i) {
        if (s[i] == '.') { if (dot) return false; dot = true; }
        else if (!std::isdigit((unsigned char)s[i])) return false;
    }
    return dot;
}

inline const char* varTypeName(Var::Type t)
{
    switch (t) {
        case Var::NONE:    return "Null";
        case Var::BOOL:    return "Bool";
        case Var::INT:     return "Int";
        case Var::DOUBLE:  return "Double";
        case Var::STRING:  return "String";
        case Var::BIN:     return "Bin";
        case Var::LIST:    return "List";
        case Var::DICT:    return "Dict";
        case Var::POINTER: return "Pointer";
        case Var::CUSTOM:  return "Custom";
        default:           return "?";
    }
}

inline Var parseVar(const std::string& raw)
{
    if (raw == "null")  return Var();
    if (raw == "true")  return Var(true);
    if (raw == "false") return Var(false);
    if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"')
        return Var(raw.substr(1, raw.size() - 2));
    if (isInt(raw))    return Var(static_cast<std::int64_t>(std::stoll(raw)));
    if (isDouble(raw)) return Var(std::stod(raw));
    return Var(raw);
}

inline std::string varPreview(const Var& v, size_t max_len = 60)
{
    std::string s = v.toString();
    if (s.size() > max_len) s = s.substr(0, max_len - 3) + "...";
    return s;
}

inline std::string nodeSummary(const Node* n)
{
    if (!n) return "(null)";
    return n->name().empty() ? "(anon)" : n->name();
}

// ============================================================================
// Flags parser — lightweight POSIX/GNU flag extraction
// ============================================================================

struct Flags {
    std::vector<std::pair<std::string, std::string>> named;
    std::vector<std::string> positional;

    bool has(const std::string& longName, char shortName = 0) const {
        for (auto& [k, _] : named)
            if (k == longName || (shortName && k.size() == 1 && k[0] == shortName))
                return true;
        return false;
    }
    std::string get(const std::string& longName, char shortName = 0, const std::string& def = "") const {
        for (auto& [k, v] : named)
            if (k == longName || (shortName && k.size() == 1 && k[0] == shortName))
                return v.empty() ? def : v;
        return def;
    }

    std::string pos(int idx) const {
        return (idx >= 0 && idx < (int)positional.size()) ? positional[idx] : std::string{};
    }
    int posCount() const { return (int)positional.size(); }
};

inline Flags parseFlags(const std::vector<std::string>& args, int startIdx = 1)
{
    Flags f;
    bool endOfFlags = false;
    for (size_t i = startIdx; i < args.size(); ++i) {
        auto& a = args[i];
        if (endOfFlags) {
            f.positional.push_back(a);
            continue;
        }
        if (a == "--") {
            endOfFlags = true;
            continue;
        }
        if (a.size() > 2 && a[0] == '-' && a[1] == '-') {
            auto eq = a.find('=', 2);
            if (eq != std::string::npos)
                f.named.push_back({a.substr(2, eq - 2), a.substr(eq + 1)});
            else if (i + 1 < args.size() && !args[i + 1].empty() && args[i + 1][0] != '-')
                f.named.push_back({a.substr(2), args[++i]});
            else
                f.named.push_back({a.substr(2), ""});
        } else if (a.size() > 1 && a[0] == '-' && !isInt(a) && !isDouble(a)) {
            for (size_t j = 1; j < a.size(); ++j) {
                std::string key(1, a[j]);
                if (j == a.size() - 1 && i + 1 < args.size() && !args[i + 1].empty() && args[i + 1][0] != '-')
                    f.named.push_back({key, args[++i]});
                else
                    f.named.push_back({key, ""});
            }
        } else {
            f.positional.push_back(a);
        }
    }
    return f;
}

} // namespace ve::detail
