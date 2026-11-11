#include "../../src/core/plugin.h"

#include <dlfcn.h>

namespace ve::plugin::impl {

const char* suffix()
{
#ifdef __APPLE__
    return ".dylib";
#else
    return ".so";
#endif
}

void* open(const std::string& path, std::string& error)
{
    void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        const char* detail = dlerror();
        error = "dlopen failed: " + path + (detail ? " (" + std::string(detail) + ")" : "");
    }
    return handle;
}

} // namespace ve::plugin::impl
