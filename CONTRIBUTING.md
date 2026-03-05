# Contributing

Thank you for your interest in VersatileEngine! We welcome contributions of all kinds.

## How to Contribute

### Reporting Bugs

1. Search existing [Issues](../../issues) to check if the problem has already been reported
2. If not, create a new Issue including:
   - A clear title and description
   - Steps to reproduce
   - Expected vs. actual behavior
   - Environment info (OS, compiler version, Qt version, CMake version)
   - A minimal reproduction if possible

### Suggesting Features

1. Create an Issue labeled `feature request`
2. Describe the desired feature, use case, and expected outcome
3. If possible, include design ideas or reference implementations

### Submitting Code

1. **Fork** this repository
2. Create your feature branch: `git checkout -b feature/your-feature-name`
3. Make sure the code compiles
4. Commit your changes: `git commit -m "feat: describe your change"`
5. Push to your fork: `git push origin feature/your-feature-name`
6. Open a **Pull Request**

## Development Setup

### Prerequisites

- C++17 compiler (MSVC 2019+, GCC 7+, Clang 5+)
- CMake 3.15+
- Qt 5.12+ or Qt 6.x
- spdlog

### Building

```bash
# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build build --config Debug
```

### Project Layout

- `core/` — Core library: data tree and module system
- `service/` — Service layer: CBS binary protocol and command server
- `main/` — Example application
- `cmake/` — CMake utility scripts
- `docs/` — Documentation

## Code Conventions

> Style follows **Qt conventions** — camelCase methods, Allman braces, Qt-compatible naming.

### Naming

| 元素 | 规则 | 示例 |
|---|---|---|
| **Namespaces** | 全小写，短名 | `ve`, `ve::basic`, `imol` |
| **Classes / Structs** | PascalCase | `Module`, `DataManager`, `NetObject` |
| **Methods / Functions** | camelCase | `fullName()`, `childCount()`, `setParent()` |
| **Member variables** | `m_` 前缀（普通成员）或 `_p`（Pimpl 指针） | `m_name`, `m_running`, `_p` |
| **Local variables** | camelCase 或 snake_case 均可 | `newVal`, `thread_count` |
| **Template params** | 单大写字母或 PascalCase | `T`, `RetT`, `DerivedT` |
| **Enums** | 类型名 PascalCase，值 UPPER_SNAKE | `enum State { NONE, INIT, READY }` |
| **Macros** | `VE_` 前缀 + UPPER_SNAKE | `VE_API`, `VE_REGISTER_MODULE` |
| **Constants** | camelCase 或 UPPER_SNAKE 均可 | `constexpr double eps = 0.000001;` |
| **Typedefs / using** | PascalCase，`T` 后缀 | `using FunctionT = ...;`, `using ActionT = ...;` |

### Brace Style — Allman (Qt style)

**Class / struct 声明**：`{` 换行

```cpp
class VE_API Module : public Object
{
    VE_DECLARE_PRIVATE

public:
    Module();
    ~Module();

    State state() const;
};
```

**函数定义**（.cpp 文件）：`{` 换行

```cpp
Module::State Module::state() const
{
    return _p->s;
}
```

**短内联函数**（一行可完成）：允许同行

```cpp
bool isNull() const { return !_p; }
State state() const { return _p->s; }
```

**控制语句**（if / for / while / switch）：`{` **不换行**（跟随 Qt 惯例）

```cpp
if (!_p->connections.has(signal)) {
    return;
}

for (auto& kv : hashmap) {
    kv.second();
}

switch (s) {
    case INIT: os << "INIT"; break;
    case READY: os << "READY"; break;
}
```

**Lambda**：`{` 不换行

```cpp
observer->connect(OBJECT_DELETED, this, [=] {
    disconnect(observer);
});
```

**Namespace**：`{` 不换行，闭合处标注名称

```cpp
namespace ve {

// ...

} // namespace ve
```

### File Organization

- Public headers: `{component}/include/ve/{component}/`
- Implementation: `{component}/src/`
- Platform-specific: `core/platform/{win,linux,unsupported}/`
- Qt adapter headers: `cpp/qt/{module}/include/`

### Header Files

```cpp
// ----------------------------------------------------------------------------
// filename.h — Brief description
// ----------------------------------------------------------------------------
// Copyright (c) 2023-present Thilo and VersatileEngine contributors.
// Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
// See LICENSE file in the project root for full license information.
// ----------------------------------------------------------------------------

#pragma once

#include "dependency.h"

namespace ve {

class MyClass
{
    // ...
};

} // namespace ve
```

### Coding Style

- **C++17** standard
- Prefer **Pimpl** encapsulation：`VE_DECLARE_PRIVATE` / `VE_DECLARE_UNIQUE_PRIVATE`
- Use `#pragma once` for header guards
- Include copyright header in all source files
- Prefer smart pointers for ownership；raw pointers for non-owning references
- Avoid `using namespace` in headers
- Use `const` and `override` wherever applicable

### Commit Message Format

We recommend [Conventional Commits](https://www.conventionalcommits.org/):

```
<type>(<scope>): <description>

[optional body]

[optional footer]
```

**Types**:
- `feat` — New feature
- `fix` — Bug fix
- `docs` — Documentation update
- `refactor` — Code refactoring (no behavior change)
- `perf` — Performance improvement
- `test` — Test-related changes
- `chore` — Build/toolchain changes

**Scopes**: `core`, `service`, `cmake`, `docs`, etc.

**Examples**:
```
feat(core): add pure C++ ve::Node data node implementation
fix(service): fix CBS reconnection logic after disconnect
docs: update README architecture section
```

## Pull Request Guidelines

- Each PR should focus on a single concern
- Ensure the code compiles successfully
- Update relevant documentation (e.g. README or ARCHITECTURE.md for API changes)
- Explain the purpose and impact in the PR description
- Reference related Issues: `Fixes #123`

## License

All contributions to this project are released under the [LGPLv3 License](LICENSE). By submitting code, you agree to these terms.

## Contact

If you have questions:

- Open a GitHub Issue
