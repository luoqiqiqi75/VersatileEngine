# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/).

---

## [2.0.0] - 2026-03

### Pure C++17 Core Rewrite

The core layer (`libve`) has been completely rewritten as **pure C++17 with zero Qt dependency**. All existing Qt-based functionality is preserved in the adapter layer (`cpp/qt/`).

### Added

#### ve::Var - 16-byte Variant
- 10 value types + CUSTOM: NONE, BOOL, INT, INT64, DOUBLE, STRING, BIN, LIST, DICT, POINTER
- `Convert<T>` extension point for custom type serialization (toString/fromString/toBin/fromBin)
- Inline storage for small types (bool/int/double/pointer), heap pointer for large types
- Full comparison, copy, move semantics

#### ve::Node - Reactive Data Tree
- Vector + Hash hybrid storage with Pool allocation
- Signal system: NODE_CHANGED, NODE_ACTIVATED, NODE_ADDED, NODE_REMOVED
- Subtree watching with signal bubbling (activated propagation)
- Same-name children with `#N` indexing
- Shadow (prototype chain) and Schema support
- Global accessors: `ve::n("/slash/path")`, `ve::d("dot.path")`
- Performance: child(index) 590x, iterator 135x, indexOf 42x faster than imol::ModuleObject

#### Command System
- `ve::Step` / `ve::Pipeline` / `ve::Command` three-level abstraction
- 20+ built-in commands: ls, info, get, set, add, rm, mv, mk, find, erase, json, help, child, shadow, watch, iter, schema, cmd
- POSIX-style flag parsing (-x, --long, -abc)

#### Service Layer (pure C++, asio2-based)
- Terminal REPL server (TCP port 5061) with tab completion
- HTTP server (port 8080) with REST-like Node access
- WebSocket server (port 8081) with real-time Node change push
- TCP Binary server (port 5065) with CBS protocol for high-efficiency IPC
- SubscribeService for Node change subscription

#### Serialization
- JSON: simdjson parse, stringify, exportTree, importTree
- Binary: CBS-compatible exportTree/importTree, writeVar/readVar
- Schema: field-list based structured export/import

#### Entry & Lifecycle
- `ve::Entry` - config-driven lifecycle (NONE -> SETUP -> INIT -> READY -> RUNNING -> SHUTDOWN)
- `ve::Module` - module lifecycle with topological sort and dependency management
- `ve::plugin` - dynamic library loading (load/unload/loaded)
- `ve::Loop` - Asio event loop (EventLoop + main/pool + LoopRef)

#### Infrastructure
- `ve::Pool<T>` / `Pooled<T>` / `PoolPtr<T>` - object pool with CRTP
- `ve::OrderedHashMap` - Robin Hood hashing with insertion order (Godot-derived)
- `ve::SmallVector<T,N>` - inline buffer with heap overflow
- `ve::Factory<Sig>` - generic factory pattern with caching
- Custom test framework (`ve_test.h`) with 535 unit tests
- Bundled dependencies: spdlog, asio2, fmt, cereal, yaml-cpp, pugixml, nlohmann/json

#### Adapter Modules
- `cpp/rtt/` - Pure C++ RTT adapter (veRttCore, XService)
- `cpp/ros/` - DDS adapter with FastDDS bridge (veFastDDS)

### Changed

- Core layer no longer depends on Qt - pure C++17 with STL only
- CMake modernized: `VE_BUILD_TEST`, `VE_BUILD_QT`, `VE_BUILD_DDS`, `VE_BUILD_RTT` options
- Dependencies reorganized into `deps/` with proper CMake targets
- `ve::Object` signal system rewritten with integer-based signals and flexible slot signatures
- Logging migrated to spdlog (pure C++, no Qt dependency)

---

## [1.0.0] - 2025

### First Independent Release of VersatileEngine

VE was extracted from the internal project (imol) and released as an open-source project, consolidating a decade of core capabilities developed across 8 commercial projects.

### Included

#### Core Layer
- `ve::Data` (`imol::ModuleObject`) — full reactive data tree node
  - QVariant value storage, Qt signal-driven (changed / activated / added / removed)
  - Tree navigation API: `p()` / `c()` / `b()` / `r()` / `fullName()`
  - Serialization: JSON / XML / Binary / QVariant
  - Quiet mode (signal suppression) and subtree watching
- `ve::d("path")` global path accessor with auto-creation
- `VE_D("path")` static-cached accessor for high-frequency access
- `ve::Module` — module lifecycle management (NONE → INIT → READY → DEINIT)
- `VE_REGISTER_MODULE` macro — module registration and factory creation
- `ve::Object` / `ve::Manager` — pure C++ base object class and container
- `ve::Factory<Sig>` — generic factory pattern
- `ve::log` — spdlog-based logging system
- `ve::terminal` — built-in runtime debugger
- Crash recovery: Windows (SEH + StackWalk64) / Linux (signal + backtrace)

#### Service Layer
- CBS (Compact Binary Service) — C++↔C++ TCP binary protocol
  - Supports echo / publish / subscribe / unsubscribe
  - Supports single and recursive data structure operations
- CommandServer — TCP text command service
- XService Client — JSON over TCP client
  - g/s/c/w operations (get / set / command / watch)
  - Synchronous/asynchronous dual mode
  - Two-reply mechanism (sync acknowledgment + async completion notification)

#### Build System
- CMake 3.15+ build system
- Shared / static library build support
- Cross-platform: Windows / Linux / macOS
- Bundled third-party dependencies: asio2, rapidjson

### Historical Origins

VE's core technology originates from the following projects:

- **RobotAssist** (2015) — established the "everything is a node" ModuleObject architecture
- **xcore** (2018) — pure C++ implementation, template TDataObject\<T\>, complete command system
- **CyberOne** (2020) — ROS ecosystem integration, AnyData\<T\> + bind() + FastDDS
- **feelinghand** (2021) — first standalone VE usage, ImGui + Qt + VE integration
- **Bezier** (2022) — medical-grade pipeline pattern, spdlog integration
- **PDS-HMI** (2023) — complete HMI development paradigm, declarative data tree
- **MozHMI** (2024) — web frontend breakthrough, WebSocket protocol design
- **MozHMI** (2025) — multi-protocol fusion (gRPC + WebSocket + HTTP + QML Bridge)

---

[2.0.0]: https://github.com/user/VersatileEngine/releases/tag/v2.0.0
[1.0.0]: https://github.com/user/VersatileEngine/releases/tag/v1.0.0
