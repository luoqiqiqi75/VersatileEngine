# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/).

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

[1.0.0]: https://github.com/user/VersatileEngine/releases/tag/v1.0.0
