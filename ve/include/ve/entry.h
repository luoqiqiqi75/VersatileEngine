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
//   setup()  loads ONE startup config file into /ve/entry, then overlays the
//            caller's options node on top so CLI always wins. Nothing outside
//            /ve/entry is written.
//   init()   loads plugins, filters factory keys by the blacklist, materializes
//            selected module nodes in hierarchical DFS order (siblings sorted
//            by base priority), copies each module's subtree from
//            /ve/entry/modules, instantiates, and drives the INIT / PREPARE /
//            READY state machine. /ve/entry is erased once READY is reached,
//            so downstream code sees only real module state.
//
// Startup config file (default "ve.json" in cwd; override with -c / --config).
// A missing file is not an error — every setting has a built-in default. The
// file name carries no meaning: leo.json and ve.json behave identically.
//
//   {
//     "app":       "leo",                          // log app name
//     "log":       { "level": "info", "dir": "" }, // level: d|i|w|e
//     "version":   2,                              // minimum VE version required
//     "blacklist": [ "ve.service.x" ],             // module keys to skip
//     "plugins":   [ { "path": "veqt.dll", "enabled": true, "min_api": 0 } ],
//
//     "modules": {                                 // copied onto the node tree
//       "ve":  { "server": { "node": { "http": { "config": { "port": 12000 } } } } },
//       "leo": { "robot":  { "controller": { "backend": "external" } } }
//     }
//   }
//
// Reserved top-level keys are app / log / version / blacklist / plugins /
// modules. Everything a module reads lives under "modules", keyed by node path
// — /ve/entry/modules/ve/server is copied to /ve/server. Module keys use '.'
// ("ve.server"), node paths use '/' ("ve/server"); they name the same thing.
// Subtrees under "modules" that match no loaded module are dropped with the
// staging area.
//
// /ve/entry after setup():
//
//   app        string   Log app name.
//   log        node     level / dir. Applied during setup() so the entry
//                       pipeline's own logs honor it.
//   version    int      Minimum VE version this config requires. setup() fails
//                       if it exceeds VE_ENTRY_VERSION.
//   blacklist  list     Module keys to skip. An entry "a.b" also skips every
//                       descendant "a.b.*". Order-independent.
//   plugins    list     [{ path, enabled?, min_api?, name? }, ...], loaded in
//                       order at the top of init(). File entries come first;
//                       CLI plugin paths (bare .dll/.so/.dylib) append.
//   modules    node     Per-module config, keyed by node path.
//   argv       list     Every argv token, verbatim — including the ones VE did
//                       not consume. Apps with their own flags parse this (or
//                       entry::args()) instead of pre-registering with VE.
//   verbose    bool     Entry-pipeline chatter (config load, module creation,
//                       state transitions). Also via entry::verbose() so
//                       modules can gate their own first-run logs on it.
//
// CLI flags, recognized only by setup(int, char**). Each one writes straight
// into the options node, so the node tree is the single representation:
//
//   -c <path> / --config <path>   startup config file
//   <path>.json                   same, as a bare positional (first wins)
//   <path>.dll/.so/.dylib         appended to plugins
//   -v / --verbose                verbose = true
//   -t / --terminal               modules/ve/client/terminal/stdio/enabled
//   -r [host:port] / --remote     modules/ve/client/terminal/tcp/*
//
// -t and -r are mutually exclusive. Unrecognized flags are ignored, not
// rejected: they stay in /ve/entry/argv for the application to parse, so
// adding a flag to VE can never break a downstream launcher.
//
// There is no generic "override any path from the command line" flag. Settings
// belong in the config file, where they are reviewable and diffable; an app
// that needs its own switches parses /ve/entry/argv (or entry::args()) and
// writes the nodes it owns before calling init().
//
// Precedence (later wins): built-in defaults - config file - caller options
// (CLI). The whole /ve/entry staging area is erased at the end of init(), so
// consumers must read what they need before READY, or use the query helpers
// (entry::verbose(), entry::args()).
//
// Module load policy:
//   - Registered modules (VE_REGISTER_MODULE) load by default; put a key in
//     blacklist to skip it (also skips its subtree).
//   - ve.service.* modules are opt-in: only loaded if a matching subtree
//     exists under "modules".
//   - Plugins (dynamic libraries) do NOT load by default. Only plugins listed
//     under /ve/entry/plugins (via config file or CLI) are loaded.
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

// Highest config-file "version" this build understands. setup() fails when a
// config asks for more, so an old binary refuses a config it cannot honor
// instead of silently ignoring the parts it does not know.
constexpr int VE_ENTRY_VERSION = 2;

enum State : int {
    NONE,
    SETUP,
    INIT,
    READY,
    RUNNING,
    SHUTDOWN
};

// Parse argv into a fresh options node and hand off to setup(Node*).
VE_API bool setup(int argc, char** argv);

// Shortcut: run with defaults but override the config file path.
VE_API bool setup(const std::string& config_file);

// Merge a caller-supplied options node into /ve/entry, load the config file it
// points at, and finish preparing the staging area for init(). Pass nullptr to
// run with defaults (loads "ve.json" from cwd if present). Returns false when
// the config requires a newer VE than this build.
VE_API bool setup(Node* options_n);

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
// need to see the process argv, and for apps parsing their own flags. argc()
// reflects the original count. /ve/entry/argv holds the same tokens until
// init() erases the staging area.
VE_API std::pair<int, char**> args();

// Convenience: /ve/entry/verbose (or false pre-setup).
VE_API bool   verbose();

// Application name derived from argv[0]; used for log prefix.
VE_API const std::string& appName();

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

VE_API bool load(const std::string& path, bool verbose = false);
VE_API bool unload(const std::string& name, bool verbose = false);
VE_API const Vector<Info>& loaded();

} // namespace plugin

} // namespace ve

VE_API std::ostream& operator<<(std::ostream& os, ve::entry::State s);
