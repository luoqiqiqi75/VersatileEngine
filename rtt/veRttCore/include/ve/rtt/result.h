#pragma once

#include <ve/rtt/json_ref.h>

#include <string>

namespace imol {

class Result {
public:
    enum ResultCode : int {
        SUCCESS = 0,
        FAIL    = -1,
        ACCEPT  = 1
    };

    Result() : m_code(SUCCESS) {}
    Result(int code) : m_code(code) {}
    Result(int code, const std::string& text) : m_code(code), m_text(text) {}
    Result(bool flag, int error_code = FAIL) : m_code(flag ? SUCCESS : error_code) {}

    bool isSuccess() const { return m_code == SUCCESS; }
    bool isError() const { return m_code < 0; }
    bool isAccepted() const { return m_code == ACCEPT; }

    int code() const { return m_code; }
    const std::string& text() const { return m_text; }

    Result& setCode(int code) { m_code = code; return *this; }
    Result& setText(const std::string& text) { m_text = text; return *this; }

    const Json& content() const { return m_content; }
    Result& setContent(const Json& content) { m_content = content; return *this; }
    Result& setContent(const std::string& json_str) {
        m_content = Json::parse(json_str, nullptr, false);
        return *this;
    }

    std::string toString() const;
    Json toJson() const;

    operator bool() const { return isSuccess(); }

private:
    int m_code;
    std::string m_text;
    Json m_content;   // nlohmann::json — defaults to null
};

} // namespace imol
