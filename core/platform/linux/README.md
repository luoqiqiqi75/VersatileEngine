# Linux平台堆栈跟踪实现说明

## 功能概述

本实现提供了与Windows版本相同的在线和崩溃时堆栈打印功能，用于Linux平台。

## 主要特性

### 1. 崩溃处理（Crash Handler）
- 使用信号处理机制捕获以下崩溃信号：
  - `SIGSEGV`: 段错误（访问无效内存）
  - `SIGABRT`: abort()调用
  - `SIGFPE`: 浮点异常
  - `SIGILL`: 非法指令
  - `SIGBUS`: 总线错误

- 崩溃时自动打印详细的堆栈信息，包括：
  - 信号类型和原因
  - 崩溃地址和信号代码
  - 完整的调用堆栈

### 2. 在线堆栈打印（Live Stack Dump）
- 通过连接`dump`信号可以在运行时打印当前线程的堆栈
- 显示进程中所有线程的列表

### 3. 符号解析（增强版）
- 使用`backtrace()`获取堆栈帧
- 使用`dladdr()`获取详细的符号信息
- 使用`abi::__cxa_demangle()`解码C++符号名称
- **使用`addr2line`获取源文件名和行号**（需要编译时带`-g`）
- 批量处理优化，提升性能
- 完整显示：`函数名 at 文件名:行号 [地址]`
- 支持lambda、内联函数等复杂场景

## 实现细节

### Linux与Windows的差异

1. **线程堆栈跟踪**
   - Windows: 可以使用`SuspendThread`和`GetThreadContext`获取其他线程的堆栈
   - Linux: 在正常模式下只能获取当前线程的堆栈
   - 解决方案: 在信号处理中（崩溃时）可以获取崩溃线程的堆栈

2. **符号信息获取**
   - Windows: 使用DbgHelp API (`SymFromAddr`, `SymGetLineFromAddr64`)
   - Linux: 使用`backtrace_symbols`和`dladdr`

3. **线程枚举**
   - Windows: 使用`CreateToolhelp32Snapshot`和`Thread32First/Next`
   - Linux: 读取`/proc/self/task/`目录

## 符号导出配置

符号导出配置已统一管理，通过两个开关控制。

### 配置开关说明

#### 1. `ENABLE_STACK_TRACE` - 符号导出开关
- **定义位置**: `cmake/platform.cmake` (自动定义)
- **设置位置**: `cmake/_custom.cmake` (本地配置，gitignore)
- **作用**: 添加 `-rdynamic` 标志，导出符号表
- **默认值**: 用户自定义（建议开发环境ON，生产环境OFF）

#### 2. `FORCE_DEBUG_INFO` - 调试信息开关
- **定义位置**: `cmake/compile.cmake` (默认OFF)
- **设置位置**: `cmake/_custom.cmake` (本地配置，gitignore)
- **作用**: 添加 `-g` 标志，包含文件名和行号
- **默认值**: OFF（可在本地设置为ON）

### 本地配置示例

在 `cmake/_custom.cmake` 中配置：

```cmake
# 开发环境推荐配置
set(FORCE_DEBUG_INFO ON)              # 显示文件名和行号
option(ENABLE_STACK_TRACE ON)         # 导出符号表

# 生产环境配置
# set(FORCE_DEBUG_INFO OFF)           # 不包含调试信息
# set(ENABLE_STACK_TRACE OFF)         # 不导出符号表
```

### 命令行配置（临时覆盖）

```bash
# 临时启用
cmake -DFORCE_DEBUG_INFO=ON -DENABLE_STACK_TRACE=ON ..

# 临时禁用
cmake -DFORCE_DEBUG_INFO=OFF -DENABLE_STACK_TRACE=OFF ..
```

### 实现原理

`cmake/platform.cmake` 中的配置会自动应用到所有Linux目标：

```cmake
if(UNIX AND NOT ANDROID AND NOT APPLE)
    if(ENABLE_STACK_TRACE)
        add_link_options(-rdynamic)
    endif()
endif()
```

### `-rdynamic`标志的作用
- 将所有符号（不仅是导出的符号）添加到动态符号表中
- 使得`backtrace_symbols()`和`dladdr()`能够解析函数名
- 对于可执行文件和共享库都很重要
- **注意**：会增加约10-20%的二进制文件大小

### 构建配置建议

| 场景 | ENABLE_STACK_TRACE | 说明 |
|------|-------------------|------|
| **开发/调试** | ON (默认) | 完整的堆栈信息，便于问题定位 |
| **测试/预发布** | ON | 便于捕获和诊断问题 |
| **生产/Release** | OFF | 减小二进制体积，提升安全性 |

### 单独的库配置

对于需要使用 `dladdr()` 的库（如 ve_core），会自动链接 `libdl`：

```cmake
# deps/ve/core/CMakeLists.txt
if (UNIX AND NOT APPLE)
    target_link_libraries(${VE_CORE_LIBRARY} PRIVATE dl)
endif()
```

## 使用方法

### 1. 设置崩溃处理器
```cpp
#include "rescue.h"

// 初始化，传入Data对象
setupRescue(dataObject);
```

### 2. 在线打印堆栈
```cpp
// 触发dump信号
ve::d(dataObject, "dump")->set(nullptr, true);

// 结果会输出到：
// 1. qCritical()日志
// 2. dataObject的"result"字段
```

### 3. 导出数据
```cpp
// 触发export信号
ve::d(dataObject, "export")->set(nullptr, true);
// 会将数据导出到root.bin文件
```

## 编译要求

### 必需的系统库
- `libdl`: 用于动态加载和符号解析
- `libpthread`: 线程支持（通常已包含）

### 编译器要求
- 支持C++11或更高版本
- GCC 4.8+ 或 Clang 3.4+

### 调试符号（重要！）

**要显示文件名和行号，必须使用 `-g` 编译标志**

#### Debug构建（推荐开发时）
```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
# 自动包含 -g 标志
```

#### Release构建（保留调试信息）
```bash
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-g -O2" ..
# -g: 包含调试信息（文件名、行号）
# -O2: 代码优化
```

#### 输出对比

**不带 -g（只有符号名）**：
```
#0 MovaX(myFunction()+0x42) [0x55a123456789]
#1 MovaX(???) [0x55a12345678a]  ← lambda看不出来
```

**带 -g（完整信息）**：
```
#0 myFunction() at /path/to/file.cpp:123 [0x55a123456789]
#1 MyClass::lambda::operator()() at /path/to/file.cpp:456 [0x55a12345678a]  ← lambda清晰可见
```

## 限制和注意事项

1. **线程堆栈跟踪限制**
   - 正常模式下只能打印当前线程的堆栈
   - 其他线程只会列出ID，无法打印堆栈
   - 崩溃时可以打印崩溃线程的堆栈

2. **符号信息精度**
   - **文件名和行号需要 `-g` 编译选项**（必需！）
   - 内联函数可能不会出现在堆栈中
   - 优化代码的堆栈可能不完整
   - lambda函数在有 `-g` 的情况下会显示定义位置

3. **性能影响**
   - `-rdynamic`会增加可执行文件大小（10-20%）
   - `-g` 会增加调试信息大小（但不影响运行时性能）
   - 堆栈解析使用`addr2line`，批量处理优化后性能可接受
   - 建议开发环境启用，生产环境可选

4. **依赖要求**
   - 需要系统安装 `binutils`（包含`addr2line`工具）
   - 大多数Linux发行版默认已安装
   - 如果没有`addr2line`，会降级到只显示符号名

## 改进建议

如果需要更强大的功能，可以考虑：

1. **使用libunwind**
   - 提供更可靠的堆栈遍历
   - 支持跨线程堆栈跟踪
   
2. **使用libbacktrace**
   - 提供更详细的调试信息
   - 可以读取DWARF调试信息

3. **使用GDB脚本**
   - 在崩溃时自动调用GDB
   - 可以获取所有线程的完整信息

## 测试

### 测试崩溃处理
```cpp
// 触发段错误
int* p = nullptr;
*p = 42;

// 触发abort
abort();

// 触发浮点异常
int x = 1 / 0;
```

### 测试在线堆栈打印
```cpp
void testStackTrace() {
    ve::d(dataObject, "dump")->set(nullptr, true);
}
```

## 相关文件

- `rescue.h`: 接口声明
- `rescue.cpp`: Linux实现
- `CMakeLists.txt`: 构建配置（包含符号导出设置）
