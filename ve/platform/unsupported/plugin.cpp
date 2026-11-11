#include "../../src/core/plugin.h"

namespace ve::plugin::impl {

const char* suffix() { return ""; }

void* open(const std::string&, std::string& error)
{
    error = "dynamic plugins are unsupported on this platform";
    return nullptr;
}

} // namespace ve::plugin::impl
