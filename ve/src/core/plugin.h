#pragma once

#include "ve/core/node.h"

// Internal: dynamic plugin loading for entry's plugins config and the plugin
// command. Libraries stay loaded until process exit.
namespace ve::plugin {

// One spec or an anonymous list, as in ve.json's plugins: { name?, path?, enabled? }.
// name is the library base name ("veqt"): without path, lib<name><suffix> then
// <name><suffix> are opened by file name, so the platform search applies
// (exe dir/PATH on Windows, rpath/LD_LIBRARY_PATH/DYLD_LIBRARY_PATH elsewhere).
// name defaults to path's stem without lib.
bool load(Node* specs, Node* out = nullptr, bool verbose = false, std::string* error = nullptr);

namespace impl {
// Implemented by the selected ve/platform backend.
const char* suffix();
void* open(const std::string& path, std::string& error);
}

} // namespace ve::plugin
