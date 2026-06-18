// res.h - embedded resource registry
//
// Files compiled into the library via the ve_embed_files() CMake helper register
// themselves here at static-init time and are read back by logical path:
//
//   ve::res::read("ve/service/op.json")  -> string_view over the embedded bytes
//
// Content is baked into the binary (no external file to lose); the returned view
// stays valid for the program lifetime.
#pragma once

#include "ve/global.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace ve {
namespace res {

// Register an embedded blob under a logical path. Called by generated code.
// `data` must have static storage duration (the view returned by read() aliases it).
VE_API void registerFile(std::string path, const unsigned char* data, std::size_t len);

// Embedded content for `path`, or an empty view if not present.
VE_API std::string_view read(const std::string& path);

VE_API bool has(const std::string& path);

} // namespace res
} // namespace ve
