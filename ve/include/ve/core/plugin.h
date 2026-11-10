#pragma once

#include "node.h"

namespace ve::plugin {

struct Info {
    std::string path;
    std::string name;
    int api_version = 0;
    void* handle = nullptr;
};

// Accept one plugin spec or an anonymous list, as in ve.json's plugins.
// Defaults: enabled=true, min_api=0, name=filename stem without leading lib.
VE_API bool load(Node* specs, Node* out = nullptr, bool verbose = false, std::string* error = nullptr);
// Identify a loaded library by name, or derive its name from path.
VE_API bool unload(Node* specs, Node* out = nullptr, bool verbose = false, std::string* error = nullptr);
VE_API const Vector<Info>& loaded();

namespace impl {
// Implemented by the selected ve/platform backend.
void* open(const std::string& path, std::string& error);
bool close(void* handle, std::string& error);
}

} // namespace ve::plugin
