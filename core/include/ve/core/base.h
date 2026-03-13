// ----------------------------------------------------------------------------
// base.h
// ----------------------------------------------------------------------------
// Copyright (c) 2023-present Thilo and VersatileEngine contributors.
// Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
// See LICENSE file in the project root for full license information.
// ----------------------------------------------------------------------------
#pragma once

#include "ve/global.h"
#include "impl/ordered_hashmap.h"
#include "impl/small_vector.h"

// --- Type-trait generator macros -------------------------------------------
// VE_DECLARE_T_CHECKER(Name, ...)   → generates is-valid trait via SFINAE
// VE_DECLARE_T_FUNC_CHECKER(Name, Ret, ...)  → generates function-existence trait
// VE_INHERIT_CONSTRUCTOR(CTOR, Class, Base)   → inherits + adds converting ctors

#define VE_DECLARE_T_CHECKER(Checker, ...) \
template<typename T, typename dummy = void> struct Checker : std::false_type {}; \
template<typename T> struct Checker<T, __VA_ARGS__> : std::true_type {}

#define VE_DECLARE_T_FUNC_CHECKER(Checker, Ret, ...) \
VE_DECLARE_T_CHECKER(Checker, std::enable_if_t<std::is_same_v<__VA_ARGS__, Ret>, void>)

namespace ve {

namespace basic {

// --- Removed (C++17 standard equivalents) ---
//   enable_if_t<b,T>  → std::enable_if_t<b,T>
//   enable_if_void<b>  → std::enable_if_t<b>       (void is default)
//   disable_if_t<b,T>  → std::enable_if_t<!b,T>
//   _void_t<...>       → std::void_t<...>
//   _t_index_sequence   → std::index_sequence
//   _t_build_index_sequence → std::make_index_sequence

// --- Type comparability detection ---

template<typename L, typename R, class = void> struct is_comparable : std::false_type {};
template<typename L, typename R>
struct is_comparable<L, R, std::void_t<decltype(std::declval<L>() == std::declval<R>())>> : std::true_type {};

template<typename T>
static bool equals(const T& t1, const T& t2) {
    if constexpr (is_comparable<T, T>::value) return t1 == t2;
    else return false;
}

// --- Type list (for FInfo arg introspection) ---

template<typename...> struct _t_list { using FirstT = void; };
template<typename T0, typename... T> struct _t_list<T0, T...> { using FirstT = T0; using RestT = _t_list<T...>; };
template<typename T0, typename T1, typename... T> struct _t_list<T0, T1, T...> { using FirstT = T0; using SecondT = T1; using RestT = _t_list<T...>; };

// --- Static var helper (for is_inputable / YAML trait detection) ---

template<typename T> struct _t_static_var_helper { static inline T var; };

// --- Stream I/O detection traits ---

VE_DECLARE_T_FUNC_CHECKER(is_outputable, std::ostream, std::remove_reference_t<decltype(std::cout << std::declval<T>())>);
VE_DECLARE_T_FUNC_CHECKER(is_inputable, std::istream, std::remove_reference_t<decltype(std::cin >> _t_static_var_helper<T>::var)>);

// --- Function introspection (FInfo) ---

template<typename T> static decltype(&T::operator()) _t_functional(int);
template<typename T> static void _t_functional(short);

template<typename F> struct FInfo : public FInfo<decltype(_t_functional<F>(0))> {};
template<> struct FInfo<void> { enum { IsFunction = false }; };

template<typename Ret, typename... Args> struct FInfo<Ret (*) (Args...)>
{
    using RetT      = Ret;
    using ArgsT     = _t_list<Args...>;
    using FptrT     = Ret (*)(Args...);
    using FuncT     = Ret (*)(Args...);
    using FunctionT = std::function<Ret(Args...)>;
    enum { IsFunction = true, IsMember = false, ArgCnt = sizeof...(Args) };
};
template<typename Ret, typename Class, typename... Args> struct FInfo<Ret (Class::*) (Args...)>
{
    using RetT      = Ret;
    using ClassT    = Class;
    using ArgsT     = _t_list<Args...>;
    using FptrT     = Ret (*)(Args...);
    using FuncT     = Ret (Class::*)(Args...);
    using FunctionT = std::function<Ret(Args...)>;
    enum { IsFunction = true, IsMember = true, ArgCnt = sizeof...(Args) };
};
template<typename Ret, typename Class, typename... Args> struct FInfo<Ret (Class::*) (Args...) const>
{
    using RetT      = Ret;
    using ClassT    = Class;
    using ArgsT     = _t_list<Args...>;
    using FptrT     = Ret (*)(Args...);
    using FuncT     = Ret (Class::*)(Args...) const;
    using FunctionT = std::function<Ret(Args...)>;
    enum { IsFunction = true, IsMember = true, ArgCnt = sizeof...(Args) };
};

// --- Type utilities ---

template<typename T> using _t_remove_rc = std::remove_const_t<std::remove_reference_t<T>>;

VE_API std::string _t_demangle(const char* type_name);

template<typename T> struct Meta
{
    using TypeT = T;
    inline static const char* typeInfoName() { return typeid(T).name(); }
    inline static std::string typeName() { return _t_demangle(typeid(T).name()); }
};

// --- SFINAE helpers for function signature matching ---

template<typename Func1, typename Func2, typename Type>
using FIfSame = std::enable_if_t<std::is_same_v<typename FInfo<Func1>::FptrT, typename FInfo<Func2>::FptrT>, Type>;

template<typename Func1, typename Func2, typename Type>
using FIfConvertible = std::enable_if_t<
    std::is_convertible_v<Func1, Func2> && std::is_convertible_v<typename FInfo<Func1>::RetT, typename FInfo<Func2>::RetT>, Type>;

} // namespace basic

// pointer
template<typename T> using Pointer = std::shared_ptr<T>;

// --- Container mixin: inherits constructors + adds converting ctors ---
#define VE_INHERIT_CONSTRUCTOR(CONSTRUCTOR, CLASS, ...) \
public: \
using __VA_ARGS__::CONSTRUCTOR; \
using BaseT = __VA_ARGS__; \
CLASS() : BaseT() {} \
CLASS(const BaseT& other) noexcept : BaseT(other) {} \
CLASS(BaseT&& other) noexcept : BaseT(std::move(other)) {}

// --- Sequential container CRTP mixin ---

template<typename DerivedT, typename ValueT>
class PrivateTContainerBase
{
public:
    using ListLike = std::true_type;

protected:
    const DerivedT* dPtr() const { return static_cast<const DerivedT*>(this); }
    DerivedT* dPtr() { return static_cast<DerivedT*>(this); }

public:
    int sizeAsInt() const { return static_cast<int>(dPtr()->size()); }
    bool has(int index) const { return index >= 0 && index < sizeAsInt(); }
    ValueT value(int index) const { return has(index) ? dPtr()->operator[](index) : ValueT(); }
    ValueT value(int index, const ValueT& default_value) const { return has(index) ? dPtr()->operator[](index) : default_value; }
    ValueT* ptr(int index) { return has(index) ? &dPtr()->operator[](index) : nullptr; }
    const ValueT* ptr(int index) const { return has(index) ? &dPtr()->operator[](index) : nullptr; }

    DerivedT& insertOne(int index, const ValueT& value) { auto d = dPtr(); d->insert(d->begin() + index, value); return *d; }
    DerivedT& insertOne(int index, ValueT&& value) { auto d = dPtr(); d->insert(d->begin() + index, std::move(value)); return *d; }
    DerivedT& prepend(const ValueT& value) { auto d = dPtr(); d->insert(d->begin(), value); return *d; }
    DerivedT& prepend(ValueT&& value) { auto d = dPtr(); d->insert(d->begin(), std::move(value)); return *d; }
    DerivedT& append(const ValueT& value) { auto d = dPtr(); d->push_back(value); return *d; }
    DerivedT& append(ValueT&& value) { auto d = dPtr(); d->push_back(std::move(value)); return *d; }
    template<typename F> DerivedT& every(F&& f) { auto d = dPtr(); std::for_each(d->begin(), d->end(), std::forward<F>(f)); return *d; }
    template<typename F> const DerivedT& every(F&& f) const { const auto d = dPtr(); std::for_each(d->cbegin(), d->cend(), std::forward<F>(f)); return *d; }

    std::string toString(const std::string& sep = "") const
    {
        int s = sizeAsInt() - 1;
        if (s < 0) return "";
        std::ostringstream ss;
        int i = 0;
        every([&] (const ValueT& v) { (i++ < s) ? (ss << v << sep) : (ss << v); });
        return ss.str();
    }
};

template<typename T, std::size_t N> class Array : public std::array<T, N>, public PrivateTContainerBase<Array<T, N>, T>
{
    VE_INHERIT_CONSTRUCTOR(array, Array, std::array<T, N>)
};

template<typename T> class Vector : public std::vector<T>, public PrivateTContainerBase<Vector<T>, T>
{
    VE_INHERIT_CONSTRUCTOR(vector, Vector, std::vector<T>)
};

template<typename T> class List : public std::list<T>, public PrivateTContainerBase<List<T>, T>
{
    VE_INHERIT_CONSTRUCTOR(list, List, std::list<T>)

public:
    const T& operator[](int i) const
    {
        int cnt = 0;
        for (const auto& it : *this) if (cnt++ == i) return it;
        throw std::out_of_range("<ve> list out of range");
    }
    T& operator[](int i)
    {
        int cnt = 0;
        for (auto& it : *this) if (cnt++ == i) return it;
        throw std::out_of_range("<ve> list out of range");
    }
};

// --- SmallVector: inline SBO vector (default N = 1) -------------------------
// Same API as Vector, but stores up to N elements on the stack.
// Ideal for the common case where a container holds very few items
// (e.g. 1 child pointer per name group in Node's children map).

template<typename T, uint32_t N = 1>
class SmallVector : public impl::SmallVectorImpl<T, N>,
                    public PrivateTContainerBase<SmallVector<T, N>, T>
{
public:
    using ImplT = impl::SmallVectorImpl<T, N>;

    // --- inherit all constructors from impl ---
    using ImplT::SmallVectorImpl;

    SmallVector() : ImplT() {}
    SmallVector(const ImplT& other) : ImplT(other) {}
    SmallVector(ImplT&& other) noexcept : ImplT(std::move(other)) {}
};

// --- KV accessor policies ---

struct StdPairKVAccess {
    template<typename KV> static const auto& key(const KV& kv) { return kv.first; }
    template<typename KV> static auto& value(KV& kv) { return kv.second; }
    template<typename KV> static const auto& value(const KV& kv) { return kv.second; }
};

struct ImplKVAccess {
    template<typename KV> static const auto& key(const KV& kv) { return kv.key; }
    template<typename KV> static auto& value(KV& kv) { return kv.value; }
    template<typename KV> static const auto& value(const KV& kv) { return kv.value; }
};

// --- KV container CRTP mixin ---

template<typename DerivedT, typename KeyT, typename ValueT, typename KVAccessor = StdPairKVAccess>
class PrivateKVContainerBase
{
public:
    using MapLike = std::true_type;
    using DictLike = std::integral_constant<bool, std::is_same_v<KeyT, std::string>>;
    using KVAccessT = KVAccessor;

protected:
    const DerivedT* dPtr() const { return static_cast<const DerivedT*>(this); }
    DerivedT* dPtr() { return static_cast<DerivedT*>(this); }
    void fromKVVectors(const Vector<KeyT>& keys, const Vector<ValueT>& values)
    {
        for (size_t i = 0; i < std::min(keys.size(), values.size()); ++i) {
            dPtr()->operator[](keys[i]) = values[i];
        }
    }

public:
    int sizeAsInt() const { return static_cast<int>(dPtr()->size()); }
    bool has(const KeyT& key) const { return dPtr()->find(key) != dPtr()->end(); }
    Vector<KeyT> keys() const
    {
        Vector<KeyT> vec;
        vec.reserve(dPtr()->size());
        for (const auto& kv : *dPtr()) vec.push_back(KVAccessor::key(kv));
        return vec;
    }
    Vector<ValueT> values() const
    {
        Vector<ValueT> vec;
        vec.reserve(dPtr()->size());
        for (const auto& kv : *dPtr()) vec.push_back(KVAccessor::value(kv));
        return vec;
    }

    ValueT value(const KeyT& key) const
    {
        const auto it = dPtr()->find(key);
        return it == dPtr()->end() ? ValueT() : KVAccessor::value(*it);
    }
    ValueT value(const KeyT& key, const ValueT& default_value) const
    {
        const auto it = dPtr()->find(key);
        return it == dPtr()->end() ? default_value : KVAccessor::value(*it);
    }
    ValueT* ptr(const KeyT& key)
    {
        auto it = dPtr()->find(key);
        return it == dPtr()->end() ? nullptr : &KVAccessor::value(*it);
    }
    const ValueT* ptr(const KeyT& key) const
    {
        const auto it = dPtr()->find(key);
        return it == dPtr()->end() ? nullptr : &KVAccessor::value(*it);
    }

    DerivedT& insertOne(const KeyT& key, const ValueT& value) { auto d = dPtr(); d->operator[](key) = value; return *d; }
    DerivedT& insertOne(const KeyT& key, ValueT&& value) { auto d = dPtr(); d->operator[](key) = std::move(value); return *d; }
};

template<typename K, typename V>
class Map : public std::map<K, V>, public PrivateKVContainerBase<Map<K, V>, K, V>
{
    VE_INHERIT_CONSTRUCTOR(map, Map, std::map<K, V>)
    Map(const Vector<K>& keys, const Vector<V>& values) { this->fromKVVectors(keys, values); }
};

template<typename K, typename V>
class UnorderedHashMap : public std::unordered_map<K, V>, public PrivateKVContainerBase<UnorderedHashMap<K, V>, K, V>
{
    VE_INHERIT_CONSTRUCTOR(unordered_map, UnorderedHashMap, std::unordered_map<K, V>)
    UnorderedHashMap(const Vector<K>& keys, const Vector<V>& values) { this->fromKVVectors(keys, values); }
};

template<typename V>
using Hash = UnorderedHashMap<std::string, V>;

// --- Ordered hash map (insertion-ordered, Robin Hood hashing) ---
// Adapted from Godot Engine's HashMap (see ve/core/impl/ for license).
// Iteration yields impl::KeyValue<K,V> with .key / .value members.
template<typename K, typename V,
         typename Hasher     = impl::HashMapHasherDefault,
         typename Comparator = impl::HashMapComparatorDefault<K>>
class OrderedHashMap
    : public impl::InsertionOrderedHashMap<K, V, Hasher, Comparator>, public PrivateKVContainerBase<OrderedHashMap<K, V, Hasher, Comparator>, K, V, ImplKVAccess>
{
    using ImplBase = impl::InsertionOrderedHashMap<K, V, Hasher, Comparator>;
public:
    VE_INHERIT_CONSTRUCTOR(InsertionOrderedHashMap, OrderedHashMap, ImplBase)
    OrderedHashMap(const Vector<K>& keys, const Vector<V>& values) { this->fromKVVectors(keys, values); }

    using ImplBase::has;
};

template<typename V>
using Dict = OrderedHashMap<std::string, V>;

using Ints = Vector<int>;
using Doubles = Vector<double>;
using Strings = Vector<std::string>;

class VE_API Values : public Doubles
{
    VE_INHERIT_CONSTRUCTOR(Doubles, Values, Doubles)

public:
    enum Unit : int {
        NONE    = 0x0000,
        M       = 0x0100,
        MM      = 0x0101,
        DEGREE  = 0x0200,
        RAD     = 0x0201,
        SAME    = 0xffff
    };

    Unit unit() const;
    Values& setUnit(Unit unit);

    Values& add(double d);
    Values& multiply(double d, Unit new_unit = SAME);

    Values& mm2m();
    Values& m2mm();
    Values& degree2rad();
    Values& rad2degree();

    bool smallerThan(const Values& other) const;
    bool greaterThan(const Values& other) const;
    bool equals(const Values& other) const;

    bool operator<(const Values& other) const { return smallerThan(other); }
    bool operator>(const Values& other) const { return greaterThan(other); }
    bool operator==(const Values& other) const { return equals(other); }

private:
    Unit m_unit = NONE;
};

}
