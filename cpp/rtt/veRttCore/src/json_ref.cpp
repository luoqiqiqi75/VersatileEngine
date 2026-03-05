#include <ve/rtt/json_ref.h>

namespace imol {

// --- SimpleJson ---

static SimpleJson s_empty_json;

const SimpleJson& SimpleJson::arrayAt(size_t i) const
{
    return i < m_array.size() ? m_array[i] : s_empty_json;
}

bool SimpleJson::hasKey(const std::string& key) const
{
    return m_object.find(key) != m_object.end();
}

const SimpleJson& SimpleJson::objectAt(const std::string& key) const
{
    auto it = m_object.find(key);
    return it != m_object.end() ? it->second : s_empty_json;
}

std::vector<std::string> SimpleJson::objectKeys() const
{
    std::vector<std::string> keys;
    for (const auto& kv : m_object) keys.push_back(kv.first);
    return keys;
}

bool SimpleJson::empty() const
{
    switch (m_type) {
    case Null:   return true;
    case Array:  return m_array.empty();
    case Object: return m_object.empty();
    case String: return m_string.empty();
    default:     return false;
    }
}

// --- JsonRef ---

JsonRef JsonRef::at(int index) const
{
    return JsonRef(m_ptr->arrayAt(index));
}

JsonRef JsonRef::at(const std::string& key) const
{
    if (!key.empty() && key[0] == '#') {
        int idx = std::stoi(key.substr(1));
        return at(idx);
    }
    return JsonRef(m_ptr->objectAt(key));
}

JsonRef JsonRef::get(const std::string& path) const
{
    JsonRef cur = *this;
    std::istringstream ss(path);
    std::string segment;
    while (std::getline(ss, segment, '.')) {
        if (segment.empty()) continue;
        cur = cur.at(segment);
        if (!cur) return cur;
    }
    return cur;
}

bool JsonRef::toBool(bool def, bool* ok) const
{
    if (ok) *ok = (m_ptr && !m_ptr->isNull());
    return m_ptr ? m_ptr->asBool(def) : def;
}

int JsonRef::toInt(int def, bool* ok) const
{
    if (ok) *ok = (m_ptr && !m_ptr->isNull());
    return m_ptr ? (int)m_ptr->asInt(def) : def;
}

double JsonRef::toDouble(double def, bool* ok) const
{
    if (ok) *ok = (m_ptr && !m_ptr->isNull());
    return m_ptr ? m_ptr->asDouble(def) : def;
}

std::string JsonRef::toString(const std::string& def, bool* ok) const
{
    if (ok) *ok = (m_ptr && !m_ptr->isNull());
    return m_ptr ? m_ptr->asString(def) : def;
}

Values JsonRef::toValues() const
{
    Values vals;
    if (m_ptr && m_ptr->isArray()) {
        for (size_t i = 0; i < m_ptr->arraySize(); ++i)
            vals.push_back(m_ptr->arrayAt(i).asDouble());
    }
    return vals;
}

std::vector<std::string> JsonRef::objectKeys() const
{
    return m_ptr ? m_ptr->objectKeys() : std::vector<std::string>{};
}

} // namespace imol
