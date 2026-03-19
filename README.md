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
[![Qt](https://img.shields.io/badge/Qt-5%20%7C%206-green.svg)](https://www.qt.io/)

---

> **Project Status: Core Layer Complete**
>
> VersatileEngine has evolved over 10 years across multiple commercial projects. The core layer (`libve`) is now a **complete pure C++17 implementation** with 535 unit tests passing. It includes `ve::Var` (16-byte variant), `ve::Node` (reactive data tree), Command system (20+ built-in commands), Service layer (Terminal/HTTP/WebSocket/TCP), and Entry lifecycle. The existing Qt-based API is preserved as the adapter layer (`cpp/qt/`). Performance benchmarks show ve::Node outperforming the legacy Qt-based implementation (imol::ModuleObject) by up to 590x on indexed access.

---

## Introduction

VersatileEngine (**VE**) is a C++17 reactive data middleware framework that provides a **hierarchical, observable data tree** with cross-language/cross-process IPC capabilities.

Core philosophy: **"Everything is a node"** — all data is organized in a global tree. Modules read/write data via paths and respond to changes via signals, communicating indirectly through data nodes rather than direct coupling.

VE has been battle-tested in commercial projects across multiple domains:

| Domain | Project | Period |
|--------|---------|--------|
| 🤖 Industrial Robotics | RobotAssist (ROKAE) | 2015–2017 |
| 🦾 Humanoid Robotics | CyberOne, MozHMI (LCCR) | 2020–2024 |
| 🏥 Medical Imaging | Bezier (Surgical Navigation) | 2022–2023 |
| 🖥️ Embedded HMI | PDS-HMI (Inspection System) | 2023 |
| 🌐 Web Frontend | MozHMI Web Console | 2024 |
| 🔗 Multi-protocol | MozHMI (MovaX 2.0) | 2025–present |

## Features

- **Reactive data tree** — `ve::Node` with `ve::Var` values, signal propagation (bubbling), and path addressing
- **Rich signal system** — `NODE_CHANGED` (value), `NODE_ACTIVATED` (subtree bubbling), `NODE_ADDED`/`NODE_REMOVED` (child lifecycle)
- **Command system** — `Step`/`Pipeline`/`Command` abstraction + 20+ built-in commands (`ls`/`get`/`set`/`json`/`find`/...)
- **Service layer** — Terminal REPL (TCP), HTTP API, WebSocket push, TCP Binary (CBS protocol)
- **Multiple serialization** — JSON (simdjson) / Binary (CBS) / Schema-based import/export
- **Module lifecycle** — `NONE → INIT → READY → DEINIT`, plugin loading, topological sort
- **Event loop** — Asio-based `EventLoop` + `LoopRef` for cross-thread dispatch
- **IPC communication** — CBS binary protocol (C++↔C++), WebSocket JSON (C++↔JS), DDS bridge (FastDDS)
- **Cross-platform** — Windows / Linux / macOS, with crash capture & diagnostics
- **Built-in terminal** — runtime REPL for data tree inspection and manipulation via TCP/netcat
- **High performance** — `child(index)` 590x faster, `iterator` 135x faster, `indexOf` 42x faster than Qt-based legacy (535 tests passing)

## Architecture

```
┌──────────────────────────────────────────────────────────────┐
│  Adapter Layer                                    cpp/ js/ py/│
│    cpp/qt/ (Qt/QML)  │  js/ (WebSocket/JS)  │  cpp/ros/ (ROS)│
├──────────────────────────────────────────────────────────────┤
│  Service Layer                                    service/    │
│    CBS Binary IPC  │  WebSocket JSON  │  Command Server       │
├──────────────────────────────────────────────────────────────┤
│  Core Layer                                         core/     │
│    ve::Object (base class)   │  ve::Factory (factory pattern) │
│    ve::Node (planned)        │  Logging / Terminal / Rescue   │
└──────────────────────────────────────────────────────────────┘
```

### Data Tree Structure

Each `ve::Data` node holds a name, value (QVariant), parent/child relationships, and signals:

```
ve::d("robot")
├── state
│   ├── power     ← set(1) → emits changed signal
│   └── mode
├── config
│   ├── speed
│   └── tool
└── value
    └── joints
```

### Multi-endpoint Access

```
                    ┌──────────────────┐
                    │  ve::Data Tree    │
                    └────────┬─────────┘
                             │
             ┌───────────────┼───────────────┐
             │               │               │
      ┌──────▼──────┐ ┌─────▼──────┐ ┌──────▼──────┐
      │  C++ Direct  │ │ QML Bridge │ │  WebSocket  │
      │   ve::d()    │ │ QuickNode  │ │  veservice  │
      └─────────────┘ └────────────┘ └─────────────┘
       C++ Backend      Qt Quick UI     Web Frontend
```

## Getting Started

### Prerequisites

- **Compiler**: C++17 capable (MSVC 2019+, GCC 7+, Clang 5+)
- **CMake**: 3.15 or later
- **Qt**: 5.12+ or 6.x (Core, Network, Widgets modules)

> Note: All third-party dependencies (asio2, asio, spdlog, cereal, fmt) are bundled in `deps/asio2/` and do not need separate installation.

### Building

```bash
# Configure (out-of-source builds enforced)
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build all
cmake --build build --config Release

# Build core library only
cmake --build build --target VE_CORE_LIBRARY

# Build service library only
cmake --build build --target VE_SERVICE_LIBRARY
```

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_CORE` | `ON` | Build core library (ve::Data, ve::Module, etc.) |
| `BUILD_SERVICE` | `ON` | Build service library (CBS, Command Server, etc.) |
| `BUILD_MAIN` | `ON` | Build main example program |
| `BUILD_SHARED_LIBS` | `ON` | Build shared libraries (OFF for static) |
| `LOG_MIN_FILE_LEVEL` | unset | Minimum file logging level |

### Integration

As a find_package dependency:

```cmake
find_package(VersatileEngine REQUIRED)
target_link_libraries(your_target PRIVATE VE::Core VE::Service)
```

As a subdirectory:

```cmake
add_subdirectory(path/to/VersatileEngine)
target_link_libraries(your_target PRIVATE VE_CORE_LIBRARY VE_SERVICE_LIBRARY)
```

## Usage Examples

> The following examples use the **Qt adapter layer** API (`cpp/qt/`), which provides `ve::Data`, `ve::d()`, `ve::Module` and Qt signal/slot integration. The core layer is being refactored toward a pure C++17 API (`ve::Node`, `ve::Value`).

### Data Tree Operations

```cpp
#include <veCommon>

// Access nodes by path (auto-created)
ve::d("robot.state.power")->set(nullptr, 1);

// Read value
int power = ve::d("robot.state.power")->var().toInt();

// Listen for value changes
connect(ve::d("robot.state.power"), &ve::Data::changed,
    [](const QVariant& newVal, const QVariant& oldVal, void*) {
        qDebug() << "Power changed:" << oldVal << "->" << newVal;
    });

// Subtree watching (bubbling)
ve::d("robot.state")->setWatching(true);
connect(ve::d("robot.state"), &ve::Data::activated,
    [](ve::Data* changed, int type, void*) {
        qDebug() << "State subtree changed:" << changed->fullName();
    });
```

### Module Registration

```cpp
#include <veModule>

class MyModule : public ve::Module {
public:
    void onInit() override {
        // initialization logic
    }
    void onReady() override {
        // module ready, start working
    }
    void onDeinit() override {
        // cleanup
    }
};

VE_REGISTER_MODULE(MyModule, MyModule)
```

### Application Entry Point

```cpp
#include <QApplication>
#include <veCommon>

int main(int argc, char *argv[]) {
    ve::entry::setup("config.ini");
    QApplication a(argc, argv);
    ve::entry::init();

    // ... application logic ...

    int res = a.exec();
    ve::entry::deinit();
    return 0;
}
```

## Project Structure

```
VersatileEngine/
├── core/                       # Core library (C++17, currently Qt-dependent)
│   ├── include/ve/             # Public headers
│   │   ├── global.h            # Global macros & export symbols (pure C++)
│   │   └── core/               # Core API headers
│   │       ├── base.h          # ve::Object / ve::Manager - base class & container
│   │       ├── factory.h       # ve::Factory - generic factory pattern
│   │       ├── log.h           # ve::log - spdlog-based logging system
│   │       └── node.h          # ve::Node - (planned) pure C++ data tree node
│   ├── src/                    # Implementation (includes imol legacy layer)
│   └── platform/               # Platform-specific crash handling
│       ├── win/                # Windows (SEH + StackWalk64)
│       ├── linux/              # Linux (signal + backtrace)
│       └── unsupported/        # Fallback stub
├── service/                    # Service library (IPC layer)
│   └── CMakeLists.txt
├── cpp/                        # Language adapter implementations
│   ├── qt/                     # Qt adapter layer
│   │   ├── core/               # ve::Data, ve::Module, ve::d() (Qt/QObject-based)
│   │   │   └── include/ve/     # Headers: data.h, module.h, common.h, terminal.h
│   │   ├── service/            # CBS, CommandServer, XService (asio2-based)
│   │   │   └── include/ve/     # Headers: compact_binary_service.h, command_server.h
│   │   └── main/               # Qt example application
│   ├── ros1/                   # ROS1 adapter (planned)
│   ├── ros2/                   # ROS2 adapter (planned)
│   └── vtk/                    # VTK adapter (planned)
├── js/                         # JavaScript/TypeScript adapter (planned)
├── py/                         # Python adapter (planned)
├── deps/                       # Bundled third-party dependencies
│   └── asio2/                  # asio2 2.9 (includes asio, spdlog, cereal, fmt)
├── main/                       # Main example application
├── cmake/                      # CMake utility scripts
├── docs/                       # Documentation
│   └── ARCHITECTURE.md         # Architecture overview & evolution plan
├── AUTHORS                     # Authors
├── LICENSE                     # LGPLv3 license
├── CHANGELOG.md                # Version changelog
├── CODE_OF_CONDUCT.md          # Code of conduct
├── CONTRIBUTING.md             # Contribution guidelines
├── SECURITY.md                 # Security policy
└── CMakeLists.txt              # Top-level build configuration
```

## API Quick Reference

### Data Navigation

| Method | Description | Example |
|--------|-------------|---------|
| `ve::d("path")` | Global path accessor (auto-creates nodes) | `ve::d("robot.state.power")` |
| `VE_D("path")` | Static-cached version (high performance) | `VE_D("robot.state.power")` |
| `p(level)` | Navigate up N parent levels | `node->p(2)` |
| `c(name)` | Get child by name | `node->c("power")` |
| `c(index)` | Get child by index | `node->c(0)` |
| `b(offset)` | Sibling node (positive=next, negative=prev) | `node->b(1)` |
| `r("a.b.c")` | Relative path navigation | `node->r("state.power")` |
| `fullName()` | Full path from root | `node->fullName()` |

### Signals

| Signal | Description |
|--------|-------------|
| `changed(newVar, oldVar, changer)` | Node's own value changed |
| `activated(changedNode, type, changer)` | Any node in subtree changed (requires `watch=true`) |
| `added(rname, changer)` | Direct child node added |
| `removed(rname, changer)` | Direct child node removed |
| `gonnaInsert(target, ref, changer)` | About to insert (interceptable) |
| `gonnaRemove(target, ref, changer)` | About to remove (interceptable) |

## Documentation

- [Architecture Overview & Evolution Plan](docs/ARCHITECTURE.md)

## License

This project is licensed under the [GNU Lesser General Public License v3.0](LICENSE).

## Authors

See the [AUTHORS](AUTHORS) file.

- **Thilo** — Creator and primary maintainer
