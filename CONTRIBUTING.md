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

### Naming

- **Namespaces**: `ve` (public API), `imol` (internal implementation layer)
- **Classes**: PascalCase — `ModuleObject`, `Factory`
- **Methods**: camelCase — `fullName()`, `childCount()`
- **Member variables**: `m_` prefix (e.g. `m_name`, `m_var`) or `_p` (Private pointer)
- **Macros**: `VE_` prefix (e.g. `VE_API`, `VE_REGISTER_MODULE`)

### File Organization

- Public headers go in `{component}/include/ve/{component}/`
- Implementation files go in `{component}/src/`
- Platform-specific code is isolated under `core/platform/{win,linux,unsupported}/`

### Coding Style

- C++17 standard
- Prefer smart pointers for memory management
- Use `VE_DECLARE_PRIVATE` / `VE_DECLARE_UNIQUE_PRIVATE` for Pimpl encapsulation
- Use `#pragma once` for header guards
- Include copyright header in source files

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
