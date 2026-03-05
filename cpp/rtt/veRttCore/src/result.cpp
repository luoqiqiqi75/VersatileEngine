#include <ve/rtt/result.h>

namespace imol {

std::string Result::toString() const
{
    if (isSuccess()) return "success";
    if (isAccepted()) return "accepted";
    std::string s = "error(" + std::to_string(m_code) + ")";
    if (!m_text.empty()) s += ": " + m_text;
    return s;
}

Json Result::toJson() const
{
    if (m_content.is_null()) {
        // No attached data — return a simple string representation.
        return Json(toString());
    }
    Json value = Json::object();
    if (m_code < 0) {
        value["error"] = toString();
    }
    value["data"] = m_content;
    return value;
}

} // namespace imol
