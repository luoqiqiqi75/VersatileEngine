#pragma once

#include "ve_rtt_global.h"

#include <cmath>

namespace imol {

template<typename DerivedT, typename ValueT>
class TContainerBase {
protected:
    const DerivedT* dPtr() const { return static_cast<const DerivedT*>(this); }
    DerivedT* dPtr() { return static_cast<DerivedT*>(this); }

public:
    int sizeAsInt() const { return static_cast<int>(dPtr()->size()); }
    bool has(int index) const { return index >= 0 && index < sizeAsInt(); }
    ValueT value(int index) const { return has(index) ? dPtr()->operator[](index) : ValueT(); }
    const ValueT& value(int index, const ValueT& default_value) const {
        return has(index) ? dPtr()->operator[](index) : default_value;
    }

    DerivedT& prepend(const ValueT& v) { auto d = dPtr(); d->insert(d->begin(), v); return *d; }
    DerivedT& append(const ValueT& v) { auto d = dPtr(); d->push_back(v); return *d; }

    template<typename F>
    DerivedT& every(F&& f) { auto d = dPtr(); std::for_each(d->begin(), d->end(), std::forward<F>(f)); return *d; }
    template<typename F>
    const DerivedT& every(F&& f) const { const auto d = dPtr(); std::for_each(d->cbegin(), d->cend(), std::forward<F>(f)); return *d; }

    std::string toString(const std::string& sep = ",") const {
        int s = sizeAsInt() - 1;
        if (s < 0) return "";
        std::ostringstream ss;
        int i = 0;
        every([&](const ValueT& v) { (i++ < s) ? (ss << v << sep) : (ss << v); });
        return ss.str();
    }
};

#define IMOL_INHERIT_CTOR(CLASS, BASE) \
public: \
    using BASE::BASE; \
    CLASS() : BASE() {} \
    CLASS(const BASE& other) : BASE(other) {} \
    CLASS(BASE&& other) noexcept : BASE(std::move(other)) {}

template<typename T, std::size_t N>
class Array : public std::array<T, N>, public TContainerBase<Array<T, N>, T> {
public:
    Array() : std::array<T, N>{} {}
};

template<typename T>
class Vector : public std::vector<T>, public TContainerBase<Vector<T>, T> {
public:
    using std::vector<T>::vector;
    Vector() : std::vector<T>() {}
    Vector(const std::vector<T>& other) : std::vector<T>(other) {}
    Vector(std::vector<T>&& other) noexcept : std::vector<T>(std::move(other)) {}
};

template<typename T>
class List : public std::list<T>, public TContainerBase<List<T>, T> {
public:
    using std::list<T>::list;
    List() : std::list<T>() {}
    List(const std::list<T>& other) : std::list<T>(other) {}
    List(std::list<T>&& other) noexcept : std::list<T>(std::move(other)) {}

    const T& operator[](int i) const {
        int cnt = 0;
        for (const auto& it : *this) if (cnt++ == i) return it;
        throw std::out_of_range("<imol> list out of range");
    }
    T& operator[](int i) {
        int cnt = 0;
        for (auto& it : *this) if (cnt++ == i) return it;
        throw std::out_of_range("<imol> list out of range");
    }
};

template<typename DerivedT, typename KeyT, typename ValueT>
class KVContainerBase {
protected:
    const DerivedT* dPtr() const { return static_cast<const DerivedT*>(this); }
    DerivedT* dPtr() { return static_cast<DerivedT*>(this); }

public:
    int sizeAsInt() const { return static_cast<int>(dPtr()->size()); }
    bool has(const KeyT& key) const { return dPtr()->find(key) != dPtr()->end(); }

    Vector<KeyT> keys() const {
        Vector<KeyT> vec;
        vec.reserve(dPtr()->size());
        for (const auto& kv : *dPtr()) vec.push_back(kv.first);
        return vec;
    }
    Vector<ValueT> values() const {
        Vector<ValueT> vec;
        vec.reserve(dPtr()->size());
        for (const auto& kv : *dPtr()) vec.push_back(kv.second);
        return vec;
    }

    ValueT value(const KeyT& key) const {
        auto it = dPtr()->find(key);
        return it == dPtr()->end() ? ValueT() : it->second;
    }
    const ValueT& value(const KeyT& key, const ValueT& default_value) const {
        auto it = dPtr()->find(key);
        return it == dPtr()->end() ? default_value : it->second;
    }

    DerivedT& insertOne(const KeyT& key, const ValueT& v) { (*dPtr())[key] = v; return *dPtr(); }
    DerivedT& insertOne(const KeyT& key, ValueT&& v) { (*dPtr())[key] = std::move(v); return *dPtr(); }
};

template<typename K, typename V>
class Map : public std::map<K, V>, public KVContainerBase<Map<K, V>, K, V> {
public:
    using std::map<K, V>::map;
    Map() : std::map<K, V>() {}
    Map(const std::map<K, V>& other) : std::map<K, V>(other) {}
    Map(const Vector<K>& keys, const Vector<V>& vals) {
        for (size_t i = 0; i < std::min(keys.size(), vals.size()); ++i)
            (*this)[keys[i]] = vals[i];
    }
};

template<typename K, typename V>
class HashMap : public std::unordered_map<K, V>, public KVContainerBase<HashMap<K, V>, K, V> {
public:
    using std::unordered_map<K, V>::unordered_map;
    HashMap() : std::unordered_map<K, V>() {}
    HashMap(const std::unordered_map<K, V>& other) : std::unordered_map<K, V>(other) {}
    HashMap(const Vector<K>& keys, const Vector<V>& vals) {
        for (size_t i = 0; i < std::min(keys.size(), vals.size()); ++i)
            (*this)[keys[i]] = vals[i];
    }
};

template<typename V> using Dict = HashMap<std::string, V>;

using Ints = Vector<int>;
using Doubles = Vector<double>;
using Strings = Vector<std::string>;

// Values — numeric array with unit conversion
class Values : public Doubles {
public:
    using Doubles::Doubles;
    Values() : Doubles() {}
    Values(const Doubles& other) : Doubles(other) {}
    Values(Doubles&& other) noexcept : Doubles(std::move(other)) {}

    enum Unit : int {
        NONE    = 0x0000,
        M       = 0x0100,
        MM      = 0x0101,
        DEGREE  = 0x0200,
        RAD     = 0x0201,
        SAME    = 0xffff
    };

    Unit unit() const;
    Values& setUnit(Unit u);

    Values& add(double d);
    Values& multiply(double d, Unit new_unit = SAME);

    Values& m2mm();
    Values& mm2m();
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

} // namespace imol
