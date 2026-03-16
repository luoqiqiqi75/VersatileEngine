// ----------------------------------------------------------------------------
// convert.h
// ----------------------------------------------------------------------------
// Copyright (c) 2023-present Thilo and VersatileEngine contributors.
// Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
// See LICENSE file in the project root for full license information.
// ----------------------------------------------------------------------------
#pragma once

#include "ve/global.h"
#include "ve/core/base.h"
#include "ve/core/var.h"
#include <string>
#include <vector>
#include <cstring>
#include <type_traits>
#include <sstream>

namespace ve {

// ----------------------------------------------------------------------------
// convert<T> - 统一类型转换接口
// ----------------------------------------------------------------------------
// 设计原则：
// 1. 统一接口：所有转换都通过 convert<T> 特化
// 2. 编译期分发：使用 if constexpr 和 SFINAE，零运行时开销
// 3. 优雅降级：转换失败返回 false，不抛异常
// 4. 可扩展：用户只需特化需要的接口
// ----------------------------------------------------------------------------
template<typename T>
struct convert {
    // ========== Var 互转 ==========
    
    // T → Var
    static Var toVar(const T& v) {
        if constexpr (std::is_same_v<T, bool>) {
            return Var(v);
        } else if constexpr (std::is_same_v<T, int>) {
            return Var(v);
        } else if constexpr (std::is_same_v<T, int64_t>) {
            return Var(v);
        } else if constexpr (std::is_same_v<T, double>) {
            return Var(v);
        } else if constexpr (std::is_same_v<T, std::string>) {
            return Var(v);
        } else if constexpr (std::is_same_v<T, const char*>) {
            return Var(v);
        } else if constexpr (std::is_same_v<T, void*>) {
            return Var(v);
        } else if constexpr (std::is_copy_constructible_v<T>) {
            return Var::custom(v); // 存储为 Custom 类型（需 T 可拷贝）
        } else {
            return Var(); // 不可拷贝类型：返回 Null
        }
    }
    
    // Var → T
    static bool fromVar(const Var& v, T& out) {
        if constexpr (std::is_same_v<T, bool>) {
            out = v.toBool();
            return true;
        } else if constexpr (std::is_same_v<T, int>) {
            out = static_cast<int>(v.toInt());
            return true;
        } else if constexpr (std::is_same_v<T, int64_t>) {
            out = v.toInt();
            return true;
        }
        else if constexpr (std::is_same_v<T, double>) {
            out = v.toDouble();
            return true;
        }
        else if constexpr (std::is_same_v<T, std::string>) {
            out = v.toString();
            return true;
        }
        else if constexpr (std::is_same_v<T, void*>) {
            out = v.toPointer();
            return true;
        }
        else {
            // 尝试从 Custom 提取
            if (v.isCustom()) {
                if (auto* p = v.customPtr<T>()) {
                    out = *p;
                    return true;
                }
            }
            return false;
        }
    }
    
    // ========== string 互转（文本化）==========
    
    // T → string
    static std::string toString(const T& v) {
        if constexpr (std::is_arithmetic_v<T>) {
            return std::to_string(v);
        } else if constexpr (std::is_same_v<T, std::string>) {
            return v;
        } else if constexpr (std::is_same_v<T, const char*>) {
            return std::string(v);
        } else {
            // 默认：通过 Var 转换
            Var var = toVar(v);
            return var.toString();
        }
    }
    
    // string → T
    static bool fromString(const std::string& s, T& out) {
        if constexpr (std::is_same_v<T, std::string>) {
            out = s;
            return true;
        }
        else if constexpr (std::is_integral_v<T>) {
            try {
                if constexpr (std::is_signed_v<T>) {
                    out = static_cast<T>(std::stoll(s));
                } else {
                    out = static_cast<T>(std::stoull(s));
                }
                return true;
            } catch (...) {
                return false;
            }
        }
        else if constexpr (std::is_floating_point_v<T>) {
            try {
                out = static_cast<T>(std::stod(s));
                return true;
            } catch (...) {
                return false;
            }
        }
        else {
            // 默认：通过 Var 转换
            Var var(s);
            return fromVar(var, out);
        }
    }
    
    // ========== 二进制互转（序列化）==========
    
    // T → bytes
    static std::vector<uint8_t> toBytes(const T& v) {
        if constexpr (std::is_trivially_copyable_v<T> && sizeof(T) <= 64) {
            // 小对象直接 memcpy
            std::vector<uint8_t> bytes(sizeof(T));
            std::memcpy(bytes.data(), &v, sizeof(T));
            return bytes;
        }
        else {
            // 大对象或复杂类型：通过 string 序列化
            std::string s = toString(v);
            return std::vector<uint8_t>(s.begin(), s.end());
        }
    }
    
    // bytes → T
    static bool fromBytes(const std::vector<uint8_t>& bytes, T& out) {
        if constexpr (std::is_trivially_copyable_v<T> && sizeof(T) <= 64) {
            // 小对象直接 memcpy
            if (bytes.size() != sizeof(T)) return false;
            std::memcpy(&out, bytes.data(), sizeof(T));
            return true;
        }
        else {
            // 大对象或复杂类型：通过 string 反序列化
            std::string s(bytes.begin(), bytes.end());
            return fromString(s, out);
        }
    }
    
    // ========== Var 自转（用于嵌套结构）==========
    // 注意：Var 本身就是 Value，所以不需要单独的 Value 转换
};

// ========== 基础类型特化 ==========

// bool 特化
template<>
struct convert<bool> {
    static Var toVar(bool v) { return Var(v); }
    static bool fromVar(const Var& v, bool& out) {
        out = v.toBool();
        return true;
    }
    static std::string toString(bool v) { return v ? "true" : "false"; }
    static bool fromString(const std::string& s, bool& out) {
        if (s == "true" || s == "1" || s == "True" || s == "TRUE") {
            out = true;
            return true;
        } else if (s == "false" || s == "0" || s == "False" || s == "FALSE") {
            out = false;
            return true;
        }
        return false;
    }
    static std::vector<uint8_t> toBytes(bool v) {
        std::vector<uint8_t> bytes(1);
        bytes[0] = v ? 1 : 0;
        return bytes;
    }
    static bool fromBytes(const std::vector<uint8_t>& bytes, bool& out) {
        if (bytes.empty()) return false;
        out = bytes[0] != 0;
        return true;
    }
};

// int 特化
template<>
struct convert<int> {
    static Var toVar(int v) { return Var(v); }
    static bool fromVar(const Var& v, int& out) {
        out = static_cast<int>(v.toInt());
        return true;
    }
    static std::string toString(int v) { return std::to_string(v); }
    static bool fromString(const std::string& s, int& out) {
        try {
            out = std::stoi(s);
            return true;
        } catch (...) {
            return false;
        }
    }
    static std::vector<uint8_t> toBytes(int v) {
        std::vector<uint8_t> bytes(sizeof(int));
        std::memcpy(bytes.data(), &v, sizeof(int));
        return bytes;
    }
    static bool fromBytes(const std::vector<uint8_t>& bytes, int& out) {
        if (bytes.size() != sizeof(int)) return false;
        std::memcpy(&out, bytes.data(), sizeof(int));
        return true;
    }
};

// int64_t 特化
template<>
struct convert<int64_t> {
    static Var toVar(int64_t v) { return Var(v); }
    static bool fromVar(const Var& v, int64_t& out) {
        out = v.toInt();
        return true;
    }
    static std::string toString(int64_t v) { return std::to_string(v); }
    static bool fromString(const std::string& s, int64_t& out) {
        try {
            out = std::stoll(s);
            return true;
        } catch (...) {
            return false;
        }
    }
    static std::vector<uint8_t> toBytes(int64_t v) {
        std::vector<uint8_t> bytes(sizeof(int64_t));
        std::memcpy(bytes.data(), &v, sizeof(int64_t));
        return bytes;
    }
    static bool fromBytes(const std::vector<uint8_t>& bytes, int64_t& out) {
        if (bytes.size() != sizeof(int64_t)) return false;
        std::memcpy(&out, bytes.data(), sizeof(int64_t));
        return true;
    }
};

// double 特化
template<>
struct convert<double> {
    static Var toVar(double v) { return Var(v); }
    static bool fromVar(const Var& v, double& out) {
        out = v.toDouble();
        return true;
    }
    static std::string toString(double v) { return std::to_string(v); }
    static bool fromString(const std::string& s, double& out) {
        try {
            out = std::stod(s);
            return true;
        } catch (...) {
            return false;
        }
    }
    static std::vector<uint8_t> toBytes(double v) {
        std::vector<uint8_t> bytes(sizeof(double));
        std::memcpy(bytes.data(), &v, sizeof(double));
        return bytes;
    }
    static bool fromBytes(const std::vector<uint8_t>& bytes, double& out) {
        if (bytes.size() != sizeof(double)) return false;
        std::memcpy(&out, bytes.data(), sizeof(double));
        return true;
    }
};

// std::string 特化
template<>
struct convert<std::string> {
    static Var toVar(const std::string& v) { return Var(v); }
    static bool fromVar(const Var& v, std::string& out) {
        out = v.toString();
        return true;
    }
    static std::string toString(const std::string& v) { return v; }
    static bool fromString(const std::string& s, std::string& out) {
        out = s;
        return true;
    }
    static std::vector<uint8_t> toBytes(const std::string& v) {
        return std::vector<uint8_t>(v.begin(), v.end());
    }
    static bool fromBytes(const std::vector<uint8_t>& bytes, std::string& out) {
        out.assign(bytes.begin(), bytes.end());
        return true;
    }
};

// const char* 特化
template<>
struct convert<const char*> {
    static Var toVar(const char* v) { return Var(v); }
    static bool fromVar(const Var& v, const char*& out) {
        // const char* 不能作为输出参数，返回 false
        return false;
    }
    static std::string toString(const char* v) { return std::string(v); }
    static bool fromString(const std::string& s, const char*& out) {
        // const char* 不能作为输出参数，返回 false
        return false;
    }
    static std::vector<uint8_t> toBytes(const char* v) {
        size_t len = std::strlen(v);
        return std::vector<uint8_t>(v, v + len);
    }
    static bool fromBytes(const std::vector<uint8_t>& bytes, const char*& out) {
        // const char* 不能作为输出参数，返回 false
        return false;
    }
};

// void* 特化
template<>
struct convert<void*> {
    static Var toVar(void* v) { return Var(v); }
    static bool fromVar(const Var& v, void*& out) {
        out = v.toPointer();
        return true;
    }
    static std::string toString(void* v) {
        std::ostringstream oss;
        oss << v;
        return oss.str();
    }
    static bool fromString(const std::string& s, void*& out) {
        // 字符串无法安全转换为指针
        return false;
    }
    static std::vector<uint8_t> toBytes(void* v) {
        std::vector<uint8_t> bytes(sizeof(void*));
        std::memcpy(bytes.data(), &v, sizeof(void*));
        return bytes;
    }
    static bool fromBytes(const std::vector<uint8_t>& bytes, void*& out) {
        if (bytes.size() != sizeof(void*)) return false;
        std::memcpy(&out, bytes.data(), sizeof(void*));
        return true;
    }
};

// Var 特化（自转）
template<>
struct convert<Var> {
    static Var toVar(const Var& v) { return v; }
    static bool fromVar(const Var& v, Var& out) {
        out = v;
        return true;
    }
    static std::string toString(const Var& v) { return v.toString(); }
    static bool fromString(const std::string& s, Var& out) {
        out = Var(s);
        return true;
    }
    static std::vector<uint8_t> toBytes(const Var& v) {
        // 通过 string 序列化
        std::string s = v.toString();
        return std::vector<uint8_t>(s.begin(), s.end());
    }
    static bool fromBytes(const std::vector<uint8_t>& bytes, Var& out) {
        std::string s(bytes.begin(), bytes.end());
        out = Var(s);
        return true;
    }
};

} // namespace ve
