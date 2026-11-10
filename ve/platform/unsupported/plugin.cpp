#include "ve/core/plugin.h"

namespace ve::plugin::impl {

void* open(const std::string&, std::string& error)
{
    error = "dynamic plugins are unsupported on this platform";
    return nullptr;
}

bool close(void*, std::string& error)
{
    error = "dynamic plugins are unsupported on this platform";
    return false;
}

} // namespace ve::plugin::impl
