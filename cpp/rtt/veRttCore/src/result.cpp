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

SimpleJson Result::toJson() const
{
    if (m_content.isNull()) {
        return SimpleJson(toString());
    }
    SimpleJson value;
    value.setObject();
    if (m_code < 0) {
        value.set("error", SimpleJson(toString()));
    }
    value.set("data", m_content);
    return value;
}

} // namespace imol
