#pragma once

#include <ve/rtt/command_object.h>
#include <ve/rtt/json_ref.h>

#include <string>
#include <functional>

namespace imol {

/// Full CIP function: receives the CommandObject and the raw JSON input.
using FullCipFunc = std::function<Result(CommandObject*, const Json&)>;

/// Simple CIP function: converts JSON input to a typed value.
template<typename T>
using SimpleCipFunc = std::function<T(const Json&)>;

class CipRegistry {
public:
    using CipMap = HashMap<std::string, FullCipFunc>;

    void reg(const std::string& cmd_key, const FullCipFunc& cip);

    template<typename T>
    void reg(const std::string& cmd_key, const SimpleCipFunc<T>& converter) {
        m_cips[cmd_key] = [converter](CommandObject* cobj, const Json& input) -> Result {
            T value = converter(input);
            cobj->setInputData(value);
            return Result::SUCCESS;
        };
    }

    /// Default CIP: just store the raw Json as input data.
    template<typename T>
    void reg(const std::string& cmd_key) {
        m_cips[cmd_key] = [](CommandObject* cobj, const Json& input) -> Result {
            cobj->setInputData(input);
            return Result::SUCCESS;
        };
    }

    FullCipFunc get(const std::string& cmd_key) const;
    bool has(const std::string& cmd_key) const;
    Procedure makeCipProc(const std::string& cmd_key, const Json& input) const;

private:
    CipMap m_cips;
};

} // namespace imol
