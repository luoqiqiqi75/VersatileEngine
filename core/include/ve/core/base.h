// ----------------------------------------------------------------------------
// base.h
// ----------------------------------------------------------------------------
// Copyright (c) 2023-present Thilo and VersatileEngine contributors.
// Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
// See LICENSE file in the project root for full license information.
// ----------------------------------------------------------------------------
#pragma once

#include "ve/global.h"
#include "ve/core/impl/ordered_hashmap.h"

#define PRIVATE_DECLARE_T_CHECKER(Checker, ...) \
template<typename T, typename dummy = void> struct Checker : std::false_type {}; \
template<typename T> struct Checker<T, __VA_ARGS__> : std::true_type {}

#define PRIVATE_DECLARE_T_FUNC_CHECKER(Checker, Ret, ...) \
PRIVATE_DECLARE_T_CHECKER(Checker, typename std::enable_if<std::is_same<__VA_ARGS__, Ret>::value, void>::type)

constexpr const char* VE_UNDEFINED_OBJECT_NAME = "@undefined";

namespace ve {

namespace basic {

template<bool b, typename T> using enable_if_t = typename std::enable_if<b, T>::type;
template<bool b> using enable_if_void = typename std::enable_if<b, void>::type;
template<bool b, typename T> using disable_if_t = typename std::enable_if<!b, T>::type;

template<class ...> using _void_t = void;
template<typename L, typename R, class = void> struct is_comparable : std::false_type {};
template<typename L, typename R> using _t_comparability = decltype(std::declval<L>() == std::declval<R>());
template<typename L, typename R> struct is_comparable<L,R,_void_t<_t_comparability<L,R>>> : std::true_type {};

template<typename T> inline static typename std::enable_if<is_comparable<T,T>::value, bool>::type equals(const T& t1, const T& t2) { return t1 == t2; }
template<typename T> inline static typename std::enable_if<!is_comparable<T,T>::value, bool>::type equals(const T& t1, const T& t2) { return false; }

template<typename...> struct _t_list { typedef void FirstT; };
template<typename T0, typename... T> struct _t_list<T0, T...> { typedef T0 FirstT; typedef _t_list<T...> RestT; };
template<typename T0, typename T1, typename... T> struct _t_list<T0, T1, T...> { typedef T0 FirstT; typedef T1 SecondT; typedef _t_list<T...> RestT; };

template<size_t... Is> struct _t_index_sequence {};
template<size_t N, size_t... Is> struct _t_build_index_sequence : _t_build_index_sequence<N - 1, N - 1, Is...> {};
template<size_t... Is> struct _t_build_index_sequence<0, Is...> : _t_index_sequence<Is...> {};

template<typename T> struct _t_static_var_helper { static T var; };

PRIVATE_DECLARE_T_FUNC_CHECKER(is_outputable, std::ostream, std::remove_reference_t<decltype(std::cout << std::declval<T>())>);
PRIVATE_DECLARE_T_FUNC_CHECKER(is_inputable, std::istream, std::remove_reference_t<decltype(std::cin >> _t_static_var_helper<T>::var)>);

template<typename T> static decltype(&T::operator()) _t_functional(int);
template<typename T> static void _t_functional(short);

template<typename F> struct FInfo : public FInfo<decltype(_t_functional<F>(0))> {};
template<> struct FInfo<void> { enum { IsFunction = false }; };

template<typename Ret, typename... Args> struct FInfo<Ret (*) (Args...)>
{
    typedef Ret RetT;
    typedef _t_list<Args...> ArgsT;
    typedef Ret (*FptrT) (Args...);
    typedef Ret (*FuncT) (Args...);
    typedef std::function<Ret(Args...)> FunctionT;
    enum { IsFunction = true, IsMember = false, ArgCnt = sizeof...(Args) };
};
template<typename Ret, typename Class, typename... Args> struct FInfo<Ret (Class::*) (Args...)>
{
    typedef Ret RetT;
    typedef Class ClassT;
    typedef _t_list<Args...> ArgsT;
    typedef Ret (*FptrT) (Args...);
    typedef Ret (Class::*FuncT) (Args...);
    typedef std::function<Ret(Args...)> FunctionT;
    enum { IsFunction = true, IsMember = true, ArgCnt = sizeof...(Args) };
};
template<typename Ret, typename Class, typename... Args> struct FInfo<Ret (Class::*) (Args...) const>
{
    typedef Ret RetT;
    typedef Class ClassT;
    typedef _t_list<Args...> ArgsT;
    typedef Ret (*FptrT) (Args...);
    typedef Ret (Class::*FuncT) (Args...) const;
    typedef std::function<Ret(Args...)> FunctionT;
    enum { IsFunction = true, IsMember = true, ArgCnt = sizeof...(Args) };
};

template<typename T> using _t_remove_rc = typename std::remove_const<typename std::remove_reference<T>::type>::type;

std::string VE_API _t_demangle(const char* type_name);
template<typename T> struct Meta
{
    typedef T TypeT;
    inline static const char* typeInfoName() { return typeid(T).name(); }
    inline static std::string typeName() { return _t_demangle(typeid(T).name()); }
};

template<typename Func1, typename Func2, typename Type>
using FIfSame = typename std::enable_if<std::is_same<typename FInfo<Func1>::FptrT, typename FInfo<Func2>::FptrT>::value, Type>::type;
template<typename Func1, typename Func2, typename Type>
using FIfConvertible = typename std::enable_if<std::is_convertible<Func1, Func2>::value
                                               && std::is_convertible<typename FInfo<Func1>::RetT, typename FInfo<Func2>::RetT>::value, Type>::type;

}

// pointer
template<typename T> using Pointer = std::shared_ptr<T>;

// container utils
#define PRIVATE_INHERIT_CONSTRUCTOR_IMPL(CONSTRUCTOR, CLASS, ...) \
public: \
using __VA_ARGS__::CONSTRUCTOR; \
using BaseT = __VA_ARGS__; \
CLASS() : BaseT() {} \
CLASS(const BaseT& other) noexcept : BaseT(other) {} \
CLASS(BaseT&& other) noexcept : BaseT(std::move(other)) {}

template<typename DerivedT, typename ValueT>
class PrivateTContainerBase
{
public:
    using ListLike = std::true_type;

protected:
    inline const DerivedT* dPtr() const { return static_cast<const DerivedT*>(this); }
    inline DerivedT* dPtr() { return static_cast<DerivedT*>(this); }

public:
    int sizeAsInt() const { return static_cast<int>(dPtr()->size()); }
    bool has(int index) const { return index >= 0 && index < sizeAsInt(); }
    ValueT value(int index) const { return has(index) ? dPtr()->operator[](index) : ValueT(); }
    const ValueT& value(int index, const ValueT& default_value) const { return has(index) ? dPtr()->operator[](index) : default_value; }

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
    PRIVATE_INHERIT_CONSTRUCTOR_IMPL(array, Array, std::array<T, N>)
};

template<typename T> class Vector : public std::vector<T>, public PrivateTContainerBase<Vector<T>, T>
{
    PRIVATE_INHERIT_CONSTRUCTOR_IMPL(vector, Vector, std::vector<T>)
};

template<typename T> class List : public std::list<T>, public PrivateTContainerBase<List<T>, T>
{
    PRIVATE_INHERIT_CONSTRUCTOR_IMPL(list, List, std::list<T>)

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

template<typename DerivedT, typename KeyT, typename ValueT>
class PrivateKVContainerBase
{
public:
    using MapLike = std::true_type;
    using DictLike = std::integral_constant<bool, std::is_base_of<std::string, KeyT>::value>;

protected:
    inline const DerivedT* dPtr() const { return static_cast<const DerivedT*>(this); }
    inline DerivedT* dPtr() { return static_cast<DerivedT*>(this); }
    inline void fromKVVectors(const Vector<KeyT>& keys, const Vector<ValueT>& values)
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
    const ValueT& value(const KeyT& key, const ValueT& default_value) const
    {
        const auto it = dPtr()->find(key);
        return it == dPtr()->end() ? default_value : it->second;
    }

    DerivedT& insertOne(const KeyT& key, const ValueT& value) { auto d = dPtr(); d->operator[](key) = value; return *d; }
    DerivedT& insertOne(const KeyT& key, ValueT&& value) { auto d = dPtr(); d->operator[](key) = std::move(value); return *d; }
};

template<typename K, typename V>
class Map : public std::map<K, V>, public PrivateKVContainerBase<Map<K, V>, K, V>
{
    PRIVATE_INHERIT_CONSTRUCTOR_IMPL(map, Map, std::map<K, V>)
    Map(const Vector<K>& keys, const Vector<V>& values) { this->fromKVVectors(keys, values); }
};

template<typename K, typename V>
class HashMap : public std::unordered_map<K, V>, public PrivateKVContainerBase<HashMap<K, V>, K, V>
{
    PRIVATE_INHERIT_CONSTRUCTOR_IMPL(unordered_map, HashMap, std::unordered_map<K, V>)
    HashMap(const Vector<K>& keys, const Vector<V>& values) { this->fromKVVectors(keys, values); }
};

template<typename V>
using Dict = HashMap<std::string, V>;

// ordered hash map — insertion-ordered, Robin Hood hashing
// Adapted from Godot Engine's HashMap (see ve/core/impl/ for license).
// Provides the same extended interface as HashMap/Map + insertion-order iteration.
// Iteration yields impl::KeyValue<K,V> with .key / .value members.
template<typename K, typename V,
         typename Hasher     = impl::HashMapHasherDefault,
         typename Comparator = impl::HashMapComparatorDefault<K>>
class OrderedHashMap
{
public:
    using MapLike  = std::true_type;
    using DictLike = std::integral_constant<bool, std::is_same<K, std::string>::value>;
    using ImplMap  = impl::InsertionOrderedHashMap<K, V, Hasher, Comparator>;

    // --- constructors ---
    OrderedHashMap() = default;
    explicit OrderedHashMap(uint32_t capacity) : _m(capacity) {}
    OrderedHashMap(std::initializer_list<impl::KeyValue<K, V>> init) : _m(init) {}
    OrderedHashMap(const OrderedHashMap& o) : _m(static_cast<const ImplMap&>(o._m)) {}
    OrderedHashMap(OrderedHashMap&&) = default;
    OrderedHashMap& operator=(const OrderedHashMap& o) { _m = o._m; return *this; }
    OrderedHashMap& operator=(OrderedHashMap&&) = default;

    // --- size / query ---
    uint32_t size() const { return _m.size(); }
    int sizeAsInt() const { return static_cast<int>(_m.size()); }
    bool is_empty() const { return _m.is_empty(); }
    bool has(const K& key) const { return _m.has(key); }

    // --- access ---
    V& operator[](const K& key) { return _m[key]; }
    const V& operator[](const K& key) const { return _m[key]; }
    V& get(const K& key) { return _m.get(key); }
    const V& get(const K& key) const { return _m.get(key); }
    V* getptr(const K& key) { return _m.getptr(key); }
    const V* getptr(const K& key) const { return _m.getptr(key); }

    // --- ve KV container interface (aligned with HashMap/Map) ---
    V value(const K& key) const
    {
        const V* ptr = _m.getptr(key);
        return ptr ? *ptr : V();
    }
    const V& value(const K& key, const V& default_value) const
    {
        const V* ptr = _m.getptr(key);
        return ptr ? *ptr : default_value;
    }

    Vector<K> keys() const
    {
        Vector<K> vec;
        vec.reserve(size());
        for (const auto& kv : _m) vec.push_back(kv.key);
        return vec;
    }
    Vector<V> values() const
    {
        Vector<V> vec;
        vec.reserve(size());
        for (const auto& kv : _m) vec.push_back(kv.value);
        return vec;
    }

    OrderedHashMap& insertOne(const K& key, const V& value) { _m[key] = value; return *this; }
    OrderedHashMap& insertOne(const K& key, V&& value) { _m[key] = std::move(value); return *this; }

    // --- mutation ---
    bool erase(const K& key) { return _m.erase(key); }
    void clear() { _m.clear(); }
    void reserve(uint32_t cap) { _m.reserve(cap); }

    // --- iterators (insertion-order) ---
    using Iterator      = typename ImplMap::Iterator;
    using ConstIterator = typename ImplMap::ConstIterator;

    Iterator begin() { return _m.begin(); }
    Iterator end()   { return _m.end(); }
    Iterator last()  { return _m.last(); }
    Iterator find(const K& key) { return _m.find(key); }

    ConstIterator begin() const { return _m.begin(); }
    ConstIterator end()   const { return _m.end(); }
    ConstIterator last()  const { return _m.last(); }
    ConstIterator find(const K& key) const { return _m.find(key); }

private:
    ImplMap _m;
};

template<typename V>
using OrderedDict = OrderedHashMap<std::string, V>;

using Ints = Vector<int>;
using Doubles = Vector<double>;
using Strings = Vector<std::string>;

class VE_API Values : public Doubles
{
    PRIVATE_INHERIT_CONSTRUCTOR_IMPL(Doubles, Values, Doubles)

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

/**
* @brief Object Class describes a minimum set of properties in order to keep controllable
*/
class VE_API Object
{
    VE_DECLARE_PRIVATE

public:
    using SignalT = int;
    using ActionT = std::function<void()>;

    explicit Object(const std::string& name = "");
    virtual ~Object();

public:
    Object* parent() const;
    void setParent(Object* obj);

    virtual std::string name() const;

    enum Signal : SignalT { OBJECT_DELETED = 0xffff };

    bool hasConnection(SignalT signal, Object* observer);
    void connect(SignalT signal, Object* observer, const ActionT& action);
    void disconnect(SignalT signal, Object* observer);
    void disconnect(Object* observer);

    void trigger(SignalT signal);

private:
    friend class Manager;
};

/**
* @brief Manager Class is a convenient object container
*/
class Manager : public Object, public HashMap<std::string, Object*>
{
public:
    explicit Manager(const std::string& name);
    virtual ~Manager();

    Object* add(Object* obj, bool delete_if_failed = false);
    template<typename SubObj> basic::enable_if_t<std::is_base_of<Object, SubObj>::value, SubObj*> add(SubObj* obj, bool delete_if_failed = false)
    { return add(static_cast<Object *>(obj), delete_if_failed) ? obj : nullptr; }

    bool remove(Object* obj, bool auto_delete = true);
    bool remove(const std::string& name, bool auto_delete = true);

    Object* get(const std::string& key) const;
    template<class SubObj> basic::enable_if_t<std::is_base_of<Object, SubObj>::value, SubObj*> get(const std::string& key) const
    { return static_cast<SubObj*>(get(key)); }

    void fixObjectLinks();
};

}
