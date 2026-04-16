#pragma once

#include <ve/rtt/data_object.h>

namespace imol {

namespace data {

inline Manager& mgr() {
    static Manager s_mgr("_imol_data_mgr");
    return s_mgr;
}

inline DataObject* get(const std::string& name) {
    return mgr().get<DataObject>(name);
}

template<typename T>
TemplateInterface<T>* ti(const std::string& name) {
    return dynamic_cast<TemplateInterface<T>*>(get(name));
}

inline JsonInterface* ji(const std::string& name) {
    return dynamic_cast<JsonInterface*>(get(name));
}

template<typename T>
TDataObject<T>* create(const std::string& name) {
    if (auto existing = get(name)) return dynamic_cast<TDataObject<T>*>(existing);
    auto* dobj = new TDataObject<T>(name);
    mgr().add(dobj);
    return dobj;
}

} // namespace data

inline DataObject* d(const std::string& key) {
    return data::get(key);
}

} // namespace imol

#define IMOL_DEC_G_DATA(_KEY, _TYPE) \
    class _TYPE##DataObject : public imol::BasicDataObject<_TYPE> { \
    public: \
        explicit _TYPE##DataObject() : imol::BasicDataObject<_TYPE>(#_KEY) {} \
    }; \
    namespace imol_g_##_TYPE { \
        typedef _TYPE##DataObject ClassT; \
        typedef _TYPE DataT; \
        inline std::string key() { return #_KEY; } \
        inline ClassT* dobj() { \
            if (auto* cur = dynamic_cast<ClassT*>(imol::d(key()))) return cur; \
            auto* ptr = new ClassT(); \
            return imol::data::mgr().add(ptr) ? ptr : nullptr; \
        } \
        inline DataT& ref() { return dobj()->ref(); } \
        inline DataT clone() { \
            std::lock_guard<std::mutex> guard(dobj()->mutex()); \
            return ref(); \
        } \
        inline void replace(const DataT& t) { \
            std::lock_guard<std::mutex> guard(dobj()->mutex()); \
            ref() = t; \
        } \
    }
