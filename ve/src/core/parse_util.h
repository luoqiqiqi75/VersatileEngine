// parse_util.h — shared parsing utilities for terminal and service adapters
//
// Single implementation of flag parsing, value parsing, and string type detection.
// Used by terminal_util.h and service-side command adapters.
#pragma once

#include "ve/core/var.h"

namespace ve::parse {

inline bool isInt(const std::string& s)
{
    if (s.empty()) return false;
    size_t start = (s[0] == '-' || s[0] == '+') ? 1 : 0;
    if (start >= s.size()) return false;
    return std::all_of(s.begin() + start, s.end(),
                       [](unsigned char c) { return std::isdigit(c); });
}

inline bool isDouble(const std::string& s)
{
    if (s.empty()) return false;
    bool dot = false;
    size_t start = (s[0] == '-' || s[0] == '+') ? 1 : 0;
    if (start >= s.size()) return false;
    for (size_t i = start; i < s.size(); ++i) {
        if (s[i] == '.') { if (dot) return false; dot = true; }
        else if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    }
    return dot;
}

inline Var parseValue(const std::string& raw)
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

inline Var parseValueAs(const std::string& raw, Var::Type type)
{
    try {
        switch (type) {
            case Var::NONE:   return parseValue(raw);
            case Var::BOOL:
                if (raw == "true" || raw == "1" || raw == "yes" || raw == "on") return Var(true);
                if (raw == "false" || raw == "0" || raw == "no" || raw == "off") return Var(false);
                return Var(!raw.empty());
            case Var::INT:    return Var(static_cast<std::int64_t>(std::stoll(raw)));
            case Var::DOUBLE: return Var(std::stod(raw));
            case Var::STRING: return Var(raw);
            default:          return parseValue(raw);
        }
    } catch (...) {
        return parseValue(raw);
    }
}

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

} // namespace ve::parse
