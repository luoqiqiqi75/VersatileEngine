// res.cpp - embedded resource registry (see res.h)

#include "ve/core/res.h"

#include <unordered_map>

namespace ve {
namespace res {

namespace {

struct Blob {
    const unsigned char* data = nullptr;
    std::size_t          len  = 0;
};

// Function-local static: constructed on first registerFile(), so it is immune to
// static-init order across the many generated translation units.
std::unordered_map<std::string, Blob>& registry()
{
    static std::unordered_map<std::string, Blob> r;
    return r;
}

} // namespace

void registerFile(std::string path, const unsigned char* data, std::size_t len)
{
    registry()[std::move(path)] = Blob{data, len};
}

std::string_view read(const std::string& path)
{
    auto it = registry().find(path);
    if (it == registry().end()) return {};
    return std::string_view(reinterpret_cast<const char*>(it->second.data), it->second.len);
}

bool has(const std::string& path)
{
    return registry().find(path) != registry().end();
}

} // namespace res
} // namespace ve
