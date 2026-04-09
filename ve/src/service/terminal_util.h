// terminal_util.h — shared utilities for builtin commands and terminal session
#pragma once

#include "ve/core/node.h"
#include "../core/parse_util.h"

namespace ve::detail {

// Re-export parse utilities for backward compat with terminal_session.cpp
using ve::parse::isInt;
using ve::parse::isDouble;
using ve::parse::Flags;
using ve::parse::parseFlags;

inline Var parseVar(const std::string& raw) { return parse::parseValue(raw); }

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
