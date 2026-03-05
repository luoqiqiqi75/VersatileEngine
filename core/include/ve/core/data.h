// ----------------------------------------------------------------------------
// data.h — Reactive data layer: AnyData<T>, DataList, DataDict, DataManager
// ----------------------------------------------------------------------------
// Copyright (c) 2023-present Thilo and VersatileEngine contributors.
// Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
// See LICENSE file in the project root for full license information.
// ----------------------------------------------------------------------------

#pragma once

#include "base.h"
#include "log.h"

#include "yaml-cpp/yaml.h"

namespace ve {

namespace flags {

inline bool get(int flags, int f) { return (flags & f) == f; }
inline int set(int& flags, int f, bool on_off) { return flags = on_off ? flags | f : flags & ~f; }

}

enum DataSignal : int {
    DATA_ABOUT_TO_CHANGE    = 0xff10,
    DATA_CHANGED            = 0xff1f
};

class VE_API AbstractData
{
    VE_DECLARE_PRIVATE

public:
    enum DataFlags : int {
        VALID       = 0x01,
        CHANGEABLE  = 0x02
    };

public:
    AbstractData();
    virtual ~AbstractData();

    Object* listener();

    std::uint8_t control() const;
    std::uint8_t control(short c);

    template<DataFlags F> bool flag() const { return control() | F; }
    template<DataFlags F> void setFlag(bool b) { control(b ? control() | F : control() & ~F); }

    virtual std::string dataType() const = 0;

    virtual bool fromString(const std::string&) = 0;
    virtual std::string toString() const = 0;

    virtual bool fromYaml(const YAML::Node&) = 0;
    virtual YAML::Node toYaml() const = 0;
};

using AbstractDataPointer = Pointer<AbstractData>;

///

template<typename T>
class AnyData;

template<typename T, class = void>
struct DataTypeHelper
{ using DataT = AnyData<basic::_t_remove_rc<T>>; };
template<typename T>
struct DataTypeHelper<T, std::enable_if_t<std::is_convertible_v<T, std::string>>>
{ using DataT = AnyData<std::string>; };

template<typename T>
using TypedData = typename DataTypeHelper<T>::DataT;

template<typename T, class = void> struct DataSerializeHelper
{ enum { IsValid = false }; using IsCommonT = bool; };
template<typename T> struct DataSerializeHelper<T, std::enable_if_t<std::is_same_v<T, AbstractDataPointer> || std::is_convertible_v<T, AbstractData*>>>
{ enum { IsValid = true }; using IsDataPointerT = bool; };
template<typename T> struct DataSerializeHelper<T, std::enable_if_t<std::is_convertible_v<T, std::string>>>
{ enum { IsValid = true }; using IsStringT = bool; using IsCommonT = bool; };
template<typename T> struct DataSerializeHelper<T, std::enable_if_t<T::ListLike::value>>
{ enum { IsValid = true }; using IsListT = bool; using IsContainerT = bool; };
template<typename T> struct DataSerializeHelper<T, std::enable_if_t<T::DictLike::value>>
{ enum { IsValid = true }; using IsDictT = bool; using IsContainerT = bool; };

namespace serialize {

// --- string serialization ---

template<typename T> inline std::enable_if_t<!DataSerializeHelper<T>::IsValid && !basic::is_inputable<T>::value, bool> fromString(const std::string&, T&)
{ return false; }
template<typename T> inline std::enable_if_t<!DataSerializeHelper<T>::IsValid && basic::is_inputable<T>::value, bool> fromString(const std::string& from, T& value)
{ std::istringstream iss(from); iss >> value; return true; }

template<typename T> inline std::enable_if_t<!DataSerializeHelper<T>::IsValid && !basic::is_outputable<T>::value, bool> toString(const T&, std::string&)
{ return false; }
template<typename T> inline std::enable_if_t<!DataSerializeHelper<T>::IsValid && basic::is_outputable<T>::value, bool> toString(const T& value, std::string& to)
{ std::ostringstream oss; oss << value; to = oss.str(); return true; }

template<typename T> inline typename DataSerializeHelper<T>::IsStringT fromString(const std::string& from, T& value) { value = from; return true; }
template<typename T> inline typename DataSerializeHelper<T>::IsStringT toString(const T& value, std::string& to) { to = value; return true; }

template<typename T> inline typename DataSerializeHelper<T>::IsDataPointerT fromString(const std::string& from, T& ptr) { return ptr->fromString(from); }
template<typename T> inline typename DataSerializeHelper<T>::IsDataPointerT toString(const T& ptr, std::string& to) { to = ptr->toString(); return true; }

template<typename T> inline typename DataSerializeHelper<T>::IsListT fromString(const std::string& from, T& value)
{
    std::size_t s = from.find('[');
    std::size_t cnt = 0;
    for (typename T::reference it : value) {
        std::size_t n = from.find(++cnt == value.size() ? ']' : ',', s + 1);
        if (s + 1 > n || s == std::string::npos || n == std::string::npos) return false;
        if (!fromString(from.substr(s + 1, n - s - 1), it)) {}
        s = n + 1;
    }
    return true;
}
template<typename T> inline typename DataSerializeHelper<T>::IsListT toString(const T& value, std::string& to)
{
    std::ostringstream oss;
    oss << "[";
    std::size_t cnt = 0;
    std::string s;
    for (const auto& it : value) {
        if (!toString(it, s)) {}
        oss << s;
        if (++cnt < value.size()) oss << ", ";
    }
    oss << "]";
    to = oss.str();
    return true;
}

template<typename T> inline typename DataSerializeHelper<T>::IsDictT fromString(const std::string& from, T& value)
{ return false; }
template<typename T> inline typename DataSerializeHelper<T>::IsDictT toString(const T& value, std::string& to)
{
    using Access = typename T::KVAccessT;
    std::ostringstream oss;
    oss << "{";
    std::size_t cnt = 0;
    std::string s;
    for (const auto& kv : value) {
        if (!toString(Access::value(kv), s)) {}
        oss << Access::key(kv) << ": " << s;
        if (++cnt < value.size()) oss << ", ";
    }
    oss << "}";
    to = oss.str();
    return true;
}

// --- yaml serialization ---

VE_DECLARE_T_FUNC_CHECKER(is_yaml_decodable, bool, decltype(YAML::convert<T>::decode(YAML::Node(), basic::_t_static_var_helper<T>::var)));
VE_DECLARE_T_FUNC_CHECKER(is_yaml_encodable, YAML::Node, decltype(YAML::convert<T>::encode(basic::_t_static_var_helper<T>::var)));

template<typename T> inline std::enable_if_t<!is_yaml_decodable<T>::value, typename DataSerializeHelper<T>::IsCommonT> fromYaml(const YAML::Node& from, T& value)
{ return false; }
template<typename T> inline std::enable_if_t<!is_yaml_encodable<T>::value, typename DataSerializeHelper<T>::IsCommonT> toYaml(const T& value, YAML::Node& to)
{ return false; }

template<typename T> inline std::enable_if_t<is_yaml_decodable<T>::value, typename DataSerializeHelper<T>::IsCommonT> fromYaml(const YAML::Node& from, T& value)
{
    try { return YAML::convert<T>::decode(from, value); }
    catch (const std::exception&) { return false; }
}
template<typename T> inline std::enable_if_t<is_yaml_encodable<T>::value, typename DataSerializeHelper<T>::IsCommonT> toYaml(const T& value, YAML::Node& to)
{
    try { to = YAML::convert<T>::encode(value); }
    catch (const std::exception&) { return false; }
    return true;
}

template<typename T> inline typename DataSerializeHelper<T>::IsDataPointerT fromYaml(const YAML::Node& from, T& ptr) { return ptr->fromYaml(from); }
template<typename T> inline typename DataSerializeHelper<T>::IsDataPointerT toYaml(const T& ptr, YAML::Node& to) { to = ptr->toYaml(); return true; }

template<typename T> inline typename DataSerializeHelper<T>::IsListT fromYaml(const YAML::Node& from, T& value)
{
    if (!from.IsSequence() || value.size() != from.size()) return false;
    std::size_t cnt = 0;
    for (typename T::reference it : value) { fromYaml(from[cnt], it); cnt++; }
    return true;
}
template<typename T> inline typename DataSerializeHelper<T>::IsListT toYaml(const T& value, YAML::Node& to)
{
    std::size_t cnt = 0;
    for (const auto& it : value) { auto n = to[cnt]; toYaml(it, n); cnt++; }
    return true;
}

template<typename T> inline typename DataSerializeHelper<T>::IsDictT fromYaml(const YAML::Node& from, T& value)
{
    using Access = typename T::KVAccessT;
    for (auto& kv : value) {
        try { fromYaml(from[Access::key(kv)], Access::value(kv)); }
        catch (const std::exception&) { continue; }
    }
    return true;
}
template<typename T> inline typename DataSerializeHelper<T>::IsDictT toYaml(const T& value, YAML::Node& to)
{
    using Access = typename T::KVAccessT;
    for (const auto& kv : value) {
        if (Access::key(kv).size() > 0 && Access::key(kv)[0] == '_') continue;
        auto n = to[Access::key(kv)];
        toYaml(Access::value(kv), n);
    }
    return true;
}

} // namespace serialize

///

#define VE_DATA_UPDATE(...) \
    listener()->trigger(DATA_ABOUT_TO_CHANGE); \
    if (!flag<CHANGEABLE>()) return false; \
    __VA_ARGS__; \
    listener()->trigger(DATA_CHANGED); \
    return true

template<typename T>
class AnyData : public AbstractData
{
    using V = basic::_t_remove_rc<T>;

public:
    AnyData(const V& v) : v_(v) {}
    AnyData(V&& v) : v_(std::move(v)) {}

    V& ref() { return v_; }
    V* ptr() { return &v_; }

    const V& get() const { return v_; }
    void set(const V& v) { v_ = v; }
    void set(V&& v) { v_ = std::move(v); }

    template<typename VRef>
    bool update(VRef&& v)
    { VE_DATA_UPDATE(set(std::forward<VRef>(v))); }
    bool updateIfDifferent(const V& v)
    { return basic::equals(this->get(), v) ? false : update(v); }

    void bind(Object* context, V* v) { *v = get(); listener()->connect(DATA_CHANGED, context, [=] { *v = get(); }); }
    void bind(V* v) { bind(listener(), v); }

    std::string dataType() const override { return basic::Meta<V>::typeName(); }

    bool fromString(const std::string& s) override
    { VE_DATA_UPDATE(if (!serialize::fromString(s, ref())) return false); }
    std::string toString() const override
    { std::string s; return serialize::toString(this->get(), s) ? s : "@invalid"; }

    bool fromYaml(const YAML::Node& n) override
    { VE_DATA_UPDATE(if (!serialize::fromYaml(n, ref())) return false); }
    YAML::Node toYaml() const override
    { YAML::Node n; return serialize::toYaml(this->get(), n) ? n : YAML::Node(); }

protected:
    V v_;
};

template<>
class AnyData<void> : public AbstractData
{
public:
    bool update()
    { VE_DATA_UPDATE(); }

    std::string dataType() const override { return basic::Meta<void>::typeName(); }

    bool fromString(const std::string& s) override
    { return (s.empty() || s == "void") ? update() : false; }
    std::string toString() const override
    { return "void"; }

    bool fromYaml(const YAML::Node& n) override
    { return n.IsNull(); }
    YAML::Node toYaml() const override
    { return YAML::Node(); }
};

///

class DataList;
class DataDict;

template<typename T, std::size_t N = 0xff, bool RO = false>
class DataCache : public Object
{
public:
    using CacheT = Array<T, N>;
    using MapT = Dict<std::size_t>;

public:
    DataCache(const std::string& name) : Object("ve::ndc_" + name), cache_(new CacheT) {}
    DataCache(const std::string& name, CacheT* cache) : Object("ve::dc_" + name), cache_(cache) {}
    ~DataCache() { if (name().at(4) == 'n') delete cache_; }

    std::size_t mapIndex(const std::string& key, std::size_t index) { return index_mapping_[key] = index; }

    const CacheT& cache() const { return *cache_; }
    std::enable_if_t<!RO, CacheT&> cache() { return *cache_; }

    const T& operator[] (const std::string& key) const { return cache_->operator[](index_mapping_.value(key)); }
    std::enable_if_t<!RO, T&> operator[] (const std::string& key) { return cache_->operator[](index_mapping_.value(key)); }

private:
    CacheT* cache_;
    MapT index_mapping_;
};

template<typename DerivedT, typename KeyT, template<typename> class Ptr = std::shared_ptr>
struct PrivateDataContainerBase
{
    template<typename T> using ValueT = TypedData<T>;
    template<typename T> inline static Ptr<ValueT<T>> newDataPointer() { return Ptr<ValueT<T>>(new ValueT<T>()); }
    template<typename T> inline static Ptr<ValueT<T>> newDataPointer(T&& t) { return Ptr<ValueT<T>>(new ValueT<T>(std::forward<T>(t))); }

    const DerivedT* dPtr() const { return static_cast<const DerivedT*>(this); }
    DerivedT* dPtr() { return static_cast<DerivedT*>(this); }

    AbstractData* rawDataAt(const KeyT& k) const { return dPtr()->value(k).get(); }
    template<typename T> ValueT<T>* dataAt(const KeyT& k) const { return dynamic_cast<ValueT<T>*>(rawDataAt(k)); }
    template<typename T> void getAt(const KeyT& k, T& t) const { t = dataAt<T>(k)->get(); }
    template<typename T> const T& getAt(const KeyT& k) const { return dataAt<T>(k)->get(); }
    template<typename T> void setAt(const KeyT& k, T&& t) const { return dataAt<T>(k)->set(std::forward<T>(t)); }
    template<typename T> void replaceAt(const KeyT& k, T&& t) { dPtr()->operator[](k) = newDataPointer(std::forward<T>(t)); }
    template<typename T> ValueT<T>* insertAt(const KeyT& k) { auto p = newDataPointer<T>(); dPtr()->insertOne(k, p); return p.get(); }
    template<typename T> ValueT<T>* insertAt(const KeyT& k, T&& t) { auto p = newDataPointer<T>(std::forward<T>(t)); dPtr()->insertOne(k, p); return p.get(); }
};

class VE_API DataList : public Vector<AbstractDataPointer>, public PrivateDataContainerBase<DataList, int>
{
public:
    using Vector<AbstractDataPointer>::Vector;

    template<typename T> void appendRaw(T&& t) { append(newDataPointer(std::forward<T>(t))); }
    template<typename T, typename... Ts> void appendRaw(T&& t, Ts&&... ts) { appendRaw(std::forward<T>(t)); appendRaw(std::forward<Ts>(ts)...); }
    template<typename T> void appendEmpty() { append(newDataPointer<T>()); }
    template<typename T, typename... Ts> std::enable_if_t<(sizeof...(Ts) > 0)> appendEmpty() { appendEmpty<T>(); appendEmpty<Ts...>(); }

    template<typename... Ts> static DataList fromRaw(Ts&&... values)
    { DataList l; l.reserve(sizeof...(Ts)); l.appendRaw(std::forward<Ts>(values)...); return l; }
    template<typename... Ts> static DataList fromRaw()
    { DataList l; l.reserve(sizeof...(Ts)); l.appendEmpty<Ts...>(); return l; }

    DataList* insertEmptyList(int index);
    DataDict* insertEmptyDict(int index);
    DataList* appendEmptyList();
    DataDict* appendEmptyDict();
};

class VE_API DataDict : public Dict<AbstractDataPointer>, public PrivateDataContainerBase<DataDict, std::string>
{
public:
    using Dict<AbstractDataPointer>::Dict;

    template<typename T> DataDict& insertRaw(const std::string& key, T&& t) { this->operator [](key) = newDataPointer(std::forward<T>(t)); return *this; }
    template<typename... Ts> static DataDict fromRaw(const Strings& keys, Ts... values) { return DataDict(keys, DataList::fromRaw(values...)); }

    DataList* insertEmptyList(const std::string& key);
    DataDict* insertEmptyDict(const std::string& key);
};

///

class VE_API DataManager
{
    VE_DECLARE_PRIVATE

public:
    DataManager();
    ~DataManager();

    Strings keys() const;
    Strings keys(const std::string& prefix) const;

    AbstractData* insert(const std::string& key, AbstractData* d, bool auto_replace = false);
    AbstractData* remove(const std::string& key, bool auto_delete = true);

    AbstractData* find(const std::string& key) const;
    AbstractData* find(const DataList* data_list, const std::string& key) const;
    AbstractData* find(const DataDict* data_dict, const std::string& key) const;
    AbstractData* find(AbstractData* root, const std::string& key) const;
    AbstractData* find(const std::string& key, std::string& rest) const;

    template<typename DataT>
    auto insertNew(const std::string& key, DataT* new_data, bool* ok = nullptr, bool auto_delete = true)
    {
        if (auto d = find(key)) {
            if (ok) *ok = false;
            if (auto_delete) delete new_data;
            return static_cast<DataT*>(d);
        }
        insert(key, new_data);
        if (ok) *ok = true;
        return new_data;
    }
};

VE_API DataManager& globalDataManager();

template<typename T, typename... Args> inline TypedData<T>* d(Args&&... args)
{
    if (auto ptr = dynamic_cast<TypedData<T>*>(globalDataManager().find(std::forward<Args>(args)...))) return ptr;
    ve::log::ws("<ve::data>", "data <", basic::Meta<T>::typeName(), "> not found with arg:", args...);
    return nullptr;
}
template<typename... Args> inline DataDict* dict(Args... args) { if (auto dd = d<DataDict>(std::forward<Args>(args)...)) return dd->ptr(); return nullptr; }
template<typename... Args> inline DataList* list(Args... args) { if (auto dd = d<DataList>(std::forward<Args>(args)...)) return dd->ptr(); return nullptr; }

namespace data {

template<typename T, typename HelperT = DataTypeHelper<T>>
inline typename HelperT::DataT from(T&& value) { return typename HelperT::DataT(std::forward<T>(value)); }

template<typename T, typename HelperT = DataTypeHelper<T>>
inline typename HelperT::DataT* create(const std::string& key, T&& value, bool* ok = nullptr)
{ return globalDataManager().insertNew(key, new typename HelperT::DataT(std::forward<T>(value)), ok); }

VE_API TypedData<void>* createVoid(const std::string& key, bool* ok = nullptr);
VE_API TypedData<void>* createTrigger(const std::string& key, const Object::ActionT& action, bool trigger_now = false);
VE_API TypedData<void>* createTrigger(const std::string& key, Object* observer, const Object::ActionT& action, bool trigger_now = false);

#define VE_D_FUNC_IMPL(Type, ...) auto ptr = d<Type>(key); if (!ptr) return false; __VA_ARGS__; return true

template<typename T> inline bool get(const std::string& key, T& value) { VE_D_FUNC_IMPL(T, value = ptr->get()); }
template<typename T> inline T get(const std::string& key) { return d<T>(key)->get(); }
template<typename T> inline bool set(const std::string& key, const T& value) { VE_D_FUNC_IMPL(T, ptr->set(value)); }
template<typename T> inline bool update(const std::string& key, const T& value) { VE_D_FUNC_IMPL(T, ptr->update(value)); }
inline bool trigger(const std::string& key) { VE_D_FUNC_IMPL(void, ptr->update()); }

} // namespace data

}
