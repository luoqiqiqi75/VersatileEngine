#pragma once

#include <ve/rtt/object.h>
#include <ve/rtt/meta.h>

namespace imol {

class JsonRef;

template<typename T>
class TemplateInterface {
public:
    typedef T Type;
    typedef Meta<T> MetaT;

    T& ref() { return m_data; }
    const T& get() const { return m_data; }
    void set(const T& t) { m_data = t; }

    std::string dataTypeName() const { return MetaT::typeName(); }

protected:
    T m_data{};
};

class DataObject : public Object {
public:
    enum Signal : int { CHANGED = 0x0200 };

    explicit DataObject(const std::string& name = "")
        : Object(name) {}

    virtual ~DataObject() = default;

    std::mutex& mutex() { return m_mutex; }
    virtual std::string dataTypeName() const { return "unknown"; }

private:
    mutable std::mutex m_mutex;
};

template<typename T>
class TDataObject : public DataObject, public TemplateInterface<T> {
public:
    explicit TDataObject(const std::string& name = "")
        : DataObject(name) {}

    std::string dataTypeName() const override { return TemplateInterface<T>::dataTypeName(); }

    void update(const T& t) {
        this->set(t);
        trigger(CHANGED);
    }

    void updateIfDifferent(const T& t) {
        if (!basic::equals(this->get(), t)) update(t);
    }

    T clone() {
        std::lock_guard<std::mutex> guard(mutex());
        return this->get();
    }

    void replace(const T& t) {
        std::lock_guard<std::mutex> guard(mutex());
        update(t);
    }
};

class JsonInterface {
public:
    virtual ~JsonInterface() = default;
    virtual std::string serializeToJsonString() const { return "{}"; }
    virtual bool deserializeFromJsonString(const std::string&) { return false; }
};

template<typename T>
class BasicDataObject : public TDataObject<T>, public JsonInterface {
public:
    explicit BasicDataObject(const std::string& name = "")
        : TDataObject<T>(name) {}
};

} // namespace imol
