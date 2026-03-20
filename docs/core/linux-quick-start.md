# Linux 堆栈跟踪 - 快速配置指南

> 对应源码: `core/platform/linux/rescue.cpp`
> 详细说明: [linux-rescue.md](linux-rescue.md) | 完整配置: [linux-config-guide.md](linux-config-guide.md)

## 快速配置

在使用 VE 的项目中，添加以下 CMake 选项（或在本地 cmake 配置文件中设置）：

```cmake
# 开发环境（完整堆栈跟踪 + 文件名行号）
set(FORCE_DEBUG_INFO ON CACHE BOOL "" FORCE)   # 添加 -g，显示文件名和行号
set(ENABLE_STACK_TRACE ON CACHE BOOL "" FORCE)  # 添加 -rdynamic，导出符号

# 生产环境（最小体积）
# set(FORCE_DEBUG_INFO OFF CACHE BOOL "" FORCE)
# set(ENABLE_STACK_TRACE OFF CACHE BOOL "" FORCE)
```

> **注意**: 这些选项通常在上层项目的 CMake 中配置，而非 VE 仓库本身。
> VE 仓库使用 `cmake/_local.cmake`（参见 `cmake/_local.cmake.example`）进行本地配置。

## 不同环境的推荐配置

### 开发环境 (Development) ⭐ 推荐
```bash
cmake -DCMAKE_BUILD_TYPE=Debug -DFORCE_DEBUG_INFO=ON -DENABLE_STACK_TRACE=ON ..
make
# 结果：完整堆栈 + 文件名:行号
```

### 测试环境 (Testing/Staging)
```bash
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DFORCE_DEBUG_INFO=ON -DENABLE_STACK_TRACE=ON ..
make
# 结果：优化代码 + 调试信息
```

### 生产环境 (Production/Release)
```bash
cmake -DCMAKE_BUILD_TYPE=Release ..
make
# 结果：最小体积，无调试信息
```

## 效果对比

### ENABLE_STACK_TRACE = ON
```
✅ 优点：
  - 崩溃时自动打印详细堆栈
  - 可以在线打印堆栈（dump 命令）
  - 符号名称完整解析
  - 便于快速定位问题

❌ 缺点：
  - 二进制文件增大 10-20%
  - 符号表暴露内部函数名
  - 轻微的性能开销
```

### ENABLE_STACK_TRACE = OFF
```
✅ 优点：
  - 二进制文件体积最小
  - 符号表不暴露（安全性更好）
  - 无额外性能开销

❌ 缺点：
  - 堆栈跟踪只显示地址
  - 需要配合 GDB 或符号文件才能调试
```

## 使用示例

### 1. 初始化（自动设置崩溃处理）
```cpp
#include "rescue.h"

void init() {
    setupRescue(dataObject);
}
```

### 2. 在线打印当前线程堆栈
```cpp
// 触发 dump
dataObject->ensure("dump")->set(ve::Var(true));

// 输出位置：
// 1. qCritical() 日志
// 2. dataObject->result 字段

// 输出示例（带 -g 编译）：
// thread 12345 trace stack:
//   #0 PrintThreadStack(int) at rescue.cpp:237 [0x7f1234567890]
//   #1 MyClass::onButtonClick()::{lambda()#1}::operator()() at main.cpp:123 [0x5566778899aa]
//   #2 main at main.cpp:456 [0x5566778899bb]
```

### 3. 程序崩溃时自动打印
```cpp
// 任何崩溃都会自动打印堆栈（带完整文件名和行号）：
int* ptr = nullptr;
*ptr = 42;  // 段错误 -> 自动打印堆栈

// 输出示例：
// ==========================================================
// Caught signal 11 (SIGSEGV - Segmentation fault)
// Signal info: si_addr=0x0, si_code=1
// ==========================================================
//
// thread 12345 trace stack:
//   #0 crashFunction() at mycode.cpp:789 [0x5566778899cc]
//   #1 processData() at mycode.cpp:456 [0x5566778899dd]
//   #2 main at main.cpp:123 [0x5566778899ee]
```

## 高级配置

### 临时启用（CMake 命令行）
```bash
# 覆盖本地配置的设置
cmake -DFORCE_DEBUG_INFO=ON -DENABLE_STACK_TRACE=ON ..

# 或使用 CMAKE_BUILD_TYPE
cmake -DCMAKE_BUILD_TYPE=Debug ..  # 自动包含 -g
```

### 查看是否启用
```bash
# 构建时会显示（如果项目 CMakeLists.txt 中添加了 message 输出）：
# [STATUS] Linux: Stack trace enabled - added -rdynamic link flag
# [STATUS] Force debug info enabled: added -g flag for stack trace source location
```

### 验证二进制文件是否包含符号
```bash
# 查看符号表
nm -D ./your_app | grep -i "yourfunction"

# 查看文件大小
ls -lh ./your_app
```

## 常见问题

### Q: 为什么只显示地址或符号名，没有文件名和行号？
**A:** **必须启用 `-g` 编译标志！**

```bash
# 方法1：使用 Debug 构建类型（自动包含 -g）
cmake -DCMAKE_BUILD_TYPE=Debug ..

# 方法2：手动添加 -g
cmake -DCMAKE_CXX_FLAGS="-g" ..

# 方法3：通过项目选项
cmake -DFORCE_DEBUG_INFO=ON ..
```

### Q: lambda 函数显示为 ??? 怎么办？
**A:** 使用 `-g` 编译后，lambda 会显示定义位置：
```
// 不带 -g：
#1 myapp(???) [0x123456]

// 带 -g：
#1 MyClass::onButtonClick()::{lambda()#1}::operator()() at main.cpp:123 [0x123456]
```

### Q: Release 版本应该如何配置？
**A:**
- **选项1（推荐）**：不带 `-rdynamic` 和 `-g`，最小体积
- **选项2（便于诊断）**：带 `-rdynamic` 和 `-g`，可现场调试
- 根据需求选择，建议测试环境使用选项2，生产环境使用选项1

### Q: 关闭后崩溃了怎么办？
**A:**
1. 使用 `gdb` 加载 core dump 文件（需要符号文件）
2. 或者编译一个开启堆栈跟踪 + `-g` 的版本重现问题
3. 使用 `addr2line` 手动解析地址

### Q: 会影响性能吗？
**A:**
- `-rdynamic` 对运行时性能影响很小（< 1%）
- `-g` 不影响运行时性能，只增加文件体积
- 堆栈跟踪使用 `addr2line` 批量解析，性能开销可接受（崩溃时才执行）
- 正常运行完全不受影响

## 快速检查清单

- [ ] 确认项目 CMake 中配置了 `ENABLE_STACK_TRACE` 和 `FORCE_DEBUG_INFO`
- [ ] Debug/Dev 环境：两个都设置为 ON
- [ ] Release/Prod 环境：两个都设置为 OFF
- [ ] 重新运行 cmake 配置
- [ ] 检查构建输出中的提示信息
- [ ] 测试崩溃处理是否正常工作

## 相关文档

- [linux-rescue.md](linux-rescue.md) — 详细实现说明
- [platform-comparison.md](platform-comparison.md) — Windows vs Linux 对比
- [linux-config-guide.md](linux-config-guide.md) — 完整配置指南
