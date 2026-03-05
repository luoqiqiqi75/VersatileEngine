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
```

CMake options (in `cpp/qt/CMakeLists.txt`): `VE_BUILD_IMOL`, `VE_BUILD_TERMINAL`, `VE_BUILD_QT_BASE`, `VE_BUILD_SERVICE`, `VE_BUILD_QML`, `VE_BUILD_EXAMPLE`.

Dependencies: Qt5/Qt6 (Core, Network, Widgets; Qml optional for veQml), spdlog, asio2 (bundled in `deps/asio2/`), rapidjson (bundled inside asio2's `3rd/cereal/external/`).

No test suite exists in the current codebase.

## Architecture

### Directory Structure

- **`deps/`** — Bundled header-only dependencies (asio2, spdlog, asio, cereal, fmt). CMake targets defined in `cmake/ve_deps.cmake`.
- **`core/`** — Pure C++ headers and sources (no Qt). Built as `libve`.
  - `ve::Object` / `ve::Manager` — Base object system with parent/child relationships.
  - `ve::Factory<Sig>` — Generic factory pattern.
  - `ve::log` — Logging interface.
  - `core/platform/` — Platform crash handling (`win/`, `linux/`, `unsupported/`).
- **`cpp/qt/`** — All Qt-dependent modules, managed by `cpp/qt/CMakeLists.txt`:
  - **`imol/`** — Legacy data tree library (`imol::ModuleObject`). Own DLL export: `CORE_LIBRARY` / `CORESHARED_EXPORT`.
  - **`veTerminal/`** — Terminal widget + TCP server. Own DLL export: `VE_TERMINAL_LIBRARY` / `VE_TERMINAL_API`.
  - **`veQtBase/`** — Qt core utilities (`ve::Data`, `ve::entry`, `ve::Module`). DLL export: `VE_LIBRARY` / `VE_API`.
  - **`veService/`** — IPC layer (CBS binary protocol, XService). Uses asio2.
  - **`veQml/`** — QML bridge (`QuickNode`, `QuickRootNode`). Own DLL export: `VE_QML_LIBRARY` / `VE_QML_API`.
  - **`veExample/`** — Test executable.

### CMake Layout

- **Root `CMakeLists.txt`** — Project setup, deps only (no Qt). Delegates to `cpp/qt/`.
- **`cpp/qt/CMakeLists.txt`** — Finds Qt, sets `AUTOMOC/AUTORCC/AUTOUIC`, manages module options and build order.
- Each module specifies its own Qt component requirements (e.g., `Core;Network` for imol, `Core;Widgets;Network` for veTerminal).

### Data Navigation API

Tree nodes use short method names: `p(level)` parent, `c(name|index)` child, `b(offset)` sibling, `r("a.b.c")` relative path, `fullName()` full path. Global accessor: `ve::d("path.to.node")`, macro: `VE_D(path)`.

### Serialization

`ve::Data` supports JSON, XML, Binary, and QVariant import/export.

## Code Conventions

- Pure C++ headers in `core/include/ve/core/`. Qt adapter headers in `cpp/qt/{module}/include/`. Implementation in `{module}/src/`.
- The `imol` namespace is the legacy implementation layer; `ve` namespace wraps it as the modern API surface.
- CMake utilities in `cmake/ve_common.cmake`: `ve_qt_module_vars()`, `ve_target_link_qt_components()`, `ve_find_sources()`.
- Platform-specific code is isolated under `core/platform/{win,linux,unsupported}/`.
- Internal planning docs in `docs/internal/plan/`. Historical analysis in `docs/internal/history/`.
- Local CMake overrides via `cmake/_*.cmake` files (gitignored).
