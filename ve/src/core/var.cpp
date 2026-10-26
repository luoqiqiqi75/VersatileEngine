// ----------------------------------------------------------------------------
// var.cpp
// ----------------------------------------------------------------------------
// Copyright (c) 2023-present Thilo and VersatileEngine contributors.
// Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
// See LICENSE file in the project root for full license information.
// ----------------------------------------------------------------------------
#include "ve/core/var.h"

#include <iomanip>
#include <cstdio>
#include <thread>

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>
#endif

namespace ve {

static_assert(sizeof(Var) == 16, "Var must stay 16 bytes (_seq lives in padding)");
static_assert(std::atomic<std::uint32_t>::is_always_lock_free, "Var::_seq must be lock-free");

template<typename T>
static VE_FORCE_INLINE T _racyLoad(const T& src) noexcept
{
#if defined(__GNUC__)
    T out;
    __atomic_load(&src, &out, __ATOMIC_RELAXED);
    return out;
#else
    return src;
#endif
}

template<typename T>
static VE_FORCE_INLINE void _racyStore(T& dst, T v) noexcept
{
#if defined(__GNUC__)
    __atomic_store(&dst, &v, __ATOMIC_RELAXED);
#else
    dst = v;
#endif
}

static VE_FORCE_INLINE void _cpuRelax() noexcept
{
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    _mm_pause();
#elif defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
    __asm__ __volatile__("yield");
#endif
}

static VE_FORCE_INLINE bool _isHeap(Var::Type t) noexcept { return t >= Var::STRING && t != Var::POINTER; }

// Only values with no public mutable access use shared storage. Copies of a
// live Var may retain this data concurrently; changing that Var still needs
// external synchronization.
template<typename T>
struct Var::SharedData {
    std::atomic<size_t> refs{1};
    const T value;

    template<typename U>
    explicit SharedData(U&& v) : value(std::forward<U>(v)) {}

    void retain() noexcept { refs.fetch_add(1, std::memory_order_relaxed); }
    void release() noexcept {
        if (refs.fetch_sub(1, std::memory_order_acq_rel) == 1) delete this;
    }
};

// construction

Var::Var() : _type(NONE), _storage{} { _storage._int = 0; }
Var::Var(bool v) : _type(BOOL), _storage{} { _storage._bool = v; }
Var::Var(int v) : _type(INT) { _storage._int = static_cast<int64_t>(v); }
Var::Var(std::int64_t v) : _type(INT) { _storage._int = v; }
Var::Var(double v) : _type(DOUBLE) { _storage._double = v; }
Var::Var(const char* v) : _type(STRING) { _storage._str = new SharedData<std::string>(v); }
Var::Var(const std::string& v) : _type(STRING) { _storage._str = new SharedData<std::string>(v); }
Var::Var(std::string&& v) : _type(STRING) { _storage._str = new SharedData<std::string>(std::move(v)); }
Var::Var(const Bytes& v) : _type(BIN) { _storage._bin = new SharedData<Bytes>(v); }
Var::Var(Bytes&& v) : _type(BIN) { _storage._bin = new SharedData<Bytes>(std::move(v)); }
Var::Var(void* ptr) : _type(POINTER) { _storage._pointer = ptr; }

Var::Var(const ListV& v) : _type(LIST) { _storage._list = new ListV(v); }
Var::Var(ListV&& v) : _type(LIST) { _storage._list = new ListV(std::move(v)); }
Var::Var(const DictV& v) : _type(DICT) { _storage._dict = new DictV(v); }
Var::Var(DictV&& v) : _type(DICT) { _storage._dict = new DictV(std::move(v)); }
Var::Var(CallableV fn) : _type(CALLABLE) { _storage._callable = new CallableV(std::move(fn)); }

Var::Var(const Var& other) : _type(other._type) { copyFrom(other); }
Var::Var(Var&& other) noexcept : _type(other._type) { moveFrom(std::move(other)); }

Var& Var::operator=(const Var& other)
{
    if (this == &other) return *this;
    if (VE_LIKELY(_type == other._type)) {
        switch (_type) {
            case STRING: if (_storage._str == other._storage._str) return *this; break;
            case BIN:    if (_storage._bin == other._storage._bin) return *this; break;
            case LIST: case DICT: case CALLABLE: case CUSTOM: break;
            default: _storage = other._storage; return *this;
        }
    }
    destroy();
    _type = other._type;
    copyFrom(other);
    return *this;
}

Var& Var::operator=(Var&& other) noexcept
{
    if (this == &other) return *this;
    if (VE_LIKELY(_type == other._type) && (_type <= DOUBLE || _type == POINTER)) {
        moveFrom(std::move(other));
        return *this;
    }
    destroy();
    _type = other._type;
    moveFrom(std::move(other));
    return *this;
}

Var::~Var() {
    destroy();
}

// Custom type query

const std::type_info& Var::customType() const {
    if (_type != CUSTOM || !_storage._custom) return typeid(void);
    const auto& a = _storage._custom->value;
    return a.has_value() ? a.type() : typeid(void);
}

bool Var::customIs(const std::type_info& ti) const {
    if (_type != CUSTOM || !_storage._custom) return false;
    return _storage._custom->value.type() == ti;
}

Var Var::invoke(const Var& input) const {
    if (_type == CALLABLE && _storage._callable)
        return (*_storage._callable)(input);
    return {};
}

// value extraction

bool Var::toBool(bool def) const {
    if (_type == BOOL) {
        return _storage._bool;
    } else if (_type == INT) {
        return _storage._int != 0;
    } else if (_type == DOUBLE) {
        return _storage._double != 0.0;
    } else if (_type == STRING) {
        const auto& s = _storage._str->value;
        return s == "true" || s == "1" || !s.empty();
    }
    return def;
}

int Var::toInt(int def) const {
    if (_type == INT) {
        return static_cast<int>(_storage._int);
    } else if (_type == BOOL) {
        return _storage._bool ? 1 : 0;
    } else if (_type == DOUBLE) {
        return static_cast<int>(_storage._double);
    } else if (_type == STRING) {
        try {
            return std::stoi(_storage._str->value);
        } catch (...) {
            return def;
        }
    }
    return def;
}

std::int64_t Var::toInt64(std::int64_t def) const {
    if (_type == INT) {
        return _storage._int;
    } else if (_type == BOOL) {
        return _storage._bool ? 1 : 0;
    } else if (_type == DOUBLE) {
        return static_cast<std::int64_t>(_storage._double);
    } else if (_type == STRING) {
        try {
            return std::stoll(_storage._str->value);
        } catch (...) {
            return def;
        }
    }
    return def;
}

double Var::toDouble(double def) const {
    if (_type == DOUBLE) {
        return _storage._double;
    } else if (_type == INT) {
        return static_cast<double>(_storage._int);
    } else if (_type == BOOL) {
        return _storage._bool ? 1.0 : 0.0;
    } else if (_type == STRING) {
        try {
            return std::stod(_storage._str->value);
        } catch (...) {
            return def;
        }
    }
    return def;
}

std::string Var::toString(const std::string& def) const {
    if (_type == STRING) {
        return _storage._str->value;
    } else if (_type == INT) {
        return std::to_string(_storage._int);
    } else if (_type == DOUBLE) {
        char buf[32];
        for (int prec = 1; prec <= 17; ++prec) {
            std::snprintf(buf, sizeof(buf), "%.*g", prec, _storage._double);
            if (std::strtod(buf, nullptr) == _storage._double) break;
        }
        return buf;
    } else if (_type == BOOL) {
        return _storage._bool ? "true" : "false";
    } else if (_type == POINTER) {
        std::ostringstream oss;
        oss << _storage._pointer;
        return oss.str();
    } else if (_type == NONE) {
        return def;
    } else if (_type == BIN) {
        const auto& bytes = _storage._bin->value;
        std::ostringstream oss;
        for (uint8_t b : bytes) {
            oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(b);
        }
        return oss.str();
    } else if (_type == LIST) {
        const auto& list = *_storage._list;
        std::string s = "[";
        for (size_t i = 0; i < list.size(); ++i) {
            if (i) s += ", ";
            const auto& item = list[i];
            if (item._type == STRING)
                s += '"' + item.toString() + '"';
            else
                s += item.toString();
        }
        s += ']';
        return s;
    } else if (_type == DICT) {
        const auto& dict = *_storage._dict;
        std::string s = "{";
        bool first = true;
        for (const auto& kv : dict) {
            if (!first) s += ", ";
            first = false;
            s += kv.first + ": ";
            if (kv.second._type == STRING)
                s += '"' + kv.second.toString() + '"';
            else
                s += kv.second.toString();
        }
        s += '}';
        return s;
    } else if (_type == CALLABLE) {
        return "<callable>";
    } else if (_type == CUSTOM) {
        if (!_storage._custom || !_storage._custom->value.has_value())
            return "[Custom:empty]";
        if (_storage._custom->to_str)
            return _storage._custom->to_str(_storage._custom->value);
        return std::string("[Custom:") + basic::_t_demangle(_storage._custom->value.type().name()) + "]";
    }
    return def;
}

Bytes Var::toBin() const {
    if (_type == BIN) {
        return _storage._bin->value;
    } else if (_type == STRING) {
        const std::string& s = _storage._str->value;
        return Bytes(s.begin(), s.end());
    } else if (_type == CUSTOM && _storage._custom && _storage._custom->to_bin) {
        return _storage._custom->to_bin(_storage._custom->value);
    }
    return Bytes();
}

const Var::ListV& Var::toList() const { return _type == LIST ? *_storage._list : basic::_t_static_var_helper<ListV>::var; }
Var::ListV& Var::toList() { return _type == LIST ? *_storage._list : basic::_t_static_var_helper<ListV>::var; }
const Var::DictV& Var::toDict() const { return _type == DICT ? *_storage._dict : basic::_t_static_var_helper<DictV>::var; }
Var::DictV& Var::toDict() { return _type == DICT ? *_storage._dict : basic::_t_static_var_helper<DictV>::var; }

void* Var::toPointer() const { return _type == POINTER ? _storage._pointer : nullptr; }

// Callable extraction
static const Var::CallableV empty_callable;
const Var::CallableV& Var::toCallable() const {
    return (_type == CALLABLE && _storage._callable) ? *_storage._callable : empty_callable;
}
Var::CallableV& Var::toCallable() {
    return (_type == CALLABLE && _storage._callable) ? *_storage._callable : const_cast<CallableV&>(empty_callable);
}

// Custom extraction
static const Var::CustomV empty_custom;
const Var::CustomV& Var::toCustom() const {
    return (_type == CUSTOM && _storage._custom) ? _storage._custom->value : empty_custom;
}
Var::CustomV& Var::toCustom() {
    return (_type == CUSTOM && _storage._custom) ? _storage._custom->value : const_cast<CustomV&>(empty_custom);
}

// value assignment

Var& Var::fromBool(bool v) {
    destroy();
    _type = BOOL;
    _storage._bool = v;
    return *this;
}

Var& Var::fromInt(int v) {
    destroy();
    _type = INT;
    _storage._int = static_cast<int64_t>(v);
    return *this;
}

Var& Var::fromInt64(std::int64_t v) {
    destroy();
    _type = INT;
    _storage._int = v;
    return *this;
}

Var& Var::fromDouble(double v) {
    destroy();
    _type = DOUBLE;
    _storage._double = v;
    return *this;
}

Var& Var::fromString(const char* v) {
    destroy();
    _type = STRING;
    _storage._str = new SharedData<std::string>(v);
    return *this;
}

Var& Var::fromString(const std::string& v) {
    destroy();
    _type = STRING;
    _storage._str = new SharedData<std::string>(v);
    return *this;
}

Var& Var::fromString(std::string&& v) {
    destroy();
    _type = STRING;
    _storage._str = new SharedData<std::string>(std::move(v));
    return *this;
}

Var& Var::fromBin(const Bytes& v) {
    destroy();
    _type = BIN;
    _storage._bin = new SharedData<Bytes>(v);
    return *this;
}

Var& Var::fromBin(Bytes&& v) {
    destroy();
    _type = BIN;
    _storage._bin = new SharedData<Bytes>(std::move(v));
    return *this;
}

Var& Var::fromList(const ListV& v) {
    destroy();
    _type = LIST;
    _storage._list = new ListV(v);
    return *this;
}

Var& Var::fromList(ListV&& v) {
    destroy();
    _type = LIST;
    _storage._list = new ListV(std::move(v));
    return *this;
}

Var& Var::fromDict(const DictV& v) {
    destroy();
    _type = DICT;
    _storage._dict = new DictV(v);
    return *this;
}

Var& Var::fromDict(DictV&& v) {
    destroy();
    _type = DICT;
    _storage._dict = new DictV(std::move(v));
    return *this;
}

Var& Var::fromPointer(void* ptr) {
    destroy();
    _type = POINTER;
    _storage._pointer = ptr;
    return *this;
}

Var& Var::fromCallable(CallableV fn) {
    destroy();
    _type = CALLABLE;
    _storage._callable = new CallableV(std::move(fn));
    return *this;
}

Var& Var::fromCustom(CustomV v) {
    destroy();
    _type = CUSTOM;
    _storage._custom = new CustomStorage{std::move(v), nullptr, nullptr};
    return *this;
}

// operator[]

const Var& Var::operator[](size_t index) const {
    static const Var null_var;
    if (_type != LIST) return null_var;
    const auto& list = *_storage._list;
    if (index >= list.size()) return null_var;
    return list[index];
}

// comparison

bool Var::operator==(const Var& other) const {
    if (_type != other._type) return false;
    
    switch (_type) {
        case NONE:
            return true;
        case BOOL:
            return _storage._bool == other._storage._bool;
        case INT:
            return _storage._int == other._storage._int;
        case DOUBLE:
            return _storage._double == other._storage._double;
        case STRING:
            return _storage._str == other._storage._str || _storage._str->value == other._storage._str->value;
        case BIN:
            return _storage._bin == other._storage._bin || _storage._bin->value == other._storage._bin->value;
        case LIST:
            return *_storage._list == *other._storage._list;
        case DICT: {
            const auto& d1 = *_storage._dict;
            const auto& d2 = *other._storage._dict;
            if (d1.size() != d2.size()) return false;
            for (const auto& kv : d1) {
                auto it = d2.find(kv.first);
                if (it == d2.end() || it->second != kv.second) {
                    return false;
                }
            }
            return true;
        }
        case POINTER:
            return _storage._pointer == other._storage._pointer;
        case CALLABLE:
            return false;  // callables are not comparable
        case CUSTOM:
            return false;
        default:
            return false;
    }
}

// debug output

std::ostream& operator<<(std::ostream& os, const Var& v) {
    os << v.toString();
    return os;
}

// internal helpers

void Var::copyFrom(const Var& other) {
    switch (_type) {
        case STRING:  _storage._str = other._storage._str; _storage._str->retain(); break;
        case BIN:     _storage._bin = other._storage._bin; _storage._bin->retain(); break;
        case LIST:    _storage._list   = new ListV(*other._storage._list);      break;
        case DICT:    _storage._dict     = new DictV(*other._storage._dict);      break;
        case CALLABLE:_storage._callable = new CallableV(*other._storage._callable); break;
        case CUSTOM:  _storage._custom   = new CustomStorage(*other._storage._custom); break;
        default:      _storage = other._storage; break;
    }
}

void Var::moveFrom(Var&& other) {
    _storage = other._storage;
    other._type = NONE;
    other._storage._int = 0;
}

void Var::destroy() {
    switch (_type) {
        case STRING:  _storage._str->release(); break;
        case BIN:     _storage._bin->release(); break;
        case LIST:    delete _storage._list;   break;
        case DICT:     delete _storage._dict;     break;
        case CALLABLE: delete _storage._callable; break;
        case CUSTOM:   delete _storage._custom;   break;
        default: break;
    }
    _type = NONE;
}

// swap

void Var::swap(Var& other) noexcept {
    std::swap(_type, other._type);
    std::swap(_storage, other._storage);
}

// ============================================================================
// concurrent
// ============================================================================

std::uint32_t Var::lockConcurrent() const noexcept
{
    std::uint32_t s = _seq.load(std::memory_order_relaxed);
    for (unsigned spins = 0;; ++spins) {
        if (!(s & 1) && _seq.compare_exchange_weak(s, std::uint32_t(s | 1),
                std::memory_order_acquire, std::memory_order_relaxed))
            return s;
        if (spins < 64) _cpuRelax();
        else std::this_thread::yield();
        s = _seq.load(std::memory_order_relaxed);
    }
}

Var Var::loadConcurrent() const
{
    const std::uint32_t s1 = _seq.load(std::memory_order_acquire);
    if (VE_LIKELY(!(s1 & 1))) {
        const Type t = _racyLoad(_type);
        if (VE_LIKELY(!_isHeap(t))) {
            const Storage st = _racyLoad(_storage);
            std::atomic_thread_fence(std::memory_order_acquire);
            if (VE_LIKELY(_seq.load(std::memory_order_relaxed) == s1)) {
                Var r;
                r._type = t;
                r._storage = st;
                return r;
            }
        }
    }
    struct Unlock {
        const Var* v; std::uint32_t s;
        ~Unlock() { v->_seq.store(s, std::memory_order_release); }
    } lk{this, lockConcurrent()};
    return Var(*this);
}

Var Var::exchangeConcurrent(Var&& v) noexcept
{
    Var old;
    const std::uint32_t s = lockConcurrent();
    std::atomic_thread_fence(std::memory_order_release);
    old._type = _type;
    old._storage = _storage;
    _racyStore(_type, v._type);
    _racyStore(_storage, v._storage);
    _seq.store(std::uint32_t(s + 2), std::memory_order_release);
    v._type = NONE;
    v._storage._int = 0;
    return old;
}

} // namespace ve
