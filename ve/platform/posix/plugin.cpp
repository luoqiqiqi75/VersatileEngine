#include "ve/core/plugin.h"

#include <dlfcn.h>

namespace ve::plugin::impl {

void* open(const std::string& path, std::string& error)
{
    void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        const char* detail = dlerror();
        error = "dlopen failed: " + path + (detail ? " (" + std::string(detail) + ")" : "");
    }
    return handle;
}

bool close(void* handle, std::string& error)
{
    if (dlclose(handle) == 0) return true;
    const char* detail = dlerror();
    error = detail ? detail : "dlclose failed";
    return false;
}

} // namespace ve::plugin::impl
