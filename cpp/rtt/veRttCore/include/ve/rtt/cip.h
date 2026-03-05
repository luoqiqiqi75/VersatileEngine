#pragma once

#include <ve/rtt/command_object.h>
#include <ve/rtt/json_ref.h>

#include <string>
#include <functional>

namespace imol {

using FullCipFunc = std::function<Result(CommandObject*, const SimpleJson&)>;

template<typename T>
using SimpleCipFunc = std::function<T(const SimpleJson&)>;

class CipRegistry {
public:
    using CipMap = HashMap<std::string, FullCipFunc>;

    void reg(const std::string& cmd_key, const FullCipFunc& cip);

    template<typename T>
    void reg(const std::string& cmd_key, const SimpleCipFunc<T>& converter) {
        m_cips[cmd_key] = [converter](CommandObject* cobj, const SimpleJson& input) -> Result {
            T value = converter(input);
            cobj->setInputData(value);
            return Result::SUCCESS;
        };
    }

    template<typename T>
    void reg(const std::string& cmd_key) {
        m_cips[cmd_key] = [](CommandObject* cobj, const SimpleJson& input) -> Result {
            cobj->setInputData(input);
            return Result::SUCCESS;
        };
    }

    FullCipFunc get(const std::string& cmd_key) const;
    bool has(const std::string& cmd_key) const;
    Procedure makeCipProc(const std::string& cmd_key, const SimpleJson& input) const;

private:
    CipMap m_cips;
};

} // namespace imol
