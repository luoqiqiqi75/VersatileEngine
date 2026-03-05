# Linux 堆栈跟踪配置指南

> 对应源码: `core/platform/linux/rescue.cpp`
> 前置阅读: [linux-rescue.md](linux-rescue.md)

---

## 配置文件说明

### VE 仓库中的配置方式

VE 使用 `cmake/_local.cmake`（本地配置，在 `.gitignore` 中）来管理开发者个人设置。参见 `cmake/_local.cmake.example`。

符号导出和调试信息的配置**通常由使用 VE 的上层项目管理**，而非 VE 自身。以下是在上层项目中的配置模式：

### 项目级 CMakeLists.txt 配置示例

```cmake
# 定义选项
option(ENABLE_STACK_TRACE "Enable stack trace symbol export for Linux" OFF)
option(FORCE_DEBUG_INFO "Force -g flag for crash tracking source location" OFF)

# 符号导出（-rdynamic）
if(UNIX AND NOT ANDROID AND NOT APPLE)
    if(ENABLE_STACK_TRACE)
        add_link_options(-rdynamic)
        message(STATUS "Linux: Stack trace enabled - added -rdynamic link flag")
    endif()
endif()

# 调试信息（-g）
if(FORCE_DEBUG_INFO)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")
    message(STATUS "Force debug info enabled: added -g flag")
endif()
```

### 本地配置（不提交到 Git）

```cmake
# cmake/_local.cmake（VE 仓库）或项目级本地配置
# 开发环境
set(ENABLE_STACK_TRACE ON CACHE BOOL "" FORCE)
set(FORCE_DEBUG_INFO ON CACHE BOOL "" FORCE)

# 生产环境
# set(ENABLE_STACK_TRACE OFF CACHE BOOL "" FORCE)
# set(FORCE_DEBUG_INFO OFF CACHE BOOL "" FORCE)
```

---

## 两个关键开关

### 1. `FORCE_DEBUG_INFO` — 调试信息开关

| 配置 | 作用 | 效果 |
|------|------|------|
| **ON** | 添加 `-g` 标志 | 显示**文件名:行号** |
| **OFF** | 不添加 `-g` | 只显示符号名 |

**示例输出对比**:
```
ON:  myFunction() at main.cpp:123
OFF: myFunction()+0x42
```

### 2. `ENABLE_STACK_TRACE` — 符号导出开关

| 配置 | 作用 | 效果 |
|------|------|------|
| **ON** | 添加 `-rdynamic` | 运行时可解析所有符号 |
| **OFF** | 不添加 | 减小体积 10-20% |

**影响**: 二进制文件大小

---

## 推荐配置方案

### 方案1: 开发环境（最详细）

```cmake
# 本地配置
set(ENABLE_STACK_TRACE ON CACHE BOOL "" FORCE)
set(FORCE_DEBUG_INFO ON CACHE BOOL "" FORCE)
```

```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
```

**效果**:
- ✅ 显示完整文件名和行号
- ✅ Lambda 函数清晰可见
- ✅ 可以在线 dump 堆栈
- ❌ 二进制文件较大

**适用**: 日常开发、调试

---

### 方案2: 测试环境（优化+调试）

```cmake
set(ENABLE_STACK_TRACE ON CACHE BOOL "" FORCE)
set(FORCE_DEBUG_INFO ON CACHE BOOL "" FORCE)
```

```bash
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo ..
```

**效果**:
- ✅ 代码优化（性能好）
- ✅ 保留调试信息（可诊断）
- ✅ 可以现场获取堆栈
- ⚠️ 二进制文件中等大小

**适用**: 预发布测试、现场诊断

---

### 方案3: 生产环境-可诊断（推荐）

```cmake
set(ENABLE_STACK_TRACE ON CACHE BOOL "" FORCE)
set(FORCE_DEBUG_INFO ON CACHE BOOL "" FORCE)
```

```bash
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-g -O2" ..
```

**效果**:
- ✅ 性能最优（-O2 优化）
- ✅ 崩溃时有完整信息
- ✅ 可以现场获取堆栈
- ⚠️ 二进制文件较大

**适用**: 需要现场诊断的生产环境

---

### 方案4: 生产环境-最小体积

```cmake
set(ENABLE_STACK_TRACE OFF CACHE BOOL "" FORCE)
set(FORCE_DEBUG_INFO OFF CACHE BOOL "" FORCE)
```

```bash
cmake -DCMAKE_BUILD_TYPE=Release ..
```

**效果**:
- ✅ 二进制文件最小
- ✅ 不暴露符号（安全）
- ❌ 崩溃时只有地址
- ❌ 需要 GDB 离线分析

**适用**: 最终发布版本

---

## 配置方式

### 方式1: 本地配置文件（推荐）

在上层项目中编辑本地 cmake 配置文件（如 `cmake/_local.cmake` 或 `cmake/_custom.cmake`）：
```cmake
set(FORCE_DEBUG_INFO ON CACHE BOOL "" FORCE)
set(ENABLE_STACK_TRACE ON CACHE BOOL "" FORCE)
```

**优点**: 持久化，每次构建都生效

### 方式2: CMake 命令行

```bash
cmake -DFORCE_DEBUG_INFO=ON -DENABLE_STACK_TRACE=ON ..
```

**优点**: 临时测试，不影响本地配置

### 方式3: 环境变量

```bash
export FORCE_DEBUG_INFO=ON
export ENABLE_STACK_TRACE=ON
cmake ..
```

**优点**: 适合 CI/CD 流程

---

## 完整对比表

| 配置组合 | 文件名行号 | Lambda 显示 | 符号解析 | 体积 | 适用场景 |
|---------|-----------|-----------|---------|------|---------|
| 两个都 ON | ✅ | ✅ | ✅ | 大 | 开发/测试 |
| DEBUG_INFO=ON, TRACE=OFF | ✅ | ✅ | ❌ | 中 | 离线分析 |
| DEBUG_INFO=OFF, TRACE=ON | ❌ | ❌ | ✅ | 中 | 仅符号名 |
| 两个都 OFF | ❌ | ❌ | ❌ | 小 | 最终发布 |

---

## 常见问题

### Q: 我修改了配置，为什么没生效？
**A**: 需要重新运行 cmake 配置：
```bash
rm -rf build
mkdir build && cd build
cmake ..
```

### Q: 团队成员的配置会冲突吗？
**A**: 不会。本地配置文件（如 `_local.cmake`、`_custom.cmake`）在 `.gitignore` 中，每个人的配置独立。

### Q: CI/CD 如何配置？
**A**: 使用命令行参数：
```bash
# 测试构建
cmake -DFORCE_DEBUG_INFO=ON -DENABLE_STACK_TRACE=ON ..

# 发布构建
cmake -DFORCE_DEBUG_INFO=OFF -DENABLE_STACK_TRACE=OFF ..
```

### Q: 我可以只在 Debug 模式启用吗？
**A**: 可以，在本地配置中：
```cmake
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(FORCE_DEBUG_INFO ON CACHE BOOL "" FORCE)
    set(ENABLE_STACK_TRACE ON CACHE BOOL "" FORCE)
else()
    set(FORCE_DEBUG_INFO OFF CACHE BOOL "" FORCE)
    set(ENABLE_STACK_TRACE OFF CACHE BOOL "" FORCE)
endif()
```

---

## 相关文档

- [linux-rescue.md](linux-rescue.md) — 技术实现详解
- [linux-quick-start.md](linux-quick-start.md) — 快速使用指南
- [platform-comparison.md](platform-comparison.md) — Windows vs Linux 对比
