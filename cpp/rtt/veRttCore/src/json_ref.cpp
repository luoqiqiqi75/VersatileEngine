#include <ve/rtt/json_ref.h>

#include <sstream>

namespace imol {

// Shared null sentinel — returned for any missing element.
const Json JsonRef::s_null = nullptr;

// ----- element access ----------------------------------------------------

JsonRef JsonRef::at(int index) const
{
    if (m_ptr && m_ptr->is_array()
        && index >= 0
        && static_cast<size_t>(index) < m_ptr->size())
    {
        return JsonRef((*m_ptr)[static_cast<size_t>(index)]);
    }
    return JsonRef(s_null);
}

JsonRef JsonRef::at(const std::string& key) const
{
    // "#N" → array index
    if (!key.empty() && key[0] == '#') {
        int idx = std::stoi(key.substr(1));
        return at(idx);
    }
    if (m_ptr && m_ptr->is_object() && m_ptr->contains(key)) {
        return JsonRef((*m_ptr)[key]);
    }
    return JsonRef(s_null);
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

// ----- typed accessors ---------------------------------------------------

bool JsonRef::toBool(bool def, bool* ok) const
{
    if (m_ptr && m_ptr->is_boolean()) {
        if (ok) *ok = true;
        return m_ptr->get<bool>();
    }
    if (ok) *ok = false;
    return def;
}

int JsonRef::toInt(int def, bool* ok) const
{
    if (m_ptr && m_ptr->is_number()) {
        if (ok) *ok = true;
        return m_ptr->get<int>();
    }
    if (ok) *ok = false;
    return def;
}

int64_t JsonRef::toInt64(int64_t def, bool* ok) const
{
    if (m_ptr && m_ptr->is_number()) {
        if (ok) *ok = true;
        return m_ptr->get<int64_t>();
    }
    if (ok) *ok = false;
    return def;
}

double JsonRef::toDouble(double def, bool* ok) const
{
    if (m_ptr && m_ptr->is_number()) {
        if (ok) *ok = true;
        return m_ptr->get<double>();
    }
    if (ok) *ok = false;
    return def;
}

std::string JsonRef::toString(const std::string& def, bool* ok) const
{
    if (m_ptr && m_ptr->is_string()) {
        if (ok) *ok = true;
        return m_ptr->get<std::string>();
    }
    if (ok) *ok = false;
    return def;
}

Values JsonRef::toValues() const
{
    Values vals;
    if (m_ptr && m_ptr->is_array()) {
        vals.reserve(m_ptr->size());
        for (const auto& elem : *m_ptr) {
            vals.push_back(elem.is_number() ? elem.get<double>() : 0.0);
        }
    }
    return vals;
}

std::vector<std::string> JsonRef::objectKeys() const
{
    std::vector<std::string> keys;
    if (m_ptr && m_ptr->is_object()) {
        keys.reserve(m_ptr->size());
        for (auto it = m_ptr->begin(); it != m_ptr->end(); ++it) {
            keys.push_back(it.key());
        }
    }
    return keys;
}

} // namespace imol
