#pragma once
// ---------------------------------------------------------------------------
// JsonRef — immutable path-navigating wrapper over nlohmann::json
//
// Replaces the original SimpleJson + JsonRef pair.
// The underlying storage is now nlohmann::json; JsonRef adds:
//   - dot-separated path navigation  (get("a.b.c"))
//   - safe typed accessors with ok-flag (toInt, toDouble, ...)
//   - toValues() for numeric arrays
// ---------------------------------------------------------------------------

#include <ve/rtt/container.h>
#include <nlohmann/json.hpp>

#include <string>
#include <memory>

namespace imol {

/// Convenience alias used throughout the rtt module.
using Json = nlohmann::json;

// =========================================================================
// JsonRef
// =========================================================================
class JsonRef {
public:
    /// Wrap an existing (externally-owned) Json value.
    explicit JsonRef(const Json& value)
        : m_ptr(&value), m_holder(nullptr) {}

    /// Take ownership of an rvalue Json.
    explicit JsonRef(Json&& value)
        : m_holder(std::make_shared<Json>(std::move(value)))
        , m_ptr(m_holder.get()) {}

    /// True if the wrapped value is not null.
    explicit operator bool() const { return m_ptr && !m_ptr->is_null(); }

    /// Access the underlying Json.
    const Json& value() const { return *m_ptr; }

    // ----- element access ------------------------------------------------
    JsonRef at(int index) const;
    JsonRef at(const std::string& key) const;

    /// Dot-separated path navigation: get("a.b.c")
    /// Also supports "#N" segments for array indexing: get("arr.#0.x")
    JsonRef get(const std::string& path) const;

    JsonRef operator[](int index) const { return at(index); }
    JsonRef operator[](const std::string& key) const { return at(key); }

    // ----- typed accessors -----------------------------------------------
    bool        toBool  (bool def = false,              bool* ok = nullptr) const;
    int         toInt   (int def = 0,                   bool* ok = nullptr) const;
    int64_t     toInt64 (int64_t def = 0,               bool* ok = nullptr) const;
    double      toDouble(double def = 0.0,              bool* ok = nullptr) const;
    std::string toString(const std::string& def = "",   bool* ok = nullptr) const;

    /// Convert a JSON array of numbers to an imol::Values vector.
    Values toValues() const;

    /// Shorthand: jr(0) → double, jr("key") → double
    double operator()(int index) const { return at(index).toDouble(); }
    double operator()(const std::string& key) const { return get(key).toDouble(); }

    /// Return all keys of the underlying JSON object (empty if not object).
    std::vector<std::string> objectKeys() const;

private:
    const Json* m_ptr;
    std::shared_ptr<Json> m_holder;

    static const Json s_null;  ///< shared sentinel for missing values
};

} // namespace imol
