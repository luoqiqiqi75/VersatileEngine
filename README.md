# Versatile Engine

```
 /\   /\ ___
 \ \ / /| __|
  \ v / | __|
   \_/  |___|  VersatileEngine
```

**Cross-domain, cross-language, cross-platform reactive data middleware**

[![License: LGPL v3](https://img.shields.io/badge/License-LGPLv3-blue.svg)](https://www.gnu.org/licenses/lgpl-3.0)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/std/the-standard)
[![CMake](https://img.shields.io/badge/CMake-3.15%2B-blue.svg)](https://cmake.org/)

---

> **v2.0 - Pure C++17 Core Complete**
>
> The core layer (`libve`) is now a **pure C++17 implementation with zero Qt dependency**. It includes `ve::Var` (16-byte variant), `ve::Node` (reactive data tree with 535 unit tests), Command system (20+ built-in commands), Service layer (Terminal/HTTP/WebSocket/TCP Binary), Module lifecycle, and Entry orchestration. Qt/ROS/RTT adapters are optional.

---

## Introduction

VersatileEngine (**VE**) is a C++17 reactive data middleware framework that provides a **hierarchical, observable data tree** with cross-language/cross-process IPC capabilities.

Core philosophy: **"Everything is a node"** - all data is organized in a global tree. Modules read/write data via paths and respond to changes via signals, communicating indirectly through data nodes rather than direct coupling.

VE has been battle-tested in commercial projects across multiple domains:

| Domain | Project | Period |
|--------|---------|--------|
| Industrial Robotics | RobotAssist (ROKAE) | 2015-2017 |
| Humanoid Robotics | CyberOne, MozHMI (LCCR) | 2020-2024 |
| Medical Imaging | Bezier (Surgical Navigation) | 2022-2023 |
| Embedded HMI | PDS-HMI (Inspection System) | 2023 |
| Web Frontend | MozHMI Web Console | 2024 |
| Multi-protocol | MozHMI (MovaX 2.0) | 2025-present |

## Features

- **Reactive data tree** - `ve::Node` with `ve::Var` values, signal propagation (bubbling), and path addressing
- **Rich signal system** - `NODE_CHANGED` (value), `NODE_ACTIVATED` (subtree bubbling), `NODE_ADDED`/`NODE_REMOVED` (child lifecycle)
- **Command system** - `Step`/`Pipeline`/`Command` abstraction + 20+ built-in commands (`ls`/`get`/`set`/`json`/`find`/...)
- **Service layer** - Terminal REPL (TCP 5061), HTTP API (8080), WebSocket push (8081), TCP Binary CBS (5065)
- **Multiple serialization** - JSON (simdjson) / Binary (CBS) / Schema-based import/export
- **Module lifecycle** - `NONE -> INIT -> READY -> DEINIT`, plugin loading, topological sort
- **Event loop** - Asio-based `EventLoop` + `LoopRef` for cross-thread dispatch
- **IPC communication** - CBS binary protocol (C++<->C++), WebSocket JSON (C++<->JS), DDS bridge (FastDDS)
- **Cross-platform** - Windows / Linux / macOS, with crash capture & diagnostics
- **High performance** - `child(index)` 590x faster, `iterator` 135x faster, `indexOf` 42x faster than Qt-based legacy

## Architecture

```
+-----------------------------------------------------------------+
|  Adapter Layer                                   cpp/ js/ py/    |
|    cpp/qt/ (Qt/QML)  |  js/ (WebSocket/JS)  |  cpp/ros/ (DDS)  |
+-----------------------------------------------------------------+
|  Service Layer                                   core/service/   |
|    Terminal (TCP)  |  HTTP  |  WebSocket  |  TCP Binary (CBS)   |
+-----------------------------------------------------------------+
|  Core Layer                           core/ (pure C++17, no Qt) |
|    ve::Node (data tree)    |  ve::Var (16B variant)             |
|    ve::Command (20+ cmds)  |  ve::Module (lifecycle)            |
|    ve::Object (signal/slot)|  ve::Entry (orchestration)         |
|    ve::Loop (asio event)   |  ve::Factory / ve::Pool            |
+-----------------------------------------------------------------+
```

### Data Tree

All data lives in a global tree. Each `ve::Node` holds a name, a `ve::Var` value, parent/child relationships, and signals:

```
ve::n("/robot")
+-- state
|   +-- power     <-- set(Var(1)) -> emits NODE_CHANGED
|   +-- mode
+-- config
|   +-- speed
|   +-- tool
+-- value
    +-- joints
```

### Multi-endpoint Access

```
                    +------------------+
                    |  ve::Node Tree   |
                    +--------+---------+
                             |
             +---------------+---------------+
             |               |               |
      +------+------+ +-----+------+ +------+------+
      |  C++ Direct  | | WebSocket  | |  Terminal   |
      |  ve::n()     | | JSON push  | |  TCP REPL   |
      +-------------+ +------------+ +-------------+
       C++ Backend     Web Frontend    Debug / CLI
```

## Getting Started

### Prerequisites

- **Compiler**: C++17 capable (MSVC 2019+, GCC 7+, Clang 5+)
- **CMake**: 3.15 or later
- **Qt** (optional): 5.12+ or 6.x (only needed for `cpp/qt/` adapter modules)
- **FastDDS** (optional): fastrtps + fastcdr (only needed for `cpp/ros/` DDS adapter)

> All other dependencies (asio2, asio, spdlog, fmt, cereal, yaml-cpp, pugixml, nlohmann/json) are bundled in `deps/` and require no separate installation.

### Building

```bash
# Configure (out-of-source builds enforced)
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build all
cmake --build build --config Release

# Build & run core tests only (no Qt / DDS required)
cmake -B build_test -DVE_BUILD_TEST=ON -DVE_BUILD_QT=OFF -DVE_BUILD_DDS=OFF -DVE_BUILD_RTT=OFF
cmake --build build_test --target ve_test --config Debug
./build_test/bin/Debug/ve_test     # Windows
./build_test/ve_test               # Linux
```

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `VE_BUILD_TEST` | OFF | Build `core/test/` -> `ve_test` executable (pure C++, no deps beyond libve) |
| `VE_BUILD_QT` | ON | Build `cpp/qt/` -> `libveqt` (needs Qt5/Qt6) |
| `VE_BUILD_DDS` | OFF | Build `cpp/ros/` -> `libvedds` (needs FastDDS) |
| `VE_BUILD_RTT` | ON | Build `cpp/rtt/` -> `libvertt` (pure C++) |

Local per-developer overrides go in `cmake/_local.cmake` (gitignored):

```cmake
set(VE_BUILD_TEST ON CACHE BOOL "" FORCE)
set(VE_BUILD_QT   OFF CACHE BOOL "" FORCE)
```

### Integration

**As a subdirectory** (default: no install; parent builds and links VE in-tree):

```cmake
add_subdirectory(path/to/VersatileEngine)
target_link_libraries(your_target PRIVATE ve)  # and optionally vedds if VE_BUILD_DDS=ON
```

**Install and use via find_package** (set `VE_INSTALL=ON` when building VE as top-level):

```bash
cd VersatileEngine && mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local -DVE_INSTALL=ON
cmake --build . && cmake --install .
```

Then in another project:

```cmake
find_package(VersatileEngine REQUIRED)
target_link_libraries(myapp PRIVATE VersatileEngine::ve)
```

**`VE_INSTALL`** defaults to **OFF** (friendly to `add_subdirectory`). Set to ON when you want to install VE from a top-level build.

## Usage Examples

### Data Tree Operations (Pure C++)

```cpp
#include <ve/core/node.h>

// Access nodes by slash path (auto-created)
auto* power = ve::n("/robot/state/power");
power->set(ve::Var(1));

// Read value
int val = ve::n("/robot/state/power")->get<int>();

// Listen for value changes
power->connect(ve::Node::NODE_CHANGED, myObj, [](const ve::Var& args) {
    // args is {new_value, old_value}
});

// Subtree watching (bubbling)
auto* state = ve::n("/robot/state");
state->watch(true);
state->connect(ve::Node::NODE_ACTIVATED, myObj, [](const ve::Var& args) {
    // triggered when any descendant changes
});

// Dot-path accessor (alternative)
auto* speed = ve::d("robot.config.speed");
speed->set(ve::Var(1.5));
```

### Module Registration

```cpp
#include <ve/core/module.h>

class MyModule : public ve::Module
{
public:
    using Module::Module;

    void init() override {
        ve::n("/robot/state/power")->set(ve::Var(0));
    }

    void ready() override {
        // all modules initialized, start working
    }

    void deinit() override {
        // cleanup
    }
};

VE_REGISTER_MODULE(my_module, MyModule)
```

### Application Entry Point

```cpp
#include <ve/entry.h>

int main(int argc, char* argv[]) {
    ve::entry::setup("config.yaml");
    ve::entry::init();
    ve::entry::run();    // blocks on event loop
    ve::entry::deinit();
    return 0;
}
```

### Terminal REPL

Connect to the built-in terminal via TCP:

```bash
# netcat / telnet to port 5061
nc localhost 5061

ve> ls /robot
state/
config/
value/

ve> get /robot/state/power
1

ve> set /robot/state/power 0
OK
```

## Project Structure

```
VersatileEngine/
+-- core/                       Pure C++17 core -> libve (shared library)
|   +-- include/ve/             Public headers
|   |   +-- global.h            Global macros (VE_API, VE_AUTO_RUN, ...)
|   |   +-- core/               Core API headers
|   |   |   +-- base.h          Object, Manager, containers, type traits
|   |   |   +-- var.h           Var (16B variant, 10 types + CUSTOM)
|   |   |   +-- node.h          Node (reactive data tree)
|   |   |   +-- command.h       Command system (Step, Pipeline, Command)
|   |   |   +-- data.h          AnyData<T>, DataManager
|   |   |   +-- factory.h       Factory<Sig>, Pool<T>, Pooled<T>
|   |   |   +-- module.h        Module lifecycle
|   |   |   +-- loop.h          EventLoop, LoopRef
|   |   |   +-- log.h           Logging (spdlog backend)
|   |   |   +-- convert.h       Convert<T> extension point
|   |   |   +-- rescue.h        Crash handler API
|   |   |   +-- impl/           Hash functions, OrderedHashMap, JSON, Binary
|   |   +-- service/            Terminal, HTTP, WebSocket, TCP Binary servers
|   |   +-- entry.h             Entry lifecycle + plugin + version
|   +-- src/                    Implementation files
|   +-- platform/               Crash handlers: win/ linux/ unsupported/
|   +-- test/                   535 unit tests (custom framework, pure C++)
|
+-- cpp/qt/                     Qt adapter modules (optional, needs Qt5/6)
|   +-- imol/                   Legacy data tree (imol::ModuleObject)
|   +-- veQtBase/               Qt core utilities
|   +-- veTerminal/             Terminal widget
|   +-- veService/              IPC layer (CBS, XService)
|   +-- veQml/                  QML bridge (QuickNode)
|   +-- veExample/              Demo application
|
+-- cpp/rtt/                    Pure C++ RTT adapter (xcore-derived)
|   +-- veRttCore/              CommandObject, Procedure, CIP, LoopObject
|   +-- XService/               XService server
|
+-- cpp/ros/                    DDS adapter (optional, needs FastDDS)
|   +-- veFastDDS/              Participant, Topic, Service, Bridge
|
+-- deps/                       Bundled dependencies
|   +-- asio2/                  asio2 + asio + spdlog + fmt + cereal
|   +-- yaml-cpp/               yaml-cpp 0.9 (static)
|   +-- pugixml/                pugixml 1.15 (static)
|   +-- nlohmann/               nlohmann/json (header-only)
|
+-- cmake/                      CMake utilities
+-- docs/                       Documentation
+-- js/                         JavaScript/TypeScript packages
+-- CMakeLists.txt              Top-level build configuration
```

## Core API Quick Reference

### Node Navigation

| Method | Description | Example |
|--------|-------------|---------|
| `ve::n("/path")` | Global slash-path accessor (auto-creates) | `ve::n("/robot/state/power")` |
| `ve::d("dot.path")` | Global dot-path accessor | `ve::d("robot.state.power")` |
| `node::root()` | Global root node | `ve::node::root()` |
| `parent(level)` | Navigate up N levels | `node->parent(2)` |
| `child(name)` | Get child by name | `node->child("power")` |
| `child(index)` | Get child by index | `node->child(0)` |
| `sibling(offset)` | Sibling node (+/-) | `node->sibling(1)` |
| `resolve(path)` | Relative path navigation | `node->resolve("state/power")` |
| `ensure(path)` | Resolve, creating if needed | `node->ensure("state/power")` |
| `path()` | Full path from root | `node->path()` |

### Node Signals

| Signal | Description |
|--------|-------------|
| `NODE_CHANGED` | Node's own value changed |
| `NODE_ACTIVATED` | Any descendant changed (requires `watch(true)`) |
| `NODE_ADDED` | Direct child node added |
| `NODE_REMOVED` | Direct child node removed |

### Var Types

| Type | C++ | Size |
|------|-----|------|
| `NONE` | (empty) | 0 |
| `BOOL` | `bool` | inline |
| `INT` | `int` | inline |
| `INT64` | `int64_t` | inline |
| `DOUBLE` | `double` | inline |
| `STRING` | `std::string` | heap ptr |
| `BIN` | `std::vector<uint8_t>` | heap ptr |
| `LIST` | `std::vector<Var>` | heap ptr |
| `DICT` | `Dict<Var>` | heap ptr |
| `POINTER` | `void*` | inline |
| `CUSTOM` | `CustomData*` | heap ptr |

### Service Endpoints

| Service | Port | Protocol | Description |
|---------|------|----------|-------------|
| Terminal | 5061 | TCP text | REPL with tab completion |
| HTTP | 8080 | HTTP | REST-like Node access |
| WebSocket | 8081 | WS JSON | Real-time Node change push |
| TCP Binary | 5065 | CBS | High-efficiency binary IPC |

## Documentation

- [Architecture Overview & Evolution Plan](docs/ARCHITECTURE.md)
- [Core Module Documentation](docs/core/README.md)

## License

This project is licensed under the [GNU Lesser General Public License v3.0](LICENSE).

## Authors

See the [AUTHORS](AUTHORS) file.

- **Thilo** - Creator and primary maintainer
