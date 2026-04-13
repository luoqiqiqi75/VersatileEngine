# VersatileEngine

```
 /\   /\ ___
 \ \ / /| __|
  \ v / | __|
   \_/  |___|  VersatileEngine (aka VeryEasy)
```

**A reactive data tree and glue layer for C++ systems**

[![License: LGPL v3](https://img.shields.io/badge/License-LGPLv3-blue.svg)](https://www.gnu.org/licenses/lgpl-3.0)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/std/the-standard)
[![CMake](https://img.shields.io/badge/CMake-3.15%2B-blue.svg)](https://cmake.org/)

---

VersatileEngine (**VE**, aka **VeryEasy**) is a pure C++17 core plus a set of adapters and services built around one idea:
**the running system should expose a single observable node tree instead of a pile of private state and ad hoc glue code**.

VE is not meant to replace Qt, ROS, DDS, web stacks, or your application framework.
It is meant to connect them through one inspectable model.

The nickname **VeryEasy** reflects the design philosophy: **99% configuration through JSON → Node tree, minimal C++ code**.
Modules focus on business logic, not configuration plumbing. `ve.exe + config.json` gets you running without writing a single line of C++.

Detailed design lineage is in [docs/HISTORY.md](docs/HISTORY.md).

## Why VE

- **Data tree as system bus**: `ve::Node` is both state storage and observation surface.
- **Glue layer, not forced rewrite**: adapters translate existing systems into the node tree instead of imposing a new business framework.
- **Same model for local and remote access**: C++, Terminal, HTTP, WebSocket, binary IPC, and DDS all talk to the same tree.
- **Runtime-first debugging**: production processes can be inspected with terminal commands, HTTP, or WebSocket without custom debug tooling.

## Architecture

```
+-----------------------------------------------------------------+
|  Programs and Adapters                           qt/ ros/ rtt/   |
|  Qt and QML | DDS and ROS | RTT and xcore | JS and tools        |
+-----------------------------------------------------------------+
|  Service Layer                                   ve/service/     |
|  Terminal REPL | HTTP API | WebSocket push | TCP Binary IPC     |
+-----------------------------------------------------------------+
|  Core Layer                              ve/ (pure C++17)       |
|  Node | Var | Object | Command | Module | Entry | Loop          |
+-----------------------------------------------------------------+
```

## Distinctive Ideas

### Data Tree as Bus

All durable runtime state lives in a hierarchical tree. A node has:

- a name
- a value (`ve::Var`)
- ordered children
- change, add, remove, and bubble signals

Modules do not need direct references to each other when they can agree on paths such as `ve/robot/state/power` or `browser/session/current`.

### Glue Layer Instead of Framework Rewrite

VE works best when each technology keeps doing what it is good at:

- **Core C++** owns domain logic and deterministic runtime behavior.
- **Qt and QML** own native desktop or embedded UI concerns.
- **ROS and DDS** own robotics transport and distributed topics.
- **Web clients** consume the same state through HTTP or WebSocket.

The VE layer exists to map those worlds into one shared tree and one consistent set of commands.

### One Runtime, Many Surfaces

The same process can expose:

- direct in-process C++ access through `ve::n()`
- terminal debugging on TCP port `10000`
- HTTP node inspection on port `12000`
- WebSocket subscriptions on port `12100`
- binary IPC on port `11000`

That is the practical payoff of the architecture: the tree is not only a model, it is the runtime control surface.

### Minimal Application Shape

The smallest VE application is a normal `main()` plus configuration:

```cpp
#include <ve/entry.h>

int main(int argc, char* argv[]) {
    return ve::entry::exec(argc, argv);
}
```

`ve::entry` loads configuration into the node tree, loads plugins, creates modules, enters the main loop, and shuts everything down in reverse order.

## Architecture

```
+-----------------------------------------------------------------+
|  Ecosystem Layer                              qt/ ros/ rtt/      |
|    qt/ (Qt/QML)  |  ve/js/ (WebSocket/JS)  |  ros/ (DDS)       |
+-----------------------------------------------------------------+
|  Service Layer                                   ve/service/     |
|    Terminal (TCP)  |  HTTP  |  WebSocket  |  TCP Binary (CBS)   |
+-----------------------------------------------------------------+
|  Core Layer                              ve/ (pure C++17, no Qt) |
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
- **Qt** (optional): 5.12+ or 6.x (only needed for `qt/` modules)
- **FastDDS** (optional): fastrtps + fastcdr (only needed for `ros/` DDS adapter)

> All other dependencies (asio2, asio, spdlog, fmt, cereal, simdjson) are bundled in `deps/` and require no separate installation.

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
| `VE_BUILD_TEST` | ON | Build `ve/test/` -> `ve_test` executable (pure C++, no deps beyond libve) |
| `VE_BUILD_EXAMPLE` | ON | Build example module DLLs (ve_example, etc.) |
| `VE_BUILD_QT` | ON | Build `qt/` -> `libveqt` (needs Qt5/Qt6) |
| `VE_BUILD_DDS` | ON | Build `ros/` -> `libvedds` (needs FastDDS) |
| `VE_BUILD_RTT` | ON | Build `rtt/` -> `libvertt` (pure C++) |
| `VE_INSTALL` | OFF | Install VE library, headers and CMake config |

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

// Slash-path accessor (shorthand)
auto* speed = ve::n("robot/config/speed");
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
    return ve::entry::exec(argc, argv);
}
```

Or step-by-step:

```cpp
#include <ve/entry.h>

int main(int argc, char* argv[]) {
    ve::entry::setup("ve.json");
    ve::entry::init();
    int code = ve::entry::run();    // blocks on event loop
    ve::entry::deinit();
    return code;
}
```

### Terminal REPL

Connect to the built-in terminal via TCP:

```bash
# netcat / telnet to port 10000
nc localhost 10000

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
+-- ve/                         VE framework (pure C++17 core -> libve + JS)
|   +-- include/ve/             Public headers
|   |   +-- global.h            Global macros (VE_API, VE_AUTO_RUN, ...)
|   |   +-- entry.h             Entry lifecycle (setup/init/run/deinit), plugin, version
|   |   +-- core/               Core API headers
|   |   |   +-- base.h          Containers, type traits, KVAccessor, Manager
|   |   |   +-- object.h        Object (signal/slot, thread-safe, parent/child)
|   |   |   +-- var.h           Var (16B variant, 10 types + CUSTOM)
|   |   |   +-- node.h          Node (reactive data tree)
|   |   |   +-- schema.h        Schema definition + Node serialization
|   |   |   +-- step.h          Step - single execution unit
|   |   |   +-- pipeline.h      Pipeline - state machine for Step chains
|   |   |   +-- command.h       Command system (named Step sequences + built-in commands)
|   |   |   +-- factory.h       Factory<Sig>, Pool<T>, Pooled<T>
|   |   |   +-- module.h        Module lifecycle
|   |   |   +-- loop.h          EventLoop, LoopRef
|   |   |   +-- convert.h       Convert<T> extension point
|   |   |   +-- log.h           Logging (spdlog backend)
|   |   |   +-- impl/           Hash functions, OrderedHashMap, SmallVector, JSON, Binary
|   |   +-- service/            Terminal, HTTP, Binary TCP services, crash handler
|   +-- src/                    Implementation files
|   |   +-- core/               Core implementations
|   |   +-- module/             Internal modules (core, terminal, http, ws, tcp_bin)
|   |   +-- service/            Service implementations
|   +-- platform/               Crash handlers: win/ linux/ unsupported/
|   +-- test/                   513 unit tests (custom framework, pure C++)
|   +-- js/                     JavaScript deliverables
|   |   +-- veservice.js        Standalone vanilla WS client
|   |   +-- ve-sdk/             TypeScript SDK (HTTP + WS)
|   |   +-- ve-app/             React admin UI (Node inspector)
|   +-- program/                ve.exe entry process + module DLLs
|       +-- main.cpp            int main() { return ve::entry::exec(argc, argv); }
|       +-- ve.json             Default configuration
|       +-- example/            ve_example.dll (ExampleModule)
|
+-- qt/                         VE + Qt ecosystem (optional, needs Qt5/6)
|   +-- include/
|   |   +-- ve/qt/              Qt-specific public headers (qt_entry, var_qt, node_qobject, qml/, ...)
|   |   +-- imol/               IMOL data tree API and bridge headers
|   +-- src/                    base/, terminal/, service/, qml/
|   +-- program/
|       +-- browser/            veqtbrowser.dll (BrowserModule)
|
+-- rtt/                        VE + RTT (pure C++, xcore-derived)
|   +-- veRttCore/              CommandObject, Procedure, CIP, LoopObject
|   +-- XService/               XService server
|
+-- ros/                        VE + ROS/DDS (optional, needs FastDDS)
|   +-- veFastDDS/              Participant, Topic, Service, Bridge
|   +-- veRos/                  ROS integration: command_service, data, ros_module
|
+-- deps/                       Bundled dependencies
|   +-- asio2/                  asio2 + asio + spdlog + fmt + cereal
|   +-- simdjson/               simdjson (high-performance JSON parser)
|
+-- cmake/                      CMake utilities
+-- docs/                       Documentation
+-- CMakeLists.txt              Top-level build configuration
```

## Core API Quick Reference

### Node Navigation

| Method | Description | Example |
|--------|-------------|---------|
| `ve::n("path")` | Global slash-path accessor that creates missing nodes | `ve::n("robot/state/power")` |
| `node::root()` | Global root node | `ve::node::root()` |
| `find(path)` | Read-only path lookup | `node->find("state/power")` |
| `at(path)` | Path lookup with creation | `node->at("state/power")` |
| `child(name, overlap)` | Get named child by overlap index | `node->child("item", 1)` |
| `child(index)` | Get child by insertion order index | `node->child(0)` |
| `parent(level)` | Navigate up N levels | `node->parent(2)` |
| `sibling(offset)` | Navigate to neighbor node | `node->sibling(1)` |
| `copy(other)` | Sync value and subtree from another node | `node->copy(template_node)` |
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
| Terminal | 10000 | TCP text | REPL with tab completion |
| HTTP | 12000 | HTTP | REST-like Node access + static files |
| WebSocket | 12100 | WS JSON | Real-time Node change push |
| TCP Binary | 11000 | CBS | High-efficiency binary IPC |

Port rule: **first three digits** = service family (fixed); **last two** increment on bind failure. See [docs/SERVICE.md](docs/SERVICE.md) (*Port numbering*).

## Documentation

- [Service Reference](docs/SERVICE.md)
- [Design Guide](docs/DESIGN.md)
- [Core API Guide](docs/CORE.md)
- [Coding Style](docs/CODING_STYLE.md)
- [History and Design Lineage](docs/HISTORY.md)
- [HTTP and curl Debugging](docs/local-http-curl-debug.md)
- [Cursor MCP Quick Start](docs/cursor-mcp-usage.md)

## License

This project is licensed under the [GNU Lesser General Public License v3.0](LICENSE).

## Authors

See the [AUTHORS](AUTHORS) file.

- **Thilo** - Creator and primary maintainer
