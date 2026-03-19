# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

VersatileEngine (VE) is a C++17 reactive data middleware framework. The core layer (`libve`) is **pure C++ with zero Qt dependency**; Qt/ROS/RTT adapters are optional. It provides a hierarchical, observable data tree with cross-language/cross-process IPC capabilities. Target domains include robotics, medical devices, and complex systems. Licensed under LGPLv3.

## Build Commands

```bash
# Configure (out-of-source builds enforced)
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build all
cmake --build build --config Release

# Build & run core tests only (no Qt/DDS required)
cmake -B build_test -DVE_BUILD_TEST=ON -DVE_BUILD_QT=OFF -DVE_BUILD_DDS=OFF -DVE_BUILD_RTT=OFF
cmake --build build_test --target ve_test --config Debug
./build_test/bin/Debug/ve_test      # Windows
./build_test/ve_test                # Linux
```

### CMake Options (top-level)

| Option | Default | Description |
|--------|---------|-------------|
| `VE_BUILD_TEST` | OFF | Build `core/test/` → `ve_test` executable (pure C++, no deps beyond libve) |
| `VE_BUILD_QT` | ON | Build `cpp/qt/` → `libveqt` (needs Qt5/Qt6) |
| `VE_BUILD_DDS` | OFF | Build `cpp/ros/` → `libvedds` (needs FastDDS: fastrtps + fastcdr) |
| `VE_BUILD_RTT` | ON | Build `cpp/rtt/` → `libvertt` (pure C++) |

Local per-developer overrides go in `cmake/_local.cmake` (gitignored). Example: `set(VE_BUILD_TEST ON CACHE BOOL "" FORCE)`.

### Dependencies

- **Bundled** (in `deps/`): spdlog, asio, asio2, fmt, cereal, yaml-cpp (static), pugixml (static), nlohmann/json
- **Optional**: Qt5/Qt6 (Core, Network, Widgets; Qml for veQml), FastDDS (fastrtps + fastcdr for DDS adapter)
- CMake targets defined in `cmake/ve_deps.cmake`: `ve_dep_spdlog`, `ve_dep_asio`, `ve_dep_asio2`, `ve_dep_yaml`, `ve_dep_pugixml`, `ve_dep_json`

## Test Suite

535 unit tests in `core/test/`, using a custom header-only framework (`ve_test.h`, ~130 lines). No third-party test framework.

```
core/test/
├── ve_test.h                 — Custom test framework (VE_TEST, VE_ASSERT_*, VE_RUN_ALL)
├── main.cpp                  — Entry point: VE_RUN_ALL()
├── test_basic_traits.cpp     — basic:: type traits (is_comparable, FnTraits, Meta, etc.)
├── test_containers.cpp       — Vector, List, Map, HashMap, Dict
├── test_ordered_hashmap.cpp  — OrderedHashMap (Godot-derived Robin Hood)
├── test_small_vector.cpp     — SmallVector<T,N> inline/heap, copy/move, mixin
├── test_object.cpp           — Object lifecycle + signal/slot + thread safety
├── test_manager.cpp          — Manager add/remove/get
├── test_data.cpp             — AnyData<T>, DataManager, DataList/DataDict
├── test_data_serialize.cpp   — String & YAML serialization
├── test_hashfuncs.cpp        — ve::impl:: hash functions
├── test_log.cpp              — Log system (smoke tests)
├── test_values.cpp           — Values unit conversion
├── test_var.cpp              — Var (10 types, copy/move, list/dict, CUSTOM)
├── test_command.cpp          — Step, Pipeline, Command, Factory, Object::once
├── test_loop.cpp             — Loop, EventLoop, LoopRef
├── test_node_basic.cpp       — Node creation, child query, count, iteration
├── test_node_signal.cpp      — Node signals (ADDED, REMOVED, ACTIVATED, bubbling)
├── test_node_path.cpp        — key, path, resolve, ensure, erase, shadow, schema
├── test_node_value.cpp       — Node value operations and ve::n() global accessor
├── test_node_navigation.cpp  — parent, indexOf, sibling, prev/next, isAncestorOf
├── test_node_management.cpp  — insert, append, take, remove, clear, name validation
└── test_node_bench.cpp       — Stress tests, complex structures, benchmarks
```

See `docs/internal/test/core-test-plan.md` for detailed test case design.

## Architecture

### Directory Structure

```
VersatileEngine/
├── core/                   Pure C++17 core → libve (shared library)
│   ├── include/ve/
│   │   ├── global.h            Global macros (VE_API, VE_AUTO_RUN, etc.)
│   │   ├── core/
│   │   │   ├── base.h          Object, Manager, containers, type traits, KVAccessor
│   │   │   ├── var.h           Var (16B variant, 10 types + CUSTOM)
│   │   │   ├── node.h          Node (reactive data tree, Vector+Hash, Pool, shadow, schema)
│   │   │   ├── command.h       Command, step.h (Step), pipeline.h (Pipeline), result.h (Result)
│   │   │   ├── data.h          AnyData<T>, DataManager, DataList, DataDict, YAML serialize
│   │   │   ├── factory.h       Factory<Sig> template + Pool<T> + Pooled<T>
│   │   │   ├── module.h        Module lifecycle (NONE→INIT→READY→DEINIT)
│   │   │   ├── loop.h          Loop<T>, EventLoop, LoopRef, loop::main/pool
│   │   │   ├── convert.h       Convert<T> customization point (toString/fromString/toBin/fromBin)
│   │   │   ├── log.h           Logging interface (spdlog backend)
│   │   │   ├── rescue.h        Crash handler API
│   │   │   └── impl/
│   │   │       ├── hashfuncs.h       Hash functions (DJB2, MurmurHash3, Wang)
│   │   │       ├── ordered_hashmap.h OrderedHashMap (Robin Hood + insertion order)
│   │   │       ├── json.h            JSON serialize (simdjson parse, stringify, exportTree)
│   │   │       └── bin.h             Binary serialize (CBS-compatible)
│   │   ├── service/
│   │   │   ├── terminal.h      Terminal REPL (TCP, port 5061)
│   │   │   ├── http_server.h   HTTP Server (REST-like, port 8080)
│   │   │   ├── ws_server.h     WebSocket Server (port 8081)
│   │   │   └── tcp_bin_server.h TCP Binary Server (CBS, port 5065)
│   │   └── entry.h             Entry lifecycle (setup→init→run→deinit), plugin, version
│   ├── src/                base.cpp, node.cpp, var.cpp, command.cpp, entry.cpp, loop.cpp, ...
│   ├── platform/           Crash handlers: win/ (SEH+StackWalk64), linux/ (signal+backtrace), unsupported/
│   └── test/               535 unit tests (custom framework, pure C++)
│
├── cpp/qt/                 Qt adapter modules
│   ├── imol/               Legacy data tree (imol::ModuleObject)
│   ├── veQtBase/           Qt core utilities (ve::entry, data wrappers)
│   ├── veTerminal/         Terminal widget + TCP server
│   ├── veService/          IPC layer (CBS, XService, CommandServer)
│   ├── veQml/              QML bridge (QuickNode)
│   ├── veExample/          Demo application
│   ├── veQtImGui/          (placeholder)
│   └── veQtVtk/            (placeholder)
│
├── cpp/rtt/                Pure C++ RTT adapter (xcore-derived)
│   ├── veRttCore/          Object, CommandObject, Procedure, CIP, LoopObject, NetObject, JsonRef
│   └── XService/           XService server implementation
│
├── cpp/ros/                DDS adapter (FastDDS) → libvedds
│   └── veFastDDS/          Participant, Topic, Service, DynTypes, Bridge
│
├── deps/                   Bundled dependencies
│   ├── asio2/              asio2 + asio + spdlog + fmt + cereal
│   ├── yaml-cpp/           yaml-cpp 0.9 (built as static lib)
│   ├── pugixml/            pugixml 1.15 (built as static lib)
│   └── nlohmann/           nlohmann/json (header-only)
│
├── cmake/                  CMake utilities
│   ├── ve_compile.cmake    Compiler flags (C++17, /utf-8, etc.)
│   ├── ve_common.cmake     Helper macros (ve_collect, ve_qt_module_vars, etc.)
│   ├── ve_deps.cmake       Dependency targets
│   └── _local.cmake        Per-developer overrides (gitignored)
│
└── docs/
    ├── ARCHITECTURE.md     Architecture overview & evolution roadmap
    ├── core/               Core module documentation (platform/rescue)
    └── internal/
        ├── plan/           Phased development plan (Phase 0–7)
        ├── test/           Test plan (core-test-plan.md)
        ├── analysis/       Design analysis (framework comparison, decision fork)
        ├── advise/         Technical advice documents
        ├── godot-learnings/ Godot engine study notes
        ├── history/        Historical project documentation
        └── notes/          Author design notes
```

### Core Components

- **`ve::Var`** — 16-byte variant (NONE/BOOL/INT/INT64/DOUBLE/STRING/BIN/LIST/DICT/POINTER/CUSTOM), Convert<T> extension point
- **`ve::Node`** — Reactive data tree node (Vector+Hash storage, Pool allocation, same-name `#N`, shadow, schema, signal bubbling)
- **`ve::Object`** — Base class with name, parent/child, integer signal/slot system, thread-safe dispatch, LoopRef queued dispatch
- **`ve::Manager`** — Object container (HashMap<string, Object*>), parent management
- **Containers** — `Vector<T>`, `List<T>`, `Map<K,V>`, `HashMap<K,V>`, `Dict<V>`, `OrderedHashMap<K,V>`, `OrderedDict<V>`, `SmallVector<T,N>`, `Array<T,N>`
- **Type Traits (`ve::basic::`)** — `is_comparable`, `is_outputable`, `is_inputable`, `FnTraits<F>` (function introspection), `Meta` (RTTI helpers)

### Command System

- **`ve::Step`** — Single execution unit: `Result(const Var&)`, flexible signatures
- **`ve::Pipeline`** — State machine: IDLE→RUNNING→PAUSED→DONE/ERRORED, signals CMD_DONE/CMD_ERROR
- **`ve::Command`** — Named sequence of Steps, `pipeline()` creates execution instance
- **Built-in commands** — `ls`, `info`, `get`, `set`, `add`, `rm`, `mv`, `mk`, `find`, `erase`, `json`, `help`, `child`, `shadow`, `watch`, `iter`, `schema`, `cmd`

### Service Layer

- **`ve::Terminal`** — TCP REPL (port 5061), TerminalSession per connection, command execution + tab completion
- **`ve::HttpServer`** — REST-like HTTP API (port 8080)
- **`ve::WsServer`** — WebSocket real-time Node updates (port 8081)
- **`ve::TcpBinServer`** — CBS binary protocol (port 5065)
- **`ve::SubscribeService`** — Node change subscription service

### Entry & Lifecycle

- **`ve::Entry`** — Config-driven lifecycle: NONE→SETUP→INIT→READY→RUNNING→SHUTDOWN
- **`ve::Module`** — Module lifecycle (NONE→INIT→READY→DEINIT), topological sort
- **`ve::Loop`** — Asio event loop (`EventLoop` + `loop::main()` / `loop::pool(n)` + `LoopRef`)
- **`ve::plugin`** — Dynamic library loading (load/unload/loaded)

### Data Layer (data.h)

- **`ve::AnyData<T>`** — Type-safe reactive data with signals, bind(), YAML serialization
- **`ve::DataManager`** — Path-based data registry (`data::create`, `data::get`, `data::at`)
- **`ve::DataList` / `ve::DataDict`** — Heterogeneous typed collections

### Serialization

- **`ve::Schema`** — Field list for Node structure, `schema::exportAs<F>` / `importAs<F>`
- **`json::`** — `stringify`, `parse`, `exportTree`, `importTree` (simdjson backend)
- **`bin::`** — `exportTree`, `importTree`, `writeVar`, `readVar` (CBS-compatible binary)

### CMake Layout

- **Root `CMakeLists.txt`** — Project setup, deps, delegates to `core/`, `cpp/qt/`, `cpp/ros/`, `cpp/rtt/`, `core/test/`
- **`core/CMakeLists.txt`** — Builds `libve` shared library, exports `VE_CORE_LIBRARY` to parent scope
- **`core/test/CMakeLists.txt`** — Builds `ve_test`, links against `libve` only
- **`cpp/qt/CMakeLists.txt`** — Finds Qt, manages Qt module options

## Code Conventions

- Pure C++ headers in `core/include/ve/core/`. Qt adapter headers in `cpp/qt/{module}/include/`.
- C++17 features used: `std::enable_if_t`, `std::void_t`, `if constexpr`, `std::is_same_v`, `std::index_sequence`
- Macros prefixed with `VE_`: `VE_DECLARE_T_CHECKER`, `VE_DECLARE_T_FUNC_CHECKER`, `VE_INHERIT_CONSTRUCTOR`, `VE_DATA_UPDATE`, `VE_D_FUNC_IMPL`
- The `imol` namespace (in `cpp/qt/imol/`) is the legacy implementation; `ve` namespace is the modern API
- Platform-specific code isolated under `core/platform/{win,linux,unsupported}/`
- Internal planning docs in `docs/internal/plan/`. Historical analysis in `docs/internal/history/`
- Local CMake overrides via `cmake/_*.cmake` files (gitignored)
- DLL export: core uses `VE_LIBRARY` / `VE_API`; Qt modules have their own export macros
