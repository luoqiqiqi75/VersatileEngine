# Linux堆栈跟踪配置指南

## 📁 配置文件职责

### 版本控制文件（提交到Git）

#### `cmake/compile.cmake`
```cmake
# 定义选项（默认值）
option(FORCE_DEBUG_INFO "Generate crash tracking" OFF)

# 实现：Linux添加-g标志
if (FORCE_DEBUG_INFO)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")
endif()
```
**职责**: 定义编译选项的实现逻辑，所有团队成员共享

#### `cmake/platform.cmake`
```cmake
# 定义堆栈跟踪辅助函数
function(add_stack_trace_to_executable TARGET_NAME)
    target_link_options(${TARGET_NAME} PRIVATE -rdynamic)
endfunction()
```
**职责**: 定义平台特定的实现函数

### 本地配置文件（不提交，在.gitignore中）

#### `cmake/_custom.cmake`
```cmake
# 用户本地设置（示例）
set(FORCE_DEBUG_INFO ON)              # 开启调试信息
option(ENABLE_STACK_TRACE ON)         # 开启堆栈跟踪
```
**职责**: 用户自己的本地配置，每个开发者可以不同

---

## ⚙️ 两个关键开关

### 1. `FORCE_DEBUG_INFO` - 调试信息开关

| 配置 | 作用 | 效果 |
|------|------|------|
| **ON** | 添加 `-g` 标志 | 显示**文件名:行号** |
| **OFF** | 不添加 `-g` | 只显示符号名 |

**示例输出对比**:
```
ON:  myFunction() at main.cpp:123
OFF: myFunction()+0x42
```

### 2. `ENABLE_STACK_TRACE` - 符号导出开关

| 配置 | 作用 | 效果 |
|------|------|------|
| **ON** | 添加 `-rdynamic` | 运行时可解析所有符号 |
| **OFF** | 不添加 | 减小体积10-20% |

**影响**: 二进制文件大小

---

## 🎯 推荐配置方案

### 方案1: 开发环境（最详细）

```cmake
# cmake/_custom.cmake
set(FORCE_DEBUG_INFO ON)
option(ENABLE_STACK_TRACE ON)
```

```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
```

**效果**:
- ✅ 显示完整文件名和行号
- ✅ Lambda函数清晰可见
- ✅ 可以在线dump堆栈
- ❌ 二进制文件较大

**适用**: 日常开发、调试

---

### 方案2: 测试环境（优化+调试）

```cmake
# cmake/_custom.cmake
set(FORCE_DEBUG_INFO ON)
option(ENABLE_STACK_TRACE ON)
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
# cmake/_custom.cmake
set(FORCE_DEBUG_INFO ON)      # 保留调试信息
set(ENABLE_STACK_TRACE ON)    # 可以现场诊断
```

```bash
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-g -O2" ..
```

**效果**:
- ✅ 性能最优（-O2优化）
- ✅ 崩溃时有完整信息
- ✅ 可以现场获取堆栈
- ⚠️ 二进制文件较大

**适用**: 需要现场诊断的生产环境

---

### 方案4: 生产环境-最小体积

```cmake
# cmake/_custom.cmake
set(FORCE_DEBUG_INFO OFF)
set(ENABLE_STACK_TRACE OFF)
```

```bash
cmake -DCMAKE_BUILD_TYPE=Release ..
```

**效果**:
- ✅ 二进制文件最小
- ✅ 不暴露符号（安全）
- ❌ 崩溃时只有地址
- ❌ 需要GDB离线分析

**适用**: 最终发布版本

---

## 🔧 配置方式

### 方式1: 本地配置文件（推荐）

编辑 `cmake/_custom.cmake`:
```cmake
set(FORCE_DEBUG_INFO ON)
option(ENABLE_STACK_TRACE ON)
```

**优点**: 持久化，每次构建都生效

### 方式2: CMake命令行

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

**优点**: 适合CI/CD流程

---

## 📊 完整对比表

| 配置组合 | 文件名行号 | Lambda显示 | 符号解析 | 体积 | 适用场景 |
|---------|-----------|-----------|---------|------|---------|
| 两个都ON | ✅ | ✅ | ✅ | 大 | 开发/测试 |
| DEBUG_INFO=ON, TRACE=OFF | ✅ | ✅ | ❌ | 中 | 离线分析 |
| DEBUG_INFO=OFF, TRACE=ON | ❌ | ❌ | ✅ | 中 | 仅符号名 |
| 两个都OFF | ❌ | ❌ | ❌ | 小 | 最终发布 |

---

## 🚀 快速启动

### 我想要完整的堆栈信息（开发用）
```cmake
# cmake/_custom.cmake
set(FORCE_DEBUG_INFO ON)
option(ENABLE_STACK_TRACE ON)
```

### 我想要最小的发布体积
```cmake
# cmake/_custom.cmake
set(FORCE_DEBUG_INFO OFF)
set(ENABLE_STACK_TRACE OFF)
```

### 我需要在生产环境诊断问题
```cmake
# cmake/_custom.cmake
set(FORCE_DEBUG_INFO ON)   # 保留完整信息
set(ENABLE_STACK_TRACE ON) # 可以现场dump
```

---

## ❓ 常见问题

### Q: 我修改了_custom.cmake，为什么没生效？
**A**: 需要重新运行cmake配置：
```bash
rm -rf build
mkdir build && cd build
cmake ..
```

### Q: 团队成员的配置会冲突吗？
**A**: 不会。`_custom.cmake` 在 `.gitignore` 中，每个人的配置独立。

### Q: CI/CD如何配置？
**A**: 使用命令行参数：
```bash
# 测试构建
cmake -DFORCE_DEBUG_INFO=ON -DENABLE_STACK_TRACE=ON ..

# 发布构建
cmake -DFORCE_DEBUG_INFO=OFF -DENABLE_STACK_TRACE=OFF ..
```

### Q: 我可以只在Debug模式启用吗？
**A**: 可以，在 `_custom.cmake` 中：
```cmake
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(FORCE_DEBUG_INFO ON)
    option(ENABLE_STACK_TRACE ON)
else()
    set(FORCE_DEBUG_INFO OFF)
    set(ENABLE_STACK_TRACE OFF)
endif()
```

---

## 📚 相关文档

- [README.md](./README.md) - 技术实现详解
- [QUICK_START.md](./QUICK_START.md) - 快速使用指南
- [../PLATFORM_COMPARISON.md](../PLATFORM_COMPARISON.md) - Windows vs Linux对比
