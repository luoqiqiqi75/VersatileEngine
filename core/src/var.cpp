// ----------------------------------------------------------------------------
// var.cpp
// ----------------------------------------------------------------------------
// Copyright (c) 2023-present Thilo and VersatileEngine contributors.
// Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
// See LICENSE file in the project root for full license information.
// ----------------------------------------------------------------------------
#include "ve/core/var.h"
#include "ve/core/convert.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ve {

// ========== 构造与赋值 ==========

Var::Var() : _type(Null), _storage{} { _storage._int = 0; }
Var::Var(bool v) : _type(Bool), _storage{} { _storage._bool = v; }
Var::Var(int v) : _type(Int) { _storage._int = static_cast<int64_t>(v); }
Var::Var(std::int64_t v) : _type(Int) { _storage._int = v; }
Var::Var(double v) : _type(Double) { _storage._double = v; }
Var::Var(const char* v) : _type(String) { _storage._str = new std::string(v); }
Var::Var(const std::string& v) : _type(String) { _storage._str = new std::string(v); }
Var::Var(std::string&& v) : _type(String) { _storage._str = new std::string(std::move(v)); }
Var::Var(const Bytes& v) : _type(Bin) { _storage._bin = new Bytes(v); }
Var::Var(Bytes&& v) : _type(Bin) { _storage._bin = new Bytes(std::move(v)); }
Var::Var(void* ptr) : _type(Pointer) { _storage._pointer = ptr; }

Var::Var(const ListV& v) : _type(List) { _storage._list = new ListV(v); }
Var::Var(ListV&& v) : _type(List) { _storage._list = new ListV(std::move(v)); }
Var::Var(const DictV& v) : _type(Dict) { _storage._dict = new DictV(v); }
Var::Var(DictV&& v) : _type(Dict) { _storage._dict = new DictV(std::move(v)); }

Var::Var(const Var& other) : _type(other._type) { copyFrom(other); }
Var::Var(Var&& other) noexcept : _type(other._type) { moveFrom(std::move(other)); }

Var& Var::operator=(const Var& other) {
    if (this == &other) return *this;
    destroy();
    _type = other._type;
    copyFrom(other);
    return *this;
}

Var& Var::operator=(Var&& other) noexcept {
    if (this == &other) return *this;
    destroy();
    _type = other._type;
    moveFrom(std::move(other));
    return *this;
}

Var::~Var() {
    destroy();
}

// ========== 私有辅助方法 ==========

template<> std::string& Var::ref<Var::String, std::string>() { return *_storage._str; }
template<> const std::string& Var::ref<Var::String, std::string>() const { return *_storage._str; }
template<> Bytes& Var::ref<Var::Bin, Bytes>() { return *_storage._bin; }
template<> const Bytes& Var::ref<Var::Bin, Bytes>() const { return *_storage._bin; }

template<> Var::ListV& Var::ref<Var::List, Var::ListV>() { return *_storage._list; }
template<> const Var::ListV& Var::ref<Var::List, Var::ListV>() const { return *_storage._list; }
template<> Var::DictV& Var::ref<Var::Dict, Var::DictV>() { return *_storage._dict; }
template<> const Var::DictV& Var::ref<Var::Dict, Var::DictV>() const { return *_storage._dict; }

template<> Var::CustomV& Var::ref<Var::Custom, Var::CustomV>() { return *_storage._custom; }
template<> const Var::CustomV& Var::ref<Var::Custom, Var::CustomV>() const { return *_storage._custom; }

// ========== 类型查询 (Custom) ==========

const std::type_info& Var::customType() const {
    if (_type != Custom) return typeid(void);
    const auto& a = ref<Custom, CustomV>();
    return a.has_value() ? a.type() : typeid(void);
}

bool Var::customIs(const std::type_info& ti) const {
    if (_type != Custom) return false;
    return ref<Custom, CustomV>().type() == ti;
}

// ========== 取值（类型安全）==========

bool Var::toBool(bool def) const {
    if (_type == Bool) {
        return _storage._bool;
    } else if (_type == Int) {
        return _storage._int != 0;
    } else if (_type == Double) {
        return _storage._double != 0.0;
    } else if (_type == String) {
        const auto& s = *_storage._str;
        return s == "true" || s == "1" || !s.empty();
    }
    return def;
}

int Var::toInt(int def) const {
    if (_type == Int) {
        return static_cast<int>(_storage._int);
    } else if (_type == Bool) {
        return _storage._bool ? 1 : 0;
    } else if (_type == Double) {
        return static_cast<int>(_storage._double);
    } else if (_type == String) {
        try {
            return std::stoi(*_storage._str);
        } catch (...) {
            return def;
        }
    }
    return def;
}

std::int64_t Var::toInt64(std::int64_t def) const {
    if (_type == Int) {
        return _storage._int;
    } else if (_type == Bool) {
        return _storage._bool ? 1 : 0;
    } else if (_type == Double) {
        return static_cast<std::int64_t>(_storage._double);
    } else if (_type == String) {
        try {
            return std::stoll(*_storage._str);
        } catch (...) {
            return def;
        }
    }
    return def;
}

double Var::toDouble(double def) const {
    if (_type == Double) {
        return _storage._double;
    } else if (_type == Int) {
        return static_cast<double>(_storage._int);
    } else if (_type == Bool) {
        return _storage._bool ? 1.0 : 0.0;
    } else if (_type == String) {
        try {
            return std::stod(*_storage._str);
        } catch (...) {
            return def;
        }
    }
    return def;
}

std::string Var::toString(const std::string& def) const {
    if (_type == String) {
        return *_storage._str;
    } else if (_type == Int) {
        return std::to_string(_storage._int);
    } else if (_type == Double) {
        // shortest representation that round-trips exactly
        char buf[32];
        for (int prec = 1; prec <= 17; ++prec) {
            std::snprintf(buf, sizeof(buf), "%.*g", prec, _storage._double);
            if (std::strtod(buf, nullptr) == _storage._double) break;
        }
        return buf;
    } else if (_type == Bool) {
        return _storage._bool ? "true" : "false";
    } else if (_type == Pointer) {
        std::ostringstream oss;
        oss << _storage._pointer;
        return oss.str();
    } else if (_type == Null) {
        return "null";
    } else if (_type == Bin) {
        const auto& bytes = *_storage._bin;
        std::ostringstream oss;
        for (uint8_t b : bytes) {
            oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(b);
        }
        return oss.str();
    } else if (_type == List) {
        return _storage._list->toString();
    } else if (_type == Dict) {
        return "[Dict]";
    } else if (_type == Custom) {
        const auto& a = *_storage._custom;
        if (!a.has_value()) return "[Custom:empty]";
        return std::string("[Custom:") + basic::_t_demangle(a.type().name()) + "]";
    }
    return def;
}

Bytes Var::toBin() const {
    if (_type == Bin) {
        return *_storage._bin;
    } else if (_type == String) {
        const std::string& s = *_storage._str;
        return Bytes(s.begin(), s.end());
    }
    return Bytes();
}

const Var::ListV& Var::toList() const { return _type == List ? *_storage._list : basic::_t_static_var_helper<ListV>::var; }
Var::ListV& Var::toList() { return _type == List ? *_storage._list : basic::_t_static_var_helper<ListV>::var; }
const Var::DictV& Var::toDict() const { return _type == Dict ? *_storage._dict : basic::_t_static_var_helper<DictV>::var; }
Var::DictV& Var::toDict() { return _type == Dict ? *_storage._dict : basic::_t_static_var_helper<DictV>::var; }

void* Var::toPointer() const { return _type == Pointer ? _storage._pointer : nullptr; }

// --- Custom 取值 ---
static const Var::CustomV empty_custom;
const Var::CustomV& Var::toCustom() const { return _type == Custom ? *_storage._custom : empty_custom; }
Var::CustomV& Var::toCustom() { return _type == Custom ? *_storage._custom : const_cast<CustomV&>(empty_custom); }

// ========== 赋值（类型安全）==========

Var& Var::fromBool(bool v) {
    destroy();
    _type = Bool;
    _storage._bool = v;
    return *this;
}

Var& Var::fromInt(int v) {
    destroy();
    _type = Int;
    _storage._int = static_cast<int64_t>(v);
    return *this;
}

Var& Var::fromInt64(std::int64_t v) {
    destroy();
    _type = Int;
    _storage._int = v;
    return *this;
}

Var& Var::fromDouble(double v) {
    destroy();
    _type = Double;
    _storage._double = v;
    return *this;
}

Var& Var::fromString(const char* v) {
    destroy();
    _type = String;
    _storage._str = new std::string(v);
    return *this;
}

Var& Var::fromString(const std::string& v) {
    destroy();
    _type = String;
    _storage._str = new std::string(v);
    return *this;
}

Var& Var::fromString(std::string&& v) {
    destroy();
    _type = String;
    _storage._str = new std::string(std::move(v));
    return *this;
}

Var& Var::fromBin(const Bytes& v) {
    destroy();
    _type = Bin;
    _storage._bin = new Bytes(v);
    return *this;
}

Var& Var::fromBin(Bytes&& v) {
    destroy();
    _type = Bin;
    _storage._bin = new Bytes(std::move(v));
    return *this;
}

Var& Var::fromList(const ListV& v) {
    destroy();
    _type = List;
    _storage._list = new ListV(v);
    return *this;
}

Var& Var::fromList(ListV&& v) {
    destroy();
    _type = List;
    _storage._list = new ListV(std::move(v));
    return *this;
}

Var& Var::fromDict(const DictV& v) {
    destroy();
    _type = Dict;
    _storage._dict = new DictV(v);
    return *this;
}

Var& Var::fromDict(DictV&& v) {
    destroy();
    _type = Dict;
    _storage._dict = new DictV(std::move(v));
    return *this;
}

Var& Var::fromPointer(void* ptr) {
    destroy();
    _type = Pointer;
    _storage._pointer = ptr;
    return *this;
}

Var& Var::fromCustom(CustomV v) {
    destroy();
    _type = Custom;
    _storage._custom = new CustomV(std::move(v));
    return *this;
}

// ========== operator[] ==========

const Var& Var::operator[](size_t index) const {
    static const Var null_var;
    if (_type != List) return null_var;
    const auto& list = *_storage._list;
    if (index >= list.size()) return null_var;
    return list[index];
}

// ========== 比较操作 ==========

bool Var::operator==(const Var& other) const {
    if (_type != other._type) return false;
    
    switch (_type) {
        case Null:
            return true;
        case Bool:
            return _storage._bool == other._storage._bool;
        case Int:
            return _storage._int == other._storage._int;
        case Double:
            return _storage._double == other._storage._double;
        case String:
            return *_storage._str == *other._storage._str;
        case Bin:
            return *_storage._bin == *other._storage._bin;
        case List:
            return *_storage._list == *other._storage._list;
        case Dict: {
            const auto& d1 = *_storage._dict;
            const auto& d2 = *other._storage._dict;
            if (d1.size() != d2.size()) return false;
            for (const auto& kv : d1) {
                auto it = d2.find(kv.key);
                if (it == d2.end() || it->value != kv.value) {
                    return false;
                }
            }
            return true;
        }
        case Pointer:
            return _storage._pointer == other._storage._pointer;
        case Custom:
            return false;
        default:
            return false;
    }
}

// ========== 调试输出 ==========

std::ostream& operator<<(std::ostream& os, const Var& v) {
    os << v.toString();
    return os;
}

// ========== 内部辅助方法 ==========

void Var::copyFrom(const Var& other) {
    switch (_type) {
        case String:  _storage._str    = new std::string(*other._storage._str); break;
        case Bin:     _storage._bin    = new Bytes(*other._storage._bin);       break;
        case List:    _storage._list   = new ListV(*other._storage._list);      break;
        case Dict:    _storage._dict   = new DictV(*other._storage._dict);      break;
        case Custom:  _storage._custom = new CustomV(*other._storage._custom);  break;
        default:      _storage = other._storage; break;
    }
}

void Var::moveFrom(Var&& other) {
    switch (_type) {
        case String:  _storage._str    = other._storage._str;    other._storage._str    = nullptr; break;
        case Bin:     _storage._bin    = other._storage._bin;    other._storage._bin    = nullptr; break;
        case List:    _storage._list   = other._storage._list;   other._storage._list   = nullptr; break;
        case Dict:    _storage._dict   = other._storage._dict;   other._storage._dict   = nullptr; break;
        case Custom:  _storage._custom = other._storage._custom; other._storage._custom = nullptr; break;
        default:      _storage = other._storage; break;
    }
    other._type = Null;
}

void Var::destroy() {
    switch (_type) {
        case String:  delete _storage._str;    break;
        case Bin:     delete _storage._bin;    break;
        case List:    delete _storage._list;   break;
        case Dict:    delete _storage._dict;   break;
        case Custom:  delete _storage._custom; break;
        default: break;
    }
    _type = Null;
}

// ========== 高性能线程安全 swap ==========

void Var::swap(Var& other) noexcept {
    std::swap(_type, other._type);
    std::swap(_storage, other._storage);
}

} // namespace ve
