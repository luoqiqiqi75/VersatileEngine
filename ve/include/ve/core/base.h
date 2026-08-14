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

#define VE_DECLARE_T_CHECKER(Checker, ...) \
template<typename T, typename dummy = void> struct Checker : std::false_type {}; \
template<typename T> struct Checker<T, __VA_ARGS__> : std::true_type {}

#define VE_DECLARE_T_FUNC_CHECKER(Checker, Ret, ...) \
VE_DECLARE_T_CHECKER(Checker, std::enable_if_t<std::is_same_v<__VA_ARGS__, Ret>, void>)

namespace ve {

namespace basic {

template<typename L, typename R, class = void> struct is_comparable : std::false_type {};
template<typename L, typename R>
struct is_comparable<L, R, std::void_t<decltype(std::declval<L>() == std::declval<R>())>> : std::true_type {};

template<typename L, typename R, class = void> struct is_less_than_comparable : std::false_type {};
template<typename L, typename R>
struct is_less_than_comparable<L, R, std::void_t<decltype(std::declval<L>() < std::declval<R>())>> : std::true_type {};

template<typename L, typename R> inline constexpr bool is_comparable_v = is_comparable<L, R>::value;
template<typename L, typename R> inline constexpr bool is_less_than_comparable_v = is_less_than_comparable<L, R>::value;

template<typename T>
static bool equals(const T& t1, const T& t2) {
    if constexpr (is_comparable_v<T, T>) return t1 == t2;
    else return false;
}

// type list (for FnTraits arg access)

template<typename...> struct _t_list { using FirstT = void; static constexpr std::size_t Size = 0; };
template<typename T0, typename... T> struct _t_list<T0, T...> { using FirstT = T0; using RestT = _t_list<T...>; static constexpr std::size_t Size = 1 + sizeof...(T); };
template<typename T0, typename T1, typename... T> struct _t_list<T0, T1, T...> { using FirstT = T0; using SecondT = T1; using RestT = _t_list<T...>; static constexpr std::size_t Size = 2 + sizeof...(T); };

template<std::size_t N, typename List> struct _t_list_at;
template<std::size_t N, typename T0, typename... Ts>
struct _t_list_at<N, _t_list<T0, Ts...>> : _t_list_at<N - 1, _t_list<Ts...>> {};
template<typename T0, typename... Ts>
struct _t_list_at<0, _t_list<T0, Ts...>> { using type = T0; };
template<std::size_t N, typename List>
using _t_list_at_t = typename _t_list_at<N, List>::type;

template<typename T> struct _t_static_var_helper { static inline T var; };

VE_DECLARE_T_FUNC_CHECKER(is_outputable, std::ostream, std::remove_reference_t<decltype(std::cout << std::declval<T>())>);
VE_DECLARE_T_FUNC_CHECKER(is_inputable, std::istream, std::remove_reference_t<decltype(std::cin >> _t_static_var_helper<T>::var)>);

template<typename T> inline constexpr bool is_outputable_v = is_outputable<T>::value;
template<typename T> inline constexpr bool is_inputable_v = is_inputable<T>::value;

// FnTraits<F> — unified callable introspection
//
// Non-decayed F (e.g. void (&)(int)) forwards to FnTraits<std::decay_t<F>> so function references
// match Ret (*)(Args...) specializations. _t_functional is only used after decay.

template<typename T> static decltype(&T::operator()) _t_functional(int);
template<typename T> static void _t_functional(short);

template<typename F>
struct FnTraits : public std::conditional_t<
    std::is_same_v<F, std::decay_t<F>>,
    FnTraits<decltype(_t_functional<F>(0))>,
    FnTraits<std::decay_t<F>>> {};
template<> struct FnTraits<void> { enum { IsFunction = false }; };

template<typename Ret, typename... Args> struct FnTraits<Ret (*) (Args...)>
{
    using RetT      = Ret;
    using ArgsT     = _t_list<Args...>;
    using ArgsTuple = std::tuple<Args...>;
    template<size_t I> using ArgAt = std::tuple_element_t<I, ArgsTuple>;
    using FptrT     = Ret (*)(Args...);
    using FuncT     = Ret (*)(Args...);
    using FunctionT = std::function<Ret(Args...)>;
    enum { IsFunction = true, IsMember = false, IsNoexcept = false, ArgCnt = sizeof...(Args) };
};

template<typename Ret, typename Class, typename... Args> struct FnTraits<Ret (Class::*) (Args...)>
{
    using RetT      = Ret;
    using ClassT    = Class;
    using ArgsT     = _t_list<Args...>;
    using ArgsTuple = std::tuple<Args...>;
    template<size_t I> using ArgAt = std::tuple_element_t<I, ArgsTuple>;
    using FptrT     = Ret (*)(Args...);
    using FuncT     = Ret (Class::*)(Args...);
    using FunctionT = std::function<Ret(Args...)>;
    enum { IsFunction = true, IsMember = true, IsNoexcept = false, ArgCnt = sizeof...(Args) };
};

template<typename Ret, typename Class, typename... Args> struct FnTraits<Ret (Class::*) (Args...) const>
{
    using RetT      = Ret;
    using ClassT    = Class;
    using ArgsT     = _t_list<Args...>;
    using ArgsTuple = std::tuple<Args...>;
    template<size_t I> using ArgAt = std::tuple_element_t<I, ArgsTuple>;
    using FptrT     = Ret (*)(Args...);
    using FuncT     = Ret (Class::*)(Args...) const;
    using FunctionT = std::function<Ret(Args...)>;
    enum { IsFunction = true, IsMember = true, IsNoexcept = false, ArgCnt = sizeof...(Args) };
};

template<typename Ret, typename... Args> struct FnTraits<Ret (*) (Args...) noexcept>
{
    using RetT      = Ret;
    using ArgsT     = _t_list<Args...>;
    using ArgsTuple = std::tuple<Args...>;
    template<size_t I> using ArgAt = std::tuple_element_t<I, ArgsTuple>;
    using FptrT     = Ret (*)(Args...);
    using FuncT     = Ret (*)(Args...) noexcept;
    using FunctionT = std::function<Ret(Args...)>;
    enum { IsFunction = true, IsMember = false, IsNoexcept = true, ArgCnt = sizeof...(Args) };
};
template<typename Ret, typename Class, typename... Args> struct FnTraits<Ret (Class::*) (Args...) noexcept>
{
    using RetT      = Ret;
    using ClassT    = Class;
    using ArgsT     = _t_list<Args...>;
    using ArgsTuple = std::tuple<Args...>;
    template<size_t I> using ArgAt = std::tuple_element_t<I, ArgsTuple>;
    using FptrT     = Ret (*)(Args...);
    using FuncT     = Ret (Class::*)(Args...) noexcept;
    using FunctionT = std::function<Ret(Args...)>;
    enum { IsFunction = true, IsMember = true, IsNoexcept = true, ArgCnt = sizeof...(Args) };
};
template<typename Ret, typename Class, typename... Args> struct FnTraits<Ret (Class::*) (Args...) const noexcept>
{
    using RetT      = Ret;
    using ClassT    = Class;
    using ArgsT     = _t_list<Args...>;
    using ArgsTuple = std::tuple<Args...>;
    template<size_t I> using ArgAt = std::tuple_element_t<I, ArgsTuple>;
    using FptrT     = Ret (*)(Args...);
    using FuncT     = Ret (Class::*)(Args...) const noexcept;
    using FunctionT = std::function<Ret(Args...)>;
    enum { IsFunction = true, IsMember = true, IsNoexcept = true, ArgCnt = sizeof...(Args) };
};

template<typename Ret, typename... Args> struct FnTraits<std::function<Ret(Args...)>>
{
    using RetT      = Ret;
    using ArgsT     = _t_list<Args...>;
    using ArgsTuple = std::tuple<Args...>;
    template<size_t I> using ArgAt = std::tuple_element_t<I, ArgsTuple>;
    using FptrT     = Ret (*)(Args...);
    using FuncT     = std::function<Ret(Args...)>;
    using FunctionT = std::function<Ret(Args...)>;
    enum { IsFunction = true, IsMember = false, IsNoexcept = false, ArgCnt = sizeof...(Args) };
};

template<typename F> using FInfo = FnTraits<F>;

template<typename T> using _t_remove_rc  = std::remove_const_t<std::remove_reference_t<T>>;
template<typename T> using _t_remove_rcv = std::remove_cv_t<std::remove_reference_t<T>>;
template<typename T> using _t_bare       = std::remove_cv_t<std::remove_pointer_t<std::remove_reference_t<T>>>;

VE_API std::string _t_demangle(const char* type_name);

// Meta<T> — compile-time + runtime type descriptor
template<typename T> struct Meta
{
    using TypeT    = T;
    using NoCVRefT = std::remove_cv_t<std::remove_reference_t<T>>;
    using BareT    = _t_bare<T>;
    using DecayT   = std::decay_t<T>;

    static const std::type_info& typeId()     { return typeid(T); }
    static const std::type_info& bareTypeId() { return typeid(BareT); }
    static const char*           typeIdName() { return typeid(T).name(); }
    static std::string           typeName()   { return _t_demangle(typeid(T).name()); }
    static std::string           bareName()   { return _t_demangle(typeid(BareT).name()); }

    // qualifiers
    static constexpr bool is_const    = std::is_const_v<std::remove_reference_t<T>>;
    static constexpr bool is_volatile = std::is_volatile_v<std::remove_reference_t<T>>;
    static constexpr bool is_lref     = std::is_lvalue_reference_v<T>;
    static constexpr bool is_rref     = std::is_rvalue_reference_v<T>;
    static constexpr bool is_ref      = std::is_reference_v<T>;
    static constexpr bool is_ptr      = std::is_pointer_v<std::remove_reference_t<T>>;

    // category
    static constexpr bool is_void       = std::is_void_v<BareT>;
    static constexpr bool is_arithmetic = std::is_arithmetic_v<BareT>;
    static constexpr bool is_integral   = std::is_integral_v<BareT>;
    static constexpr bool is_floating   = std::is_floating_point_v<BareT>;
    static constexpr bool is_enum       = std::is_enum_v<BareT>;
    static constexpr bool is_class      = std::is_class_v<BareT>;
    static constexpr bool is_numeric    = std::is_arithmetic_v<NoCVRefT>;
    static constexpr bool is_string     = std::is_same_v<NoCVRefT, std::string>
                                       || std::is_same_v<NoCVRefT, const char*>
                                       || std::is_same_v<NoCVRefT, char*>;

    // capability
    static constexpr bool is_copyable    = std::is_copy_constructible_v<BareT>;
    static constexpr bool is_movable     = std::is_move_constructible_v<BareT>;
    static constexpr bool is_trivial     = std::is_trivially_copyable_v<BareT>;
    static constexpr bool is_abstract    = std::is_abstract_v<BareT>;
    static constexpr bool is_polymorphic = std::is_polymorphic_v<BareT>;

    // callable
    static constexpr bool is_function_ptr        = std::is_pointer_v<NoCVRefT>
                                                && std::is_function_v<std::remove_pointer_t<NoCVRefT>>;
    static constexpr bool is_member_function_ptr = std::is_member_function_pointer_v<NoCVRefT>;
    static constexpr bool is_callable            = FnTraits<NoCVRefT>::IsFunction;
    static constexpr bool is_raw_pointer         = std::is_pointer_v<NoCVRefT>
                                                && !is_function_ptr
                                                && !is_string;

    static constexpr size_t typeSize() {
        if constexpr (is_void) return 0;
        else return sizeof(T);
    }
    static constexpr size_t typeAlign() {
        if constexpr (is_void) return 0;
        else return alignof(T);
    }

    static std::string describe() {
        std::string s = typeName();
        std::string q;
        if constexpr (is_const)    q += "const ";
        if constexpr (is_volatile) q += "volatile ";
        if constexpr (is_lref)     q += "& ";
        if constexpr (is_rref)     q += "&& ";
        if constexpr (is_ptr)      q += "* ";
        if (!q.empty()) q.pop_back();

        std::string c;
        if      constexpr (is_void)         c = "void";
        else if constexpr (is_callable)     c = "callable";
        else if constexpr (is_integral)     c = "integral";
        else if constexpr (is_floating)     c = "floating";
        else if constexpr (is_enum)         c = "enum";
        else if constexpr (is_class)        c = "class";
        else if constexpr (is_raw_pointer)  c = "pointer";
        else if constexpr (is_ptr)          c = "pointer";

        if constexpr (!is_void) {
            c += ", " + std::to_string(typeSize()) + "B";
        }

        if (!q.empty() || !c.empty()) {
            s += " [";
            if (!q.empty()) s += q;
            if (!q.empty() && !c.empty()) s += "] (";
            else if (!c.empty()) s += "(";
            if (!c.empty()) s += c + ")";
            else s += "]";
        }
        return s;
    }
};

// detection traits

template<typename T, class = void> struct is_hashable : std::false_type {};
template<typename T>
struct is_hashable<T, std::void_t<decltype(std::hash<T>{}(std::declval<T>()))>> : std::true_type {};
template<typename T> inline constexpr bool is_hashable_v = is_hashable<T>::value;

template<typename T, class = void> struct is_iterable : std::false_type {};
template<typename T>
struct is_iterable<T, std::void_t<
    decltype(std::begin(std::declval<T&>())),
    decltype(std::end(std::declval<T&>()))>> : std::true_type {};
template<typename T> inline constexpr bool is_iterable_v = is_iterable<T>::value;

template<typename T, class = void> struct is_string_like : std::false_type {};
template<typename T>
struct is_string_like<T, std::void_t<decltype(std::string(std::declval<T>()))>> : std::true_type {};
template<typename T> inline constexpr bool is_string_like_v = is_string_like<T>::value;

// enum traits

template<typename T, class = void>
struct is_scoped_enum : std::false_type {};
template<typename T>
struct is_scoped_enum<T, std::enable_if_t<
    std::is_enum_v<T> && !std::is_convertible_v<T, std::underlying_type_t<T>>>> : std::true_type {};
template<typename T> inline constexpr bool is_scoped_enum_v = is_scoped_enum<T>::value;

template<typename E>
constexpr auto to_underlying(E e) noexcept
    -> std::enable_if_t<std::is_enum_v<E>, std::underlying_type_t<E>>
{ return static_cast<std::underlying_type_t<E>>(e); }

// smart pointer detection

template<typename T> struct is_smart_pointer : std::false_type {};
template<typename T> struct is_smart_pointer<std::shared_ptr<T>> : std::true_type {};
template<typename T> struct is_smart_pointer<std::unique_ptr<T>> : std::true_type {};
template<typename T> struct is_smart_pointer<std::weak_ptr<T>> : std::true_type {};
template<typename T> inline constexpr bool is_smart_pointer_v = is_smart_pointer<T>::value;

// callable / pointer classification (from Meta<T>)

template<typename T> inline constexpr bool is_callable_v     = Meta<T>::is_callable;
template<typename T> inline constexpr bool is_function_ptr_v = Meta<T>::is_function_ptr;
template<typename T> inline constexpr bool is_raw_pointer_v  = Meta<T>::is_raw_pointer;

// SFINAE helpers for function signature matching

template<typename Func1, typename Func2, typename Type>
using FIfSame = std::enable_if_t<std::is_same_v<typename FnTraits<Func1>::FptrT, typename FnTraits<Func2>::FptrT>, Type>;

template<typename Func1, typename Func2, typename Type>
using FIfConvertible = std::enable_if_t<std::is_convertible_v<Func1, Func2> && std::is_convertible_v<typename FnTraits<Func1>::RetT, typename FnTraits<Func2>::RetT>, Type>;

} // namespace basic

namespace flags {

inline bool get(int flags, int f) { return (flags & f) == f; }
inline int set(int& flags, int f, bool on_off) { return flags = on_off ? flags | f : flags & ~f; }

}

// container mixin: inherits constructors + adds converting ctors
#define VE_INHERIT_CONSTRUCTOR(CONSTRUCTOR, CLASS, ...) \
public: \
using __VA_ARGS__::CONSTRUCTOR; \
using BaseT = __VA_ARGS__; \
CLASS() : BaseT() {} \
CLASS(const BaseT& other) noexcept : BaseT(other) {} \
CLASS(BaseT&& other) noexcept : BaseT(std::move(other)) {}

namespace basic {

// sequential container CRTP mixin

template<typename DerivedT, typename ValueT>
class TContainer
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

template<typename T, typename = void>
struct is_list_like : std::false_type {};

template<typename T>
struct is_list_like<T, std::void_t<typename T::ListLike>> : std::is_same<typename T::ListLike, std::true_type> {};

}

template<typename T, std::size_t N> class Array : public std::array<T, N>, public basic::TContainer<Array<T, N>, T>
{
    VE_INHERIT_CONSTRUCTOR(array, Array, std::array<T, N>)
};

template<typename T> class Vector : public std::vector<T>, public basic::TContainer<Vector<T>, T>
{
    VE_INHERIT_CONSTRUCTOR(vector, Vector, std::vector<T>)
};

template<typename T> class List : public std::list<T>, public basic::TContainer<List<T>, T>
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

// SmallVector: SBO vector (default N = 1), same API as Vector

template<typename T, uint32_t N = 1>
class SmallVector : public impl::SmallVectorImpl<T, N>, public basic::TContainer<SmallVector<T, N>, T>
{
    VE_INHERIT_CONSTRUCTOR(SmallVectorImpl, SmallVector, impl::SmallVectorImpl<T, N>)
};

namespace basic {

// KV container CRTP mixin

template<typename DerivedT, typename KeyT, typename ValueT>
class KVContainer
{
public:
    using MapLike = std::true_type;
    using DictLike = std::integral_constant<bool, std::is_same_v<KeyT, std::string>>;

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
        for (const auto& kv : *dPtr()) vec.push_back(kv.first);
        return vec;
    }
    Vector<ValueT> values() const
    {
        Vector<ValueT> vec;
        vec.reserve(dPtr()->size());
        for (const auto& kv : *dPtr()) vec.push_back(kv.second);
        return vec;
    }

    ValueT value(const KeyT& key) const
    {
        const auto it = dPtr()->find(key);
        return it == dPtr()->end() ? ValueT() : it->second;
    }
    ValueT value(const KeyT& key, const ValueT& default_value) const
    {
        const auto it = dPtr()->find(key);
        return it == dPtr()->end() ? default_value : it->second;
    }
    ValueT* ptr(const KeyT& key)
    {
        auto it = dPtr()->find(key);
        return it == dPtr()->end() ? nullptr : &it->second;
    }
    const ValueT* ptr(const KeyT& key) const
    {
        const auto it = dPtr()->find(key);
        return it == dPtr()->end() ? nullptr : &it->second;
    }

    DerivedT& insertOne(const KeyT& key, const ValueT& value) { auto d = dPtr(); d->operator[](key) = value; return *d; }
    DerivedT& insertOne(const KeyT& key, ValueT&& value) { auto d = dPtr(); d->operator[](key) = std::move(value); return *d; }
};

template<typename T, typename = void>
struct is_dict_like : std::false_type {};

template<typename T>
struct is_dict_like<T, std::void_t<typename T::DictLike>> : std::integral_constant<bool, T::DictLike::value> {};

}

template<typename K, typename V>
class Map : public std::map<K, V>, public basic::KVContainer<Map<K, V>, K, V>
{
    VE_INHERIT_CONSTRUCTOR(map, Map, std::map<K, V>)
    Map(const Vector<K>& keys, const Vector<V>& values) { this->fromKVVectors(keys, values); }
};

template<typename K, typename V>
class UnorderedHashMap : public std::unordered_map<K, V>, public basic::KVContainer<UnorderedHashMap<K, V>, K, V>
{
    VE_INHERIT_CONSTRUCTOR(unordered_map, UnorderedHashMap, std::unordered_map<K, V>)
    UnorderedHashMap(const Vector<K>& keys, const Vector<V>& values) { this->fromKVVectors(keys, values); }
};

template<typename V>
using Hash = UnorderedHashMap<std::string, V>;

// OrderedHashMap: insertion-ordered Robin Hood hashing (Godot-derived)
template<typename K, typename V, typename Hasher = impl::HashMapHasherDefault, typename Comparator = impl::HashMapComparatorDefault<K>>
class OrderedHashMap
    : public impl::InsertionOrderedHashMap<K, V, Hasher, Comparator>, public basic::KVContainer<OrderedHashMap<K, V, Hasher, Comparator>, K, V>
{
    using ImplBase = impl::InsertionOrderedHashMap<K, V, Hasher, Comparator>;
public:
    VE_INHERIT_CONSTRUCTOR(InsertionOrderedHashMap, OrderedHashMap, ImplBase)
    OrderedHashMap(const Vector<K>& keys, const Vector<V>& values) { this->fromKVVectors(keys, values); }

    using ImplBase::has;
};

template<typename T>
inline constexpr bool is_list_like_v = basic::is_list_like<T>::value;

using Ints = Vector<int>;
using Doubles = Vector<double>;
using Strings = Vector<std::string>;

using Bytes = Vector<std::uint8_t>;

class VE_API Values : public Doubles
{
    VE_INHERIT_CONSTRUCTOR(Doubles, Values, Doubles)

public:
    enum Unit : int {
        NONE                = 0x0000,

        // Length
        M                   = 0x0100,
        MM                  = 0x0101,
        CM                  = 0x0102,
        KM                  = 0x0103,

        // Angle
        DEGREE              = 0x0200,
        RAD                 = 0x0201,

        // Time
        SECOND              = 0x0300,
        MILLISECOND         = 0x0301,
        MICROSECOND         = 0x0302,
        NANOSECOND          = 0x0303,

        // Linear velocity
        M_PER_S             = 0x0400,
        MM_PER_S            = 0x0401,
        KM_PER_H            = 0x0402,

        // Linear acceleration
        M_PER_S2            = 0x0500,
        MM_PER_S2           = 0x0501,
        G_FORCE             = 0x0502,

        // Angular velocity
        RAD_PER_S           = 0x0600,
        DEGREE_PER_S        = 0x0601,
        RPM                 = 0x0602,

        // Angular acceleration
        RAD_PER_S2          = 0x0700,
        DEGREE_PER_S2       = 0x0701,

        // Frequency
        HZ                  = 0x0800,
        KHZ                 = 0x0801,
        MHZ                 = 0x0802,

        // Mass
        KG                  = 0x0900,
        GRAM                = 0x0901,

        // Mechanics
        NEWTON              = 0x0a00,
        NEWTON_METER        = 0x0b00,
        PASCAL              = 0x0c00,
        KILOPASCAL          = 0x0c01,
        JOULE               = 0x0d00,
        WATT                = 0x0e00,

        // Electrical
        VOLT                = 0x0f00,
        AMPERE              = 0x1000,

        // Temperature
        CELSIUS             = 0x1100,
        KELVIN              = 0x1101,

        // Dimensionless quantities
        RATIO               = 0x1200,
        PERCENT             = 0x1201
    };

    Unit unit() const;
    Values& setUnit(Unit unit);

    Values& add(double d);
    Values& multiply(double d);

    template<Unit U> Values& addToUnit(double d) { return unit() == U ? *this : add(d).setUnit(U); }
    template<Unit U> Values& multiplyToUnit(double d) { return unit() == U ? *this : multiply(d).setUnit(U); }

    bool isFinite() const;

    bool smallerThan(const Values& other) const;
    bool greaterThan(const Values& other) const;

    bool operator<(const Values& other) const { return smallerThan(other); }
    bool operator>(const Values& other) const { return greaterThan(other); }
    bool operator==(const Values& other) const { return approximate(other); }

    // Arithmetic operators (scalar)
    Values& operator+=(double v) { return add(v); }
    Values& operator-=(double v) { return add(-v); }
    Values& operator*=(double v) { return multiply(v); }
    Values& operator/=(double v) { return multiply(1.0 / v); }

    Values operator+(double v) const { return Values(*this) += v; }
    Values operator-(double v) const { return Values(*this) -= v; }
    Values operator*(double v) const { return Values(*this) *= v; }
    Values operator/(double v) const { return Values(*this) /= v; }

    // Arithmetic operators (vector)
    Values& operator+=(const Values& o);
    Values& operator-=(const Values& o);

    Values operator+(const Values& o) const { return Values(*this) += o; }
    Values operator-(const Values& o) const { return Values(*this) -= o; }

    // Concatenation
    using Doubles::append;
    Values& append(const Values& o);
    Values operator|(const Values& o) const { return Values(*this).append(o); }

    // Math utilities
    double sum() const;
    double norm() const;
    double dot(const Values& o) const;
    double distance(const Values& o) const;
    bool approximate(const Values& o, double epsilon = 0.0001) const;

    Values& normalize();
    Values& clamp(double min, double max);

public:
    // Common optimistic conversions: a value already in the target unit is left unchanged.
    Values& mm2m() { return multiplyToUnit<M>(0.001); }
    Values& m2mm() { return multiplyToUnit<MM>(1000.0); }
    Values& cm2m() { return multiplyToUnit<M>(0.01); }
    Values& m2cm() { return multiplyToUnit<CM>(100.0); }
    Values& km2m() { return multiplyToUnit<M>(1000.0); }
    Values& m2km() { return multiplyToUnit<KM>(0.001); }

    Values& degree2rad() { return multiplyToUnit<RAD>(0.017453292519943295); }
    Values& rad2degree() { return multiplyToUnit<DEGREE>(57.29577951308232); }

    Values& second2millisecond() { return multiplyToUnit<MILLISECOND>(1000.0); }
    Values& millisecond2second() { return multiplyToUnit<SECOND>(0.001); }
    Values& second2microsecond() { return multiplyToUnit<MICROSECOND>(1.0e6); }
    Values& microsecond2second() { return multiplyToUnit<SECOND>(1.0e-6); }
    Values& second2nanosecond() { return multiplyToUnit<NANOSECOND>(1.0e9); }
    Values& nanosecond2second() { return multiplyToUnit<SECOND>(1.0e-9); }

    Values& mps2mmps() { return multiplyToUnit<MM_PER_S>(1000.0); }
    Values& mmps2mps() { return multiplyToUnit<M_PER_S>(0.001); }
    Values& mps2kmph() { return multiplyToUnit<KM_PER_H>(3.6); }
    Values& kmph2mps() { return multiplyToUnit<M_PER_S>(1.0 / 3.6); }
    Values& mpss2mmpss() { return multiplyToUnit<MM_PER_S2>(1000.0); }
    Values& mmpss2mpss() { return multiplyToUnit<M_PER_S2>(0.001); }
    Values& mpss2gForce() { return multiplyToUnit<G_FORCE>(1.0 / 9.80665); }
    Values& gForce2mpss() { return multiplyToUnit<M_PER_S2>(9.80665); }

    Values& degreePerS2radPerS() { return multiplyToUnit<RAD_PER_S>(0.017453292519943295); }
    Values& radPerS2degreePerS() { return multiplyToUnit<DEGREE_PER_S>(57.29577951308232); }
    Values& rpm2radPerS() { return multiplyToUnit<RAD_PER_S>(0.10471975511965977); }
    Values& radPerS2rpm() { return multiplyToUnit<RPM>(9.549296585513721); }
    Values& degreePerS22radPerS2() { return multiplyToUnit<RAD_PER_S2>(0.017453292519943295); }
    Values& radPerS22degreePerS2() { return multiplyToUnit<DEGREE_PER_S2>(57.29577951308232); }

    Values& hz2khz() { return multiplyToUnit<KHZ>(0.001); }
    Values& khz2hz() { return multiplyToUnit<HZ>(1000.0); }
    Values& hz2mhz() { return multiplyToUnit<MHZ>(1.0e-6); }
    Values& mhz2hz() { return multiplyToUnit<HZ>(1.0e6); }
    Values& kg2gram() { return multiplyToUnit<GRAM>(1000.0); }
    Values& gram2kg() { return multiplyToUnit<KG>(0.001); }
    Values& pascal2kilopascal() { return multiplyToUnit<KILOPASCAL>(0.001); }
    Values& kilopascal2pascal() { return multiplyToUnit<PASCAL>(1000.0); }
    Values& ratio2percent() { return multiplyToUnit<PERCENT>(100.0); }
    Values& percent2ratio() { return multiplyToUnit<RATIO>(0.01); }
    Values& celsius2kelvin() { return addToUnit<KELVIN>(273.15); }
    Values& kelvin2celsius() { return addToUnit<CELSIUS>(-273.15); }

private:
    Unit unit_ = NONE;
};

template<typename T>
inline constexpr bool is_dict_like_v = basic::is_dict_like<T>::value;

template<typename V>
using Dict = OrderedHashMap<std::string, V>;

class VE_API Entity
{
public:
    explicit Entity(const std::string& name = "") : _name(name) {}
    virtual ~Entity() = default;

    Entity(const Entity&) = delete;
    Entity& operator=(const Entity&) = delete;

    const std::string& name() const { return _name; }

protected:
    int _flags = 0;

private:
    std::string _name;
};

using Task = std::function<void()>;

// Result — defined in var.h (requires full Var type).
// Layout: { int code; std::string message; Var data; }
// See var.h for the full definition + factory methods.

}
