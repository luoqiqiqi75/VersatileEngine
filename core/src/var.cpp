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

namespace ve {

// ========== 构造与赋值 ==========

Var::Var() : _type(Null), _storage() { _storage._int = 0; }
Var::Var(bool v) : _type(Bool), _storage() { _storage._bool = v; }
Var::Var(int v) : _type(Int) { _storage._int = static_cast<int64_t>(v); }
Var::Var(std::int64_t v) : _type(Int) { _storage._int = v; }
Var::Var(double v) : _type(Double) { _storage._double = v; }
Var::Var(const char* v) : _type(String) { new (&_storage._str) std::string(v); }
Var::Var(const std::string& v) : _type(String) { new (&_storage._str) std::string(v); }
Var::Var(std::string&& v) : _type(String) { new (&_storage._str) std::string(std::move(v)); }
Var::Var(const Bytes& v) : _type(Bin) { new (&_storage._bin) Bytes(v); }
Var::Var(Bytes&& v) : _type(Bin) { new (&_storage._bin) Bytes(std::move(v)); }
Var::Var(void* ptr) : _type(Pointer) { _storage._pointer = ptr; }

Var::Var(const ListV& v) : _type(List) { new (&_storage._list) ListV(v); }
Var::Var(ListV&& v) : _type(List) { new (&_storage._list) ListV(std::move(v)); }
Var::Var(const DictV& v) : _type(Dict) { new (&_storage._dict) DictV(v); }
Var::Var(DictV&& v) : _type(Dict) { new (&_storage._dict) DictV(std::move(v)); }

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

template<> std::string& Var::ref<Var::String, std::string>() { return *reinterpret_cast<std::string*>(&_storage._str); }
template<> const std::string& Var::ref<Var::String, std::string>() const { return *reinterpret_cast<const std::string*>(&_storage._str); }
template<> Bytes& Var::ref<Var::Bin, Bytes>() { return *reinterpret_cast<Bytes*>(&_storage._bin); }
template<> const Bytes& Var::ref<Var::Bin, Bytes>() const { return *reinterpret_cast<const Bytes*>(&_storage._bin); }

template<> Var::ListV& Var::ref<Var::List, Var::ListV>() { return *reinterpret_cast<ListV*>(&_storage._list); }
template<> const Var::ListV& Var::ref<Var::List, Var::ListV>() const { return *reinterpret_cast<const ListV*>(&_storage._list); }
template<> Var::DictV& Var::ref<Var::Dict, Var::DictV>() { return *reinterpret_cast<DictV*>(&_storage._dict); }
template<> const Var::DictV& Var::ref<Var::Dict, Var::DictV>() const { return *reinterpret_cast<const DictV*>(&_storage._dict); }

// ========== 取值（类型安全）==========

bool Var::toBool(bool def) const {
    if (_type == Bool) {
        return _storage._bool;
    } else if (_type == Int) {
        return _storage._int != 0;
    } else if (_type == Double) {
        return _storage._double != 0.0;
    } else if (_type == String) {
        const auto& s = ref<String, std::string>();
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
            return std::stoi(ref<String, std::string>());
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
            return std::stod(ref<String, std::string>());
        } catch (...) {
            return def;
        }
    }
    return def;
}

std::string Var::toString(const std::string& def) const {
    if (_type == String) {
        return ref<String, std::string>();
    } else if (_type == Int) {
        return std::to_string(_storage._int);
    } else if (_type == Double) {
        return std::to_string(_storage._double);
    } else if (_type == Bool) {
        return _storage._bool ? "true" : "false";
    } else if (_type == Pointer) {
        std::ostringstream oss;
        oss << _storage._pointer;
        return oss.str();
    } else if (_type == Null) {
        return "null";
    } else if (_type == Bin) {
        // Bytes 转换为十六进制字符串
        const auto& bytes = ref<Bin, Bytes>();
        std::ostringstream oss;
        for (uint8_t b : bytes) {
            oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(b);
        }
        return oss.str();
    } else if (_type == List) {
        return ref<List, ListV>().toString();
    } else if (_type == Dict) {
        return "[Dict]"; // TODO: 实现对象的字符串化
    }
    return def;
}

Bytes Var::toBin() const {
    if (_type == Bin) {
        return ref<Bin, Bytes>();
    } else if (_type == String) {
        const std::string& s = ref<String, std::string>();
        return Bytes(s.begin(), s.end());
    }
    return Bytes();
}

const Var::ListV& Var::toList() const { return _type == List ? ref<List, ListV>() : basic::_t_static_var_helper<ListV>::var; }
Var::ListV& Var::toList() { return _type == List ? ref<List, ListV>() : basic::_t_static_var_helper<ListV>::var; }
const Var::DictV& Var::toDict() const { return _type == Dict ? ref<Dict, DictV>() : basic::_t_static_var_helper<DictV>::var; }
Var::DictV& Var::toDict() { return _type == Dict ? ref<Dict, DictV>() : basic::_t_static_var_helper<DictV>::var; }

void* Var::toPointer() const { return _type == Pointer ? _storage._pointer : nullptr; }

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
    new (&_storage._str) std::string(v);
    return *this;
}

Var& Var::fromString(const std::string& v) {
    destroy();
    _type = String;
    new (&_storage._str) std::string(v);
    return *this;
}

Var& Var::fromString(std::string&& v) {
    destroy();
    _type = String;
    new (&_storage._str) std::string(std::move(v));
    return *this;
}

Var& Var::fromBin(const Bytes& v) {
    destroy();
    _type = Bin;
    new (&_storage._bin) Bytes(v);
    return *this;
}

Var& Var::fromBin(Bytes&& v) {
    destroy();
    _type = Bin;
    new (&_storage._bin) Bytes(std::move(v));
    return *this;
}

Var& Var::fromList(const ListV& v) {
    destroy();
    _type = List;
    new (&_storage._list) ListV(v);
    return *this;
}

Var& Var::fromList(ListV&& v) {
    destroy();
    _type = List;
    new (&_storage._list) ListV(std::move(v));
    return *this;
}

Var& Var::fromDict(const DictV& v) {
    destroy();
    _type = Dict;
    new (&_storage._dict) DictV(v);
    return *this;
}

Var& Var::fromDict(DictV&& v) {
    destroy();
    _type = Dict;
    new (&_storage._dict) DictV(std::move(v));
    return *this;
}

Var& Var::fromPointer(void* ptr) {
    destroy();
    _type = Pointer;
    _storage._pointer = ptr;
    return *this;
}

// ========== operator[] ==========

const Var& Var::operator[](size_t index) const {
    static const Var null_var;
    if (_type != List) return null_var;
    const auto& list = ref<List, ListV>();
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
            return ref<String, std::string>() == other.ref<String, std::string>();
        case Bin:
            return ref<Bin, Bytes>() == other.ref<Bin, Bytes>();
        case List:
            return ref<List, ListV>() == other.ref<List, ListV>();
        case Dict: {
            // Dict 没有 operator==，手动比较
            const auto& d1 = ref<Dict, DictV>();
            const auto& d2 = other.ref<Dict, DictV>();
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
        case String:
            new (&_storage._str) std::string(other.ref<String, std::string>());
            break;
        case Bin:
            new (&_storage._bin) Bytes(other.ref<Bin, Bytes>());
            break;
        case List:
            new (&_storage._list) ListV(other.ref<List, ListV>());
            break;
        case Dict:
            new (&_storage._dict) DictV(other.ref<Dict, DictV>());
            break;
        default:
            _storage = other._storage;
            break;
    }
}

void Var::moveFrom(Var&& other) {
    switch (_type) {
        case String:
            new (&_storage._str) std::string(std::move(other.ref<String, std::string>()));
            other.ref<String, std::string>().~basic_string();
            break;
        case Bin:
            new (&_storage._bin) Bytes(std::move(other.ref<Bin, Bytes>()));
            other.ref<Bin, Bytes>().~Bytes();
            break;
        case List:
            new (&_storage._list) ListV(std::move(other.ref<List, ListV>()));
            other.ref<List, ListV>().~ListV();
            break;
        case Dict:
            new (&_storage._dict) DictV(std::move(other.ref<Dict, DictV>()));
            other.ref<Dict, DictV>().~DictV();
            break;
        default:
            _storage = other._storage;
            break;
    }
    other._type = Null;
}

void Var::destroy() {
    switch (_type) {
        case String:
            ref<String, std::string>().~basic_string();
            break;
        case Bin:
            ref<Bin, Bytes>().~Bytes();
            break;
        case List:
            ref<List, ListV>().~ListV();
            break;
        case Dict:
            ref<Dict, DictV>().~DictV();
            break;
        default:
            break;
    }
    _type = Null;
}

// ========== 高性能线程安全 swap ==========

void Var::swap(Var& other) noexcept {
    // 如果类型相同，直接交换存储内容（O(1)）
    if (_type == other._type) {
        switch (_type) {
            case Null:
            case Bool:
            case Int:
            case Double:
            case Pointer:
                // 基础类型：直接交换存储
                std::swap(_storage, other._storage);
                return;
            case String:
                // string：交换内容（O(1)）
                ref<String, std::string>().swap(other.ref<String, std::string>());
                return;
            case Bin:
                // Bytes：交换内容（O(1)）
                ref<Bin, Bytes>().swap(other.ref<Bin, Bytes>());
                return;
            case List:
                // ListV：交换内容（O(1)）
                ref<List, ListV>().swap(other.ref<List, ListV>());
                return;
            case Dict: {
                // OrderedHashMap 没有 swap，使用移动语义（O(1)）
                DictV temp(std::move(ref<Dict, DictV>()));
                ref<Dict, DictV>().~DictV();
                new (&_storage._dict) DictV(std::move(other.ref<Dict, DictV>()));
                other.ref<Dict, DictV>().~DictV();
                new (&other._storage._dict) DictV(std::move(temp));
                return;
            }
        }
    }
    
    // 类型不同：使用移动语义交换（O(1) 移动，避免拷贝）
    Var temp = std::move(*this);
    *this = std::move(other);
    other = std::move(temp);
}

} // namespace ve
