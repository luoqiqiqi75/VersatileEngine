// ----------------------------------------------------------------------------
// var.h
// ----------------------------------------------------------------------------
// Copyright (c) 2023-present Thilo and VersatileEngine contributors.
// Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
// See LICENSE file in the project root for full license information.
// ----------------------------------------------------------------------------
#pragma once

#include "ve/global.h"
#include "ve/core/base.h"

namespace ve {
    
template<typename T>
struct convert;

// ----------------------------------------------------------------------------
// Var - 高性能通用数据类型，替代 QVariant 和 Value
// ----------------------------------------------------------------------------
// 设计目标：
// 1. 基础类型（bool, int, double, string, void*）零开销存储
// 2. 支持 Array 和 Object（类似 JSON）
// 3. 通过 convert<T> 支持任意自定义类型转换
// 4. 非 Object，纯数据类型，不包含信号/槽机制
// ----------------------------------------------------------------------------
class VE_API Var {
public:
    using ListV = ve::Vector<Var>;
    using DictV = ve::Dict<Var>;
    
    enum Type : uint8_t {
        Null,           // 空值

        Bool,           // bool
        Int,            // int (int64_t)
        Double,         // double
        String,          // std::string
        Bin,          // Bytes（二进制数据）

        List,          // Vector<Var>（数组）
        Dict,         // Dict<Var>（对象，有序哈希表）

        Pointer         // void*
    };

    // ========== 构造与赋值 ==========

    Var();

    Var(bool v);
    Var(int v);
    Var(std::int64_t v);
    Var(double v);
    Var(const char* v);
    Var(const std::string& v);
    Var(std::string&& v);
    Var(const Bytes& v);
    Var(Bytes&& v);
    Var(void* ptr);

    Var(const ListV& v);
    Var(ListV&& v);
    Var(const DictV& v);
    Var(DictV&& v);
    
    // 模板构造：支持 ListLike 和 DictLike 类型
    template<typename T>
    Var(const T& v) {
        if constexpr (is_list_like_v<T>) {
            _type = List;
            new (&_storage._list) ListV();
            auto& lv = ref<List, ListV>();
            lv.reserve(v.size());
            for (const auto& item : v) {
                lv.push_back(Var(item));
            }
        } else if constexpr (is_dict_like_v<T>) {
            _type = Dict;
            new (&_storage._dict) DictV();
            auto& dv = ref<Dict, DictV>();
            for (const auto& kv : v) {
                using KVAccess = typename T::KVAccessT;
                dv[KVAccess::key(kv)] = Var(KVAccess::value(kv));
            }
        } else {
            *this = convert<T>::toVar(v); // 通过 convert<T> 转换
        }
    }

    Var(const Var& other);
    Var(Var&& other) noexcept;
    Var& operator=(const Var& other);
    Var& operator=(Var&& other) noexcept;
    ~Var();
    
    // ========== 类型查询 ==========
    
    Type type() const { return _type; }
    bool isNull() const { return _type == Null; }
    bool is(const Type t) const { return _type == t; }

    bool isBool() const { return _type == Bool; }
    bool isInt() const { return _type == Int; }
    bool isDouble() const { return _type == Double; }
    bool isString() const { return _type == String; }
    bool isBytes() const { return _type == Bin; }
    bool isList() const { return _type == List; }
    bool isDict() const { return _type == Dict; }
    bool isPointer() const { return _type == Pointer; }
    
    // ========== 取值（类型安全）==========
    
    // 基础类型取值（类型不匹配返回默认值）
    bool toBool(bool def = false) const;
    int toInt(int def = -1) const;
    // std::int64_t toLong(std::int64_t def = -1) const;
    double toDouble(double def = 0.0) const;
    std::string toString(const std::string& def = "") const;
    Bytes toBin() const;
    
    const ListV& toList() const;
    ListV& toList();
    const DictV& toDict() const;
    DictV& toDict();
    void* toPointer() const;
    
    // 通用转换（通过 convert<T>）
    template<typename T>
    T to(const T& def = T{}) const {
        T out;
        if (convert<T>::fromVar(*this, out)) return out;
        return def;
    }
    
    // as<T>() — 快速类型提取（信号参数拆包用）
    template<typename T>
    std::decay_t<T> as() const {
        using U = std::decay_t<T>;
        if constexpr (std::is_same_v<U, Var>)               return *this;
        else if constexpr (std::is_same_v<U, bool>)          return toBool();
        else if constexpr (std::is_same_v<U, int>)           return toInt();
        else if constexpr (std::is_integral_v<U>)             return static_cast<U>(toInt());
        else if constexpr (std::is_floating_point_v<U>)       return static_cast<U>(toDouble());
        else if constexpr (std::is_same_v<U, std::string>)    return toString();
        else if constexpr (std::is_pointer_v<U>)              return static_cast<U>(toPointer());
        else                                                   return to<U>();
    }
    
    // operator[] — 按下标访问 List 元素（越界返回 Null Var）
    const Var& operator[](size_t index) const;
    
    // ========== 赋值（类型安全）==========
    
    // 基础类型赋值
    Var& fromBool(bool v);
    Var& fromInt(int v);
    Var& fromInt64(std::int64_t v);
    Var& fromDouble(double v);
    Var& fromString(const char* v);
    Var& fromString(const std::string& v);
    Var& fromString(std::string&& v);
    Var& fromBin(const Bytes& v);
    Var& fromBin(Bytes&& v);
    Var& fromList(const ListV& v);
    Var& fromList(ListV&& v);
    Var& fromDict(const DictV& v);
    Var& fromDict(DictV&& v);
    Var& fromPointer(void* ptr);
    
    // 通用赋值（通过 convert<T>）
    template<typename T>
    Var& from(const T& v) {
        *this = convert<T>::toVar(v);
        return *this;
    }
    
    // ========== 比较操作 ==========
    
    bool operator==(const Var& other) const;
    bool operator!=(const Var& other) const { return !(*this == other); }
    
    // ========== 高性能线程安全 swap ==========
    
    // swap 方法：原子交换两个 Var 的值（线程安全）
    // 性能：O(1) 对于基础类型，O(n) 对于复杂类型（但避免拷贝）
    void swap(Var& other) noexcept;
    
    // ========== 调试输出 ==========
    
    friend std::ostream& operator<<(std::ostream& os, const Var& v);

private:
    template<Type T, typename V> V& ref();
    template<Type T, typename V> const V& ref() const;

    void copyFrom(const Var& other);
    void moveFrom(Var&& other);
    void destroy();

private:
    Type _type;

    union Storage {
        bool _bool;
        int64_t _int;
        double _double;
        void* _pointer;
        std::aligned_storage_t<sizeof(std::string), alignof(std::string)> _str;
        std::aligned_storage_t<sizeof(Bytes), alignof(Bytes)> _bin;
        std::aligned_storage_t<sizeof(ListV), alignof(ListV)> _list;
        std::aligned_storage_t<sizeof(DictV), alignof(DictV)> _dict;
    } _storage;
};

} // namespace ve
