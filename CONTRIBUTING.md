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
- Qt 5.12+ or 6.x (optional, only for `qt/` modules)
- FastDDS (optional, only for `ros/` DDS adapter)

> All other dependencies (spdlog, asio2, fmt, yaml-cpp, pugixml, etc.) are bundled in `deps/`.

### Building

```bash
# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build build --config Debug

# Build & run core tests only (no Qt / DDS needed)
cmake -B build_test -DVE_BUILD_TEST=ON -DVE_BUILD_QT=OFF -DVE_BUILD_DDS=OFF -DVE_BUILD_RTT=OFF
cmake --build build_test --target ve_test --config Debug
```

### Project Layout

- `ve/` - VE framework (pure C++17 + JS): data tree, command system, service layer, module lifecycle
- `qt/` - VE + Qt ecosystem (optional)
- `rtt/` - VE + RTT (pure C++)
- `ros/` - VE + ROS/DDS (optional, needs FastDDS)
- `deps/` - Bundled third-party dependencies
- `cmake/` - CMake utility scripts
- `docs/` - Documentation

## Code Conventions

> **短**。符合规范的前提下，一行能写完的绝不两行。

### Naming

| 元素 | 规则 | 示例 |
|---|---|---|
| **Namespaces** | 全小写，短名 | `ve`, `ve::impl`, `ve::basic` |
| **Classes / Structs** | PascalCase | `Node`, `SchemaField`, `SmallVector` |
| **Methods / Functions** | camelCase | `childCount()`, `setShadow()`, `isValidName()` |
| **Public parameters** | 全称 camelCase | `auto_delete`, `auto_fill` |
| **Private members** | 简短 | `_p`, `cnt`, `mtx`, `ch` |
| **Impl local variables** | 缩写 | `v`, `r`, `gs`, `nm`, `sv`, `seg` |
| **Template params** | 单大写字母或 PascalCase | `T`, `RetT`, `DerivedT` |
| **Enums** | 类型名 PascalCase，值 UPPER_SNAKE | `enum State { NONE, INIT, READY }` |
| **Macros** | `VE_` 前缀 + UPPER_SNAKE | `VE_API`, `VE_DECLARE_PRIVATE` |
| **Type aliases** | PascalCase | `Dict<V>`, `Strings`, `Ints` |

公共接口名称必须**清晰自解释**，不需要注释就能看懂。
内部实现变量**越短越好**，不推荐 Java 风格长名字。

### Formatting

- 缩进：4 空格，无 tab
- 行宽：不强制限制，合理即可
- 单行函数：getter/setter 等 trivial 函数写在一行
- `for`/`while`/`do` 循环体**必须**使用花括号，即使只有一行
- `if`/`else`：单行可以不加花括号，但多行必须加

```cpp
// good - single-line trivial functions
Node* Node::parent() const { return _p->parent; }
Node* Node::prev() const { return sibling(-1); }

// good - short early return
if (!child) return false;

// good - loops always use braces
for (auto& kv : _p->ch) {
    for (auto* n : kv.value) {
        out.push_back(n);
    }
}
```

### Brace Style

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

**函数定义**（.cpp）：`{` 换行（多行函数体），单行允许同行

**控制语句**（if / for / while / switch）：`{` **不换行**

```cpp
if (!_p->connections.has(signal)) {
    return;
}

switch (s) {
    case INIT: os << "INIT"; break;
    case READY: os << "READY"; break;
}
```

**Lambda / Namespace**：`{` 不换行

```cpp
observer->connect(OBJECT_DELETED, this, [=] { disconnect(observer); });

namespace ve {
// ...
} // namespace ve
```

### Comments

- **少写**。好的命名 > 注释
- 文件头：`// file.h — brief description`
- 段落分隔：`// --- section ---` 或 `// ====` 分隔线
- 行内注释：只在不明显处简短标注
- 禁止：注释掉的代码

```cpp
// good
Dict<SmallVector<Node*, 1>> ch;   // name → [Node*]

// bad — 注释比代码还长
// This method returns the parent node of the current node.
// If there is no parent, it returns nullptr.
Node* Node::parent() const { return _p->parent; }
```

### File Organization

- Public headers: `{component}/include/ve/{component}/`
- Implementation: `{component}/src/`
- Platform-specific: `ve/platform/{win,linux,unsupported}/`
- Qt headers: `qt/include/ve/qt/`

### Header Files (.h)

- `#pragma once`
- 最少 include，能前置声明就前置声明
- 公共 API 按功能分组，用 `// --- group ---` 标注
- 声明对齐：返回类型左对齐
- 不在头文件里写实现（模板和 inline 除外）

```cpp
// --- child (by name, no # no /) ---
Node*         child(const std::string& name) const;
Node*         child(const std::string& name, int index) const;
int           childCount() const;
Vector<Node*> children() const;
```

### Implementation Files (.cpp)

- 用 `// ====` 分隔线标注大段落（与 .h 的 API 分组对应）
- 内部 helper 用 `static` 函数，`_` 前缀
- Private 结构体成员用缩写

```cpp
struct Node::Private
{
    std::string name;
    Node* parent = nullptr;
    Dict<SmallVector<Node*, 1>> ch;   // children
    int cnt = 0;                      // total child count
    mutable std::recursive_mutex mtx;
};
```

### Containers

| 用途 | 容器 |
|---|---|
| 通用数组 | `Vector<T>` |
| 少量元素 (SBO) | `SmallVector<T, N>` |
| 有序键值 | `Dict<V>` / `OrderedHashMap<K,V>` |
| 无序键值 | `Hash<V>` / `UnorderedHashMap<K,V>` |
| 固定大小 | `Array<T, N>` |

### Type Casting

- C-style cast 用于简单数值转换：`(int)v->size()`
- `static_cast` 用于指针/类层次转换
- 禁止 `reinterpret_cast`（除非绝对必要）
- 禁止 `const_cast`（除非 API 边界需要）

### Thread Safety

- Private 中持有 `mutable std::recursive_mutex mtx`
- 方法内 `using Lock = std::lock_guard<std::recursive_mutex>; Lock lk(_p->mtx);`
- 锁粒度尽量小

### PIMPL

| 宏 | 语义 | 说明 |
|---|---|---|
| `VE_DECLARE_UNIQUE_PRIVATE` | 值语义 | 不可复制，推荐新代码使用 |
| `VE_DECLARE_SHARED_PRIVATE` | 共享语义 | 引用计数 |
| `VE_DECLARE_PRIVATE` | 原始指针 | 遗留，避免新代码使用 |

### Coding Style

- **C++17** standard
- Use `#pragma once` for header guards
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
