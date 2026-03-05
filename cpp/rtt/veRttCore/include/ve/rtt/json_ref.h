#pragma once

#include <ve/rtt/container.h>

#include <string>
#include <memory>
#include <sstream>

namespace imol {

class SimpleJson {
public:
    enum Type { Null, Bool, Int, Double, String, Array, Object };

    SimpleJson() : m_type(Null) {}
    SimpleJson(bool v) : m_type(Bool), m_bool(v) {}
    SimpleJson(int v) : m_type(Int), m_int(v) {}
    SimpleJson(int64_t v) : m_type(Int), m_int(v) {}
    SimpleJson(double v) : m_type(Double), m_double(v) {}
    SimpleJson(const std::string& v) : m_type(String), m_string(v) {}
    SimpleJson(const char* v) : m_type(String), m_string(v) {}

    Type type() const { return m_type; }
    bool isNull() const { return m_type == Null; }
    bool isObject() const { return m_type == Object; }
    bool isArray() const { return m_type == Array; }

    void setArray() { m_type = Array; }
    void push(const SimpleJson& v) { m_array.push_back(v); }
    size_t arraySize() const { return m_array.size(); }
    const SimpleJson& arrayAt(size_t i) const;

    void setObject() { m_type = Object; }
    void set(const std::string& key, const SimpleJson& v) { m_type = Object; m_object[key] = v; }
    bool hasKey(const std::string& key) const;
    const SimpleJson& objectAt(const std::string& key) const;
    std::vector<std::string> objectKeys() const;

    bool asBool(bool def = false) const { return m_type == Bool ? m_bool : def; }
    int64_t asInt(int64_t def = 0) const { return m_type == Int ? m_int : (m_type == Double ? (int64_t)m_double : def); }
    double asDouble(double def = 0.0) const { return m_type == Double ? m_double : (m_type == Int ? (double)m_int : def); }
    std::string asString(const std::string& def = "") const { return m_type == String ? m_string : def; }

    bool empty() const;

private:
    Type m_type;
    bool m_bool = false;
    int64_t m_int = 0;
    double m_double = 0.0;
    std::string m_string;
    std::vector<SimpleJson> m_array;
    std::map<std::string, SimpleJson> m_object;
};

class JsonRef {
public:
    explicit JsonRef(const SimpleJson& value)
        : m_ptr(&value), m_holder(nullptr) {}

    explicit JsonRef(SimpleJson&& value)
        : m_holder(std::make_shared<SimpleJson>(std::move(value)))
        , m_ptr(m_holder.get()) {}

    explicit operator bool() const { return m_ptr && !m_ptr->isNull(); }

    const SimpleJson& value() const { return *m_ptr; }

    JsonRef at(int index) const;
    JsonRef at(const std::string& key) const;
    JsonRef get(const std::string& path) const;

    JsonRef operator[](int index) const { return at(index); }
    JsonRef operator[](const std::string& key) const { return at(key); }

    bool toBool(bool def = false, bool* ok = nullptr) const;
    int toInt(int def = 0, bool* ok = nullptr) const;
    double toDouble(double def = 0.0, bool* ok = nullptr) const;
    std::string toString(const std::string& def = "", bool* ok = nullptr) const;
    Values toValues() const;

    double operator()(int index) const { return at(index).toDouble(); }
    double operator()(const std::string& key) const { return get(key).toDouble(); }

    std::vector<std::string> objectKeys() const;

private:
    const SimpleJson* m_ptr;
    std::shared_ptr<SimpleJson> m_holder;
};

} // namespace imol
