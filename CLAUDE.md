# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

VersatileEngine (VE) is a C++17 reactive data middleware framework built on Qt. It provides a hierarchical, observable data tree with cross-language/cross-process IPC capabilities. Target domains include robotics, medical devices, and complex systems. Licensed under LGPLv3.

## Build Commands

```bash
# Configure (out-of-source builds enforced)
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build all
cmake --build build --config Release

# Build specific targets
cmake --build build --target VE_CORE_LIBRARY
cmake --build build --target VE_SERVICE_LIBRARY
```

CMake options: `BUILD_CORE=ON`, `BUILD_SERVICE=ON` (both default ON). `BUILD_MAIN` exists but is commented out.

Dependencies: Qt5/Qt6 (Core, Network, Widgets), spdlog, asio2 (bundled in `service/3rdparty/`), rapidjson (bundled inside asio2).

No test suite exists in the current codebase.

## Architecture

### Three Layers

**Core** (`core/`) — Qt-dependent shared library. The reactive data tree built on QObject/QVariant.
- `ve::Data` (alias for `imol::ModuleObject`) — Tree node with name, value, parent/children, and Qt signals (`changed`, `activated`, `added`, `removed`).
- `ve::d("path.to.node")` — Global path accessor. `VE_D(path)` is the static-cached macro variant.
- `ve::Module` — Lifecycle: `NONE → INIT → READY → DEINIT`. Register with `VE_REGISTER_MODULE(Name, Class)`.
- `ve::Object` / `ve::Manager` — Base object system with parent/child relationships and HashMap container.
- `ve::Factory<Sig>` — Generic factory pattern for dynamic object creation.

**Service** (`service/`) — IPC layer using asio2 for networking.
- CBS (Compact Binary Service) — C++↔C++ TCP binary protocol with `echo`, `publish`, `subscribe`, `unsubscribe` operations on `single` or `recursive` data structures.
- CommandServer — TCP text command interface.

**Platform** (`core/platform/`) — Crash handling per OS.
- `win/rescue.cpp` — SEH + StackWalk64 + DbgHelp
- `linux/rescue.cpp` — Signal handlers + backtrace + dladdr + cxa_demangle
- `unsupported/rescue.h` — Fallback stub

### Data Navigation API

Tree nodes use short method names: `p(level)` parent, `c(name|index)` child, `b(offset)` sibling, `r("a.b.c")` relative path, `fullName()` full path.

### Serialization

`ve::Data` supports JSON, XML, Binary, and QVariant import/export.

## Code Conventions

- Public headers live in `{component}/include/ve/{component}/`. Implementation in `{component}/src/`.
- The `imol` namespace contains the original implementation layer; `ve` namespace provides the modern API surface wrapping it.
- CMake macros in `cmake/ve_common.cmake`: `ve_add_library()`, `ve_find_sources()`, `ve_find_resources()`, `ve_target_link_qt_components()`.
- Platform-specific code is isolated under `core/platform/{win,linux,unsupported}/`.
- The architecture doc (`docs/ARCHITECTURE.md`, written in Chinese) describes a planned refactoring to decouple from Qt by introducing pure C++17 `ve::Value` and `ve::Node` types.
