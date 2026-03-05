#include <ve/rtt/cip.h>

namespace imol {

void CipRegistry::reg(const std::string& cmd_key, const FullCipFunc& cip)
{
    m_cips[cmd_key] = cip;
}

FullCipFunc CipRegistry::get(const std::string& cmd_key) const
{
    auto it = m_cips.find(cmd_key);
    return it != m_cips.end() ? it->second : nullptr;
}

bool CipRegistry::has(const std::string& cmd_key) const
{
    return m_cips.find(cmd_key) != m_cips.end();
}

Procedure CipRegistry::makeCipProc(const std::string& cmd_key, const SimpleJson& input) const
{
    auto cip = get(cmd_key);
    if (!cip) return Procedure();
    return Procedure([cip, input](CommandObject* cobj) -> Result {
        return cip(cobj, input);
    });
}

} // namespace imol
