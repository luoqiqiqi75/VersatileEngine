// ----------------------------------------------------------------------------
// convert.h — Type conversion framework (Var-independent)
// ----------------------------------------------------------------------------
// Copyright (c) 2023-present Thilo and VersatileEngine contributors.
// Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
// See LICENSE file in the project root for full license information.
// ----------------------------------------------------------------------------
//
//  Dependency: base.h only.  Does NOT depend on var.h.
//  Include order: base.h ← convert.h ← var.h
//
//  convert::parse<From, To>   — user extension point (specialize for your types)
//  Convert<T>                 — SFINAE-dispatched: toString/fromString/toBin/fromBin
//    primary                  → unknown types (delegates to convert::parse)
//    is_numeric partial       → bool, int, int64_t, double, float, ...
//    is_string partial        → std::string, const char*
//    full specialization      → void*
//
//  Example — making MyPoint convertible:
//
//    template<> bool ve::convert::parse(const MyPoint& p, std::string& out) {
//        out = std::to_string(p.x) + "," + std::to_string(p.y);
//        return true;
//    }
//    // Now: Var(MyPoint{1,2}).toString() → "1,2"
//
#pragma once

#include "base.h"

namespace ve {

namespace convert {

template<typename From, typename To> bool parse(const From&, To&) { return false; }

} // namespace convert

// ============================================================================
// Convert<T> — primary template (unknown / user types)
// ============================================================================

template<typename T, typename Enable = void>
struct Convert {
    static std::string toString(const T& v) {
        std::string out;
        if (convert::parse(v, out)) return out;
        return std::string("[") + basic::_t_demangle(typeid(T).name()) + "]";
    }

    static bool fromString(const std::string& s, T& out) {
        return convert::parse(s, out);
    }

    static Bytes toBin(const T& v) {
        Bytes out;
        if (convert::parse(v, out)) return out;
        if constexpr (std::is_trivially_copyable_v<T> && sizeof(T) <= 64) {
            Bytes b(sizeof(T));
            std::memcpy(b.data(), &v, sizeof(T));
            return b;
        } else {
            auto s = toString(v);
            return Bytes(s.begin(), s.end());
        }
    }

    static bool fromBin(const Bytes& b, T& out) {
        if (convert::parse(b, out)) return true;
        if constexpr (std::is_trivially_copyable_v<T> && sizeof(T) <= 64) {
            if (b.size() != sizeof(T)) return false;
            std::memcpy(&out, b.data(), sizeof(T));
            return true;
        }
        std::string s(b.begin(), b.end());
        return fromString(s, out);
    }
};

// ============================================================================
// Convert<T> — numeric (arithmetic types)
// ============================================================================

template<typename T>
struct Convert<T, std::enable_if_t<basic::Meta<T>::is_numeric>> {
    static std::string toString(const T& v) {
        if constexpr (std::is_same_v<T, bool>)
            return v ? "true" : "false";
        else
            return std::to_string(v);
    }

    static bool fromString(const std::string& s, T& out) {
        if constexpr (std::is_same_v<T, bool>) {
            if (s == "true" || s == "1" || s == "True" || s == "TRUE")
                { out = true; return true; }
            if (s == "false" || s == "0" || s == "False" || s == "FALSE")
                { out = false; return true; }
            return false;
        } else if constexpr (std::is_integral_v<T>) {
            try {
                if constexpr (std::is_signed_v<T>)
                    out = static_cast<T>(std::stoll(s));
                else
                    out = static_cast<T>(std::stoull(s));
                return true;
            } catch (...) { return false; }
        } else {
            try {
                out = static_cast<T>(std::stod(s));
                return true;
            } catch (...) { return false; }
        }
    }

    static Bytes toBin(const T& v) {
        Bytes b(sizeof(T));
        std::memcpy(b.data(), &v, sizeof(T));
        return b;
    }

    static bool fromBin(const Bytes& b, T& out) {
        if (b.size() != sizeof(T)) return false;
        std::memcpy(&out, b.data(), sizeof(T));
        return true;
    }
};

// ============================================================================
// Convert<T> — string (std::string, const char*, char*)
// ============================================================================

template<typename T>
struct Convert<T, std::enable_if_t<basic::Meta<T>::is_string>> {
    static std::string toString(const T& v) { return std::string(v); }

    static bool fromString(const std::string& s, T& out) {
        if constexpr (std::is_same_v<T, std::string>) { out = s; return true; }
        return false;
    }

    static Bytes toBin(const T& v) {
        std::string s(v);
        return Bytes(s.begin(), s.end());
    }

    static bool fromBin(const Bytes& b, T& out) {
        if constexpr (std::is_same_v<T, std::string>) {
            out.assign(b.begin(), b.end());
            return true;
        }
        return false;
    }
};

// ============================================================================
// Convert<void*>
// ============================================================================

template<>
struct Convert<void*> {
    static std::string toString(void* v) {
        std::ostringstream oss; oss << v; return oss.str();
    }
    static bool fromString(const std::string&, void*&) { return false; }

    static Bytes toBin(void* v) {
        Bytes b(sizeof(void*));
        std::memcpy(b.data(), &v, sizeof(void*));
        return b;
    }
    static bool fromBin(const Bytes& b, void*& out) {
        if (b.size() != sizeof(void*)) return false;
        std::memcpy(&out, b.data(), sizeof(void*));
        return true;
    }
};

} // namespace ve
