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
//   ve::entry::setup(argc, argv);
//   ve::entry::init();
//   int code = ve::entry::run();
//   ve::entry::deinit();
//
// Lifecycle:
//   setup()  parses argv into a temporary options node and applies it. The
//            config file (default "ve.json") is loaded into
//            /ve/entry/config/<stem>, its "options" subtree is merged into
//            /ve/entry/options, and finally the CLI options overlay it so
//            command-line always wins. Nothing outside /ve/entry is written.
//   init()   loads plugins listed in options, filters factory keys by
//            options/modules/blacklist, materializes selected module nodes in
//            hierarchical DFS order (siblings sorted by base priority), copies
//            each module's config subtree in, instantiates, and drives the
//            INIT / PREPARE / READY state machine. /ve/entry is erased once
//            READY is reached, so downstream code sees only real module state.
//
// /ve/entry/options schema:
//
//   verbose         bool     Entry-pipeline chatter (config-load, module
//                            creation, state transitions). Also surfaced via
//                            entry::verbose() so modules can gate their own
//                            first-run logs on the same switch.
//   config_file     string   Path handed to setup(). Read by setup(Node*)
//                            before it touches the filesystem; a value of
//                            "" skips file loading entirely.
//   plugins         list     [{ path, enabled?, min_api?, name? }, ...].
//                            Loaded in order at the top of init(). Config
//                            entries come first; CLI plugin paths (bare
//                            .dll/.so/.dylib on argv) append.
//   modules.blacklist list   Module keys to skip. An entry "a.b" also skips
//                            every descendant "a.b.*". Order-independent.
//   config_override list     Internal — each entry { path, value } is
//                            flushed into /ve/entry/config during setup()
//                            and then erased. This is where CLI --set,
//                            --terminal, and --remote land; any caller
//                            constructing an options_n manually can push
//                            entries here too instead of pre-baking them
//                            into a full config subtree.
//
// Precedence (later wins): built-in defaults ◃ config file "options" subtree
// ◃ caller-supplied options_n. The whole /ve/entry staging area is erased
// at the end of init(), so consumers must read anything they need before
// READY, or use the query helpers (entry::verbose(), entry::args()).
//
// Config file layout (foo.json is mounted at /ve/entry/config/foo):
//
//   {
//     "options": {
//       "plugins":  [ { "path": "...", "enabled": true } ],
//       "modules":  { "blacklist": ["a.b.c"] },
//       "verbose":  true
//     },
//     "core":  { ... module foo.core config ... },
//     "sub":   { "mod": { ... module foo.sub.mod config ... } }
//   }
//
// A module named "foo.sub.mod" reads its own subtree at /foo/sub/mod ('.' in
// key -> '/' in path). Config subtrees that don't correspond to a loaded
// module are dropped with the staging area.
//
// Module load policy:
//   - Registered modules (VE_REGISTER_MODULE) load by default; put a key in
//     options/modules/blacklist to skip it (also skips its subtree).
//   - ve.service.* modules are opt-in: only loaded if their config subtree
//     exists.
//   - Plugins (dynamic libraries) do NOT load by default. Only plugins listed
//     under /ve/entry/options/plugins (via config or CLI) are loaded.
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

// Parse argv into a fresh options node and hand off to setup(Node*).
VE_API void setup(int argc, char** argv);

// Shortcut: run with defaults but override the config file path.
VE_API void setup(const std::string& config_file);

// Merge a caller-supplied options node into the staging area, load the config
// file it points at, and finish preparing /ve/entry for init(). Pass nullptr
// to run with defaults (loads "ve.json" from cwd if present).
VE_API void setup(Node* options_n);

// Load plugins, create modules, and complete initialization.
VE_API void init();

// Enter the main loop and block until quit is requested.
VE_API int  run();

// Deinitialize modules in reverse order and release runtime state.
VE_API void deinit();

// Request run() to return. Delegates to loop::main()->quit().
VE_API void requestQuit(int exit_code = 0);

// Convenience: setup + init + run + deinit
VE_API int  exec(int argc, char** argv);

// --- queries ---
VE_API State state();

// Original argv as passed to setup() — kept for frameworks (Qt, ROS) that
// need to see the process argv. argc() reflects the original count.
VE_API std::pair<int, char**> args();

// Application name derived from argv[0]; used for log prefix.
VE_API const std::string& appName();

// Convenience: /ve/entry/options/verbose (or false pre-setup).
VE_API bool   verbose();

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
