#include "ve/core/plugin.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <filesystem>

namespace ve::plugin::impl {

void* open(const std::string& path, std::string& error)
{
    auto native = std::filesystem::u8path(path);
    HMODULE handle = LoadLibraryW(native.c_str());
    DWORD code = handle ? 0 : GetLastError();
    if (!handle && !native.has_parent_path()) {
        std::vector<wchar_t> executable(32768);
        DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        if (length && length < executable.size()) {
            auto full = std::filesystem::path(std::wstring(executable.data(), length)).parent_path() / native;
            handle = LoadLibraryW(full.c_str());
            code = handle ? 0 : GetLastError();
        }
    }
    if (!handle) error = "LoadLibrary failed: " + path + " (error " + std::to_string(code) + ")";
    return handle;
}

bool close(void* handle, std::string& error)
{
    if (FreeLibrary(static_cast<HMODULE>(handle))) return true;
    error = "FreeLibrary failed (error " + std::to_string(GetLastError()) + ")";
    return false;
}

} // namespace ve::plugin::impl
