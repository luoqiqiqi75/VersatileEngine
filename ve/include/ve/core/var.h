// ----------------------------------------------------------------------------
// var.h — ve::Var : 16-byte variant (NONE/BOOL/INT/DOUBLE/STRING/BIN/LIST/DICT/POINTER/CALLABLE/CUSTOM)
// ----------------------------------------------------------------------------
// Copyright (c) 2023-present Thilo and VersatileEngine contributors.
// Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
// See LICENSE file in the project root for full license information.
// ----------------------------------------------------------------------------
#pragma once

#include "base.h"
#include "convert.h"

namespace ve {

class VE_API Var
{
public:
    using ListV   = Vector<Var>;
    using DictV   = Dict<Var>;
    using CustomV = std::any;
    using CallableV = std::function<Var(const Var&)>;

    struct CustomStorage {
        std::any value;
        std::string(*to_str)(const std::any&) = nullptr;
        Bytes(*to_bin)(const std::any&) = nullptr;
    };

    enum Type : uint8_t {
        NONE,
        BOOL,
        INT,
        DOUBLE,
        STRING,
        BIN,
        LIST,
        DICT,
        POINTER,
        CALLABLE,
        CUSTOM
    };

    // construction
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
    Var(CallableV fn);

    // Block implicit pointer-to-bool conversion for non-char/void data pointers.
    // Function pointers are allowed (routed to callable via template ctor).
    // Data pointers must be stored explicitly via Var(static_cast<void*>(p)).
    template<typename T, std::enable_if_t<
        basic::Meta<T>::is_raw_pointer
        && !std::is_same_v<T, void*>, int> = 0>
    Var(T) = delete;

    template<typename T>
    Var(const T& v) : _type(NONE), _storage{} {
        if constexpr (is_list_like_v<T>) {
            _type = LIST;
            _storage._list = new ListV();
            _storage._list->reserve(v.size());
            for (const auto& item : v)
                _storage._list->push_back(Var(item));
        } else if constexpr (is_dict_like_v<T>) {
            _type = DICT;
            _storage._dict = new DictV();
            for (const auto& kv : v) {
                using KVAccess = typename T::KVAccessT;
                (*_storage._dict)[KVAccess::key(kv)] = Var(KVAccess::value(kv));
            }
        } else if constexpr (basic::Meta<T>::is_numeric) {
            if constexpr (std::is_same_v<T, bool>)
                { _type = BOOL; _storage._bool = v; }
            else if constexpr (std::is_integral_v<T>)
                { _type = INT; _storage._int = static_cast<int64_t>(v); }
            else
                { _type = DOUBLE; _storage._double = static_cast<double>(v); }
        } else if constexpr (basic::Meta<T>::is_string) {
            _type = STRING;
            _storage._str = new std::string(v);
        } else if constexpr (basic::Meta<T>::is_callable) {
            *this = Var::callable(v);
        } else {
            *this = Var::custom(v);
        }
    }

    template<typename T>
    static Var custom(T&& v) {
        using U = std::decay_t<T>;
        Var result;
        result._type = CUSTOM;
        result._storage._custom = new CustomStorage{
            std::any(std::forward<T>(v)),
            [](const std::any& a) -> std::string {
                if (auto* p = std::any_cast<U>(&a)) return Convert<U>::toString(*p);
                return "";
            },
            [](const std::any& a) -> Bytes {
                if (auto* p = std::any_cast<U>(&a)) return Convert<U>::toBin(*p);
                return {};
            }
        };
        return result;
    }

    // callable — universal callable-to-CallableV conversion
    // Adapts: Var as raw Var, other single args via as<Arg0>(), multi-arg from List, Result -> custom<Result>
    template<typename F, std::enable_if_t<basic::Meta<std::decay_t<F>>::is_callable, int> = 0>
    static Var callable(F&& fn) { return Var(wrapCallable(std::forward<F>(fn))); }

    Var(const Var& other);
    Var(Var&& other) noexcept;
    Var& operator=(const Var& other);
    Var& operator=(Var&& other) noexcept;
    ~Var();

    // type query
    Type type() const { return _type; }
    bool isNull() const { return _type == NONE; }
    bool is(const Type t) const { return _type == t; }

    bool isBool() const { return _type == BOOL; }
    bool isInt() const { return _type == INT; }
    bool isDouble() const { return _type == DOUBLE; }
    bool isString() const { return _type == STRING; }
    bool isBin() const { return _type == BIN; }
    bool isList() const { return _type == LIST; }
    bool isDict() const { return _type == DICT; }
    bool isPointer() const { return _type == POINTER; }
    bool isCallable() const { return _type == CALLABLE; }
    bool isCustom() const { return _type == CUSTOM; }

    const std::type_info& customType() const;
    bool customIs(const std::type_info& ti) const;
    template<typename T> bool customIs() const { return customIs(typeid(T)); }

    // callable invoke
    Var invoke(const Var& input = {}) const;

    // value extraction (type-safe, returns default on mismatch)
    bool toBool(bool def = false) const;
    int toInt(int def = -1) const;
    std::int64_t toInt64(std::int64_t def = -1) const;
    double toDouble(double def = 0.0) const;
    std::string toString(const std::string& def = "") const;
    Bytes toBin() const;

    const ListV& toList() const;
    ListV& toList();
    const DictV& toDict() const;
    DictV& toDict();
    void* toPointer() const;

    const CallableV& toCallable() const;
    CallableV& toCallable();

    const CustomV& toCustom() const;
    CustomV& toCustom();

    template<typename T> const T* customPtr() const {
        if (_type != CUSTOM || !_storage._custom) return nullptr;
        return std::any_cast<T>(&_storage._custom->value);
    }
    template<typename T> T* customPtr() {
        if (_type != CUSTOM || !_storage._custom) return nullptr;
        return std::any_cast<T>(&_storage._custom->value);
    }

    // as<T>() — fast extraction: basic types -> direct, CUSTOM -> any_cast
    template<typename T>
    std::decay_t<T> as() const {
        using U = std::decay_t<T>;
        if constexpr (std::is_same_v<U, Var>)                  return *this;
        else if constexpr (std::is_same_v<U, bool>)             return toBool();
        else if constexpr (std::is_same_v<U, int>)              return toInt();
        else if constexpr (std::is_integral_v<U>)               return static_cast<U>(toInt64());
        else if constexpr (std::is_floating_point_v<U>)         return static_cast<U>(toDouble());
        else if constexpr (std::is_same_v<U, std::string>)      return toString();
        else if constexpr (basic::Meta<U>::is_raw_pointer)      return static_cast<U>(toPointer());
        else {
            if (_type == CUSTOM && _storage._custom) {
                if (auto* p = std::any_cast<U>(&_storage._custom->value))
                    return *p;
            }
            return U{};
        }
    }

    template<typename T>
    T to(const T& def = T{}) const {
        if (_type == NONE) return def;
        using U = std::decay_t<T>;
        if constexpr (std::is_same_v<U, Var>
                   || basic::Meta<U>::is_numeric
                   || basic::Meta<U>::is_string
                   || basic::Meta<U>::is_raw_pointer) {
            return as<T>();
        } else {
            if (_type == CUSTOM && _storage._custom) {
                if (auto* p = std::any_cast<U>(&_storage._custom->value))
                    return *p;
            }
            U out;
            if (Convert<U>::fromString(toString(), out)) return out;
            return def;
        }
    }

    const Var& operator[](size_t index) const;

    // value assignment
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
    Var& fromCallable(CallableV fn);
    Var& fromCustom(CustomV v);

    template<typename T>
    Var& from(const T& v) {
        *this = Var(v);
        return *this;
    }

    // comparison
    bool operator==(const Var& other) const;
    bool operator!=(const Var& other) const { return !(*this == other); }

    void swap(Var& other) noexcept;

    friend std::ostream& operator<<(std::ostream& os, const Var& v);

private:
    template<Type E, typename V> V& ref();
    template<Type E, typename V> const V& ref() const;

    void copyFrom(const Var& other);
    void moveFrom(Var&& other);
    void destroy();

    template<typename F>
    static CallableV wrapCallable(F&& fn);

    template<typename F, std::size_t... I>
    static CallableV wrapCallableIndexed(F&& fn, std::index_sequence<I...>);

    Type _type;

    union Storage {
        bool            _bool;
        int64_t         _int;
        double          _double;
        void*           _pointer;
        std::string*    _str;
        Bytes*          _bin;
        ListV*          _list;
        DictV*          _dict;
        CallableV*      _callable;
        CustomStorage*  _custom;
    } _storage;
};

using Result = ResultT<Var>;

namespace detail {

inline Var wrapCallableRet() { return Var(); }

template<typename R>
inline Var wrapCallableRet(R&& ret) {
    using Ret = std::decay_t<R>;
    if constexpr (std::is_same_v<Ret, Result>) {
        return Var::custom(std::forward<R>(ret));
    } else if constexpr (basic::Meta<Ret>::is_raw_pointer) {
        return Var(static_cast<void*>(ret));
    } else {
        return Var(std::forward<R>(ret));
    }
}

} // namespace detail

template<typename F, std::size_t... I>
inline Var::CallableV Var::wrapCallableIndexed(F&& fn, std::index_sequence<I...>) {
    using Traits = basic::FnTraits<std::decay_t<F>>;
    using FDecay = std::decay_t<F>;
    return [f = FDecay(std::forward<F>(fn))](const Var& input) -> Var {
        return detail::wrapCallableRet(f(input[I].template as<std::decay_t<typename Traits::template ArgAt<I>>>()...));
    };
}

template<typename F>
inline Var::CallableV Var::wrapCallable(F&& fn) {
    using Traits = basic::FnTraits<std::decay_t<F>>;
    using Ret = typename Traits::RetT;

    if constexpr (std::is_convertible_v<std::decay_t<F>, CallableV>) {
        return CallableV(std::forward<F>(fn));
    } else if constexpr (Traits::ArgCnt == 0) {
        return [f = std::decay_t<F>(std::forward<F>(fn))](const Var& input) -> Var {
            (void)input;
            if constexpr (std::is_void_v<Ret>) {
                f();
                return detail::wrapCallableRet();
            }
            else {
                return detail::wrapCallableRet(f());
            }
        };
    } else if constexpr (Traits::ArgCnt == 1) {
        using Arg0 = std::decay_t<typename Traits::template ArgAt<0>>;
        if constexpr (std::is_same_v<Arg0, Var>) {
            return [f = std::decay_t<F>(std::forward<F>(fn))](const Var& input) -> Var {
                if constexpr (std::is_void_v<Ret>) {
                    f(input);
                    return detail::wrapCallableRet();
                } else {
                    return detail::wrapCallableRet(f(input));
                }
            };
        } else {
            return [f = std::decay_t<F>(std::forward<F>(fn))](const Var& input) -> Var {
                const Var& arg = input.isList() ? input[0] : input;
                if constexpr (std::is_void_v<Ret>) {
                    f(arg.template as<Arg0>());
                    return detail::wrapCallableRet();
                } else {
                    return detail::wrapCallableRet(f(arg.template as<Arg0>()));
                }
            };
        }
    }  else {
        return wrapCallableIndexed(std::forward<F>(fn), std::make_index_sequence<Traits::ArgCnt>{});
    }
}

} // namespace ve
