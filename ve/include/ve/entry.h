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
//   int code = ve::entry::run();
//   ve::entry::deinit();
//
// Configuration JSON (ve.json) maps directly to the node tree:
//
//   {
//     "ve": {
//       "entry": { "verbose": false, "plugins": [...] },
//       "core":  { "config": { "log": { "level": "info" } } },
//       "service": { "http": { "config": { "port": 8080 } } }
//     },
//     "browser": { "config": { ... } }
//   }
//
// The entire JSON is loaded into node::root(). Each module reads its own
// subtree (key with '.' replaced by '/'). No staging area or dispatch needed.
//
// Module loading policy:
//   - Registered modules (VE_REGISTER_MODULE) load by default.
//     Config only overrides priority/depends, or disables via "enabled": false.
//   - Plugins (dynamic libraries) do NOT load by default.
//     Only plugins listed under ve/entry/plugins are loaded.
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
    bool verbose      = false;
    // Enable the local stdio terminal via ve/client/terminal/stdio/enabled.
    bool terminal     = false;
    // Enable the remote terminal client via ve/client/terminal/tcp/enabled.
    bool remote_terminal = false;
    std::string remote_host = "127.0.0.1";
    int remote_port = 10000;
    int  pool_threads = 4;
    int    argc = 0;
    char** argv = nullptr;
};

// Load configuration into node::root().
VE_API void setup(const std::string& config_file);
VE_API void setup(const Options& options);
VE_API void setup(Node* config_node);

// Load plugins, create modules, and complete initialization.
VE_API void init();

// Enter the main loop and block until quit is requested.
VE_API int  run();

// Deinitialize modules in reverse order and release runtime state.
VE_API void deinit();

// Request run() to return via loop::quit()
VE_API void requestQuit(int exit_code = 0);

// Convenience: setup + init + run + deinit
VE_API int  exec(const std::string& config_file);
VE_API int  exec(int argc, char** argv);

// State query
VE_API State state();

// Access the parsed Options (argc/argv available for Qt etc.)
VE_API const Options& options();

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
