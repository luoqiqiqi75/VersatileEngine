// terminal_util.h — shared utilities for builtin commands and terminal session
#pragma once

#include "ve/core/node.h"
#include <algorithm>
#include <string>

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

} // namespace ve::detail
