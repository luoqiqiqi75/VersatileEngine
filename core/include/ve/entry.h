// ----------------------------------------------------------------------------
// entry.h - VersatileEngine initialization / module loading / shutdown
// ----------------------------------------------------------------------------
// Copyright (c) 2023-present Thilo and VersatileEngine contributors.
// Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
// See LICENSE file in the project root for full license information.
// ----------------------------------------------------------------------------
//
// Pure C++ entry point (no Qt / ROS dependency).
//
// Typical usage:
//
//   #include <ve/entry.h>
//   int main(int argc, char** argv) {
//       return ve::entry::exec(argc, argv);
//   }
//
// Or step-by-step:
//
//   ve::entry::setup("ve.json");
//   ve::entry::init();
//   // ... application logic ...
//   ve::entry::deinit();
//
// Configuration is loaded into the Node tree at "ve/entry/".
// Each module works under ve/entry/module/{key} (accessed via Module::node()).
//
// Node layout:
//   ve/
//   ve/state                          - current entry::State
//   ve/entry/                         - setup-loaded config
//   ve/entry/module/{key}/            - per-module workspace (config, state, ...)
//   ve/entry/plugins/                 - dynamic plugin config
//
// Module loading policy:
//   - Registered modules (VE_REGISTER_MODULE) load by default.
//     Config only overrides priority/depends, or disables via "enabled": false.
//   - Plugins (dynamic libraries) do NOT load by default.
//     Only plugins listed in config with a path are loaded.
//
// ----------------------------------------------------------------------------

#pragma once

#include "ve/core/module.h"

namespace ve {

class Node;

// ============================================================================
// ve::entry - application lifecycle
// ============================================================================

namespace entry {

enum State : int {
    NONE,
    SETUP,
    INIT,
    READY,
    RUNNING,
    SHUTDOWN
};

struct Options {
    std::string config_file;
    bool verbose        = false;
    int  pool_threads   = 4;
};

// Phase 1 - Load configuration into ve/entry/ node tree
VE_API void setup(const std::string& config_file);
VE_API void setup(const Options& options);
VE_API void setup(Node* config_node);

// Phase 2 - Load plugins → create modules → INIT → READY
VE_API void init();

// Phase 3 - Block on condition variable until requestQuit()
VE_API int  run();

// Phase 4 - DEINIT modules (reverse order) → cleanup
VE_API void deinit();

// Request run() to return
VE_API void requestQuit(int exit_code = 0);

// Convenience: setup + init + run + deinit
VE_API int  exec(const std::string& config_file);
VE_API int  exec(int argc, char** argv);

// State query
VE_API State state();

// Access the ve/ root node
VE_API Node* config();

} // namespace entry

// ============================================================================
// ve::plugin - cross-platform dynamic library loading
// ============================================================================

namespace plugin {

struct Info {
    std::string path;
    std::string name;
    int         api_version = 0;
    void*       handle      = nullptr;
};

VE_API bool load(const std::string& path);
VE_API bool unload(const std::string& name);
VE_API const Vector<Info>& loaded();

} // namespace plugin

} // namespace ve

VE_API std::ostream& operator<<(std::ostream& os, ve::entry::State s);
