# Linux堆栈跟踪 - 快速配置指南

## 🎯 快速配置

在 `cmake/_custom.cmake` (本地配置) 中设置：

```cmake
# 开发环境（完整堆栈跟踪 + 文件名行号）
set(FORCE_DEBUG_INFO ON)              # 添加-g，显示文件名和行号
option(ENABLE_STACK_TRACE ON)         # 添加-rdynamic，导出符号

# 生产环境（最小体积）
# set(FORCE_DEBUG_INFO OFF)
# set(ENABLE_STACK_TRACE OFF)
```

**注意**: `cmake/_custom.cmake` 在 `.gitignore` 中，不会提交到版本控制。

## 📋 不同环境的推荐配置

### 开发环境 (Development) ⭐ 推荐
```cmake
# cmake/_custom.cmake
set(FORCE_DEBUG_INFO ON)       # 显示文件名和行号
option(ENABLE_STACK_TRACE ON)  # 导出符号表
```
```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
make
# 结果：完整堆栈 + 文件名:行号
```

### 测试环境 (Testing/Staging)
```cmake
# cmake/_custom.cmake
set(FORCE_DEBUG_INFO ON)       # 保留调试信息
option(ENABLE_STACK_TRACE ON)  # 可现场诊断
```
```bash
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo ..
make
# 结果：优化代码 + 调试信息
```

### 生产环境 (Production/Release)
```cmake
# cmake/_custom.cmake
set(FORCE_DEBUG_INFO OFF)      # 不包含调试信息
set(ENABLE_STACK_TRACE OFF)    # 不导出符号（最小体积）
```
```bash
cmake -DCMAKE_BUILD_TYPE=Release ..
make
# 结果：最小体积，无调试信息
```

## 🔍 效果对比

### ENABLE_STACK_TRACE = ON
```
✅ 优点：
  - 崩溃时自动打印详细堆栈
  - 可以在线打印堆栈（dump命令）
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
  - 需要配合GDB或符号文件才能调试
```

## 💡 使用示例

### 1. 初始化（自动设置崩溃处理）
```cpp
#include "rescue.h"

void init() {
    setupRescue(dataObject);
}
```

### 2. 在线打印当前线程堆栈
```cpp
// 触发dump
ve::d(dataObject, "dump")->set(nullptr, true);

// 输出位置：
// 1. qCritical() 日志
// 2. dataObject->result 字段

// 输出示例（带-g编译）：
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

## ⚙️ 高级配置

### 临时启用（CMake命令行）
```bash
# 覆盖_custom.cmake中的设置
cmake -DFORCE_DEBUG_INFO=ON -DENABLE_STACK_TRACE=ON ..

# 或使用CMAKE_BUILD_TYPE
cmake -DCMAKE_BUILD_TYPE=Debug ..  # 自动包含-g
```

### 查看是否启用
```bash
# 构建时会显示：
# [STATUS] Linux: Stack trace enabled - added -rdynamic link flag
# [STATUS] Force debug info enabled: added -g flag for stack trace source location
```

### 验证二进制文件是否包含符号
```bash
# 查看符号表
nm -D ./MovaX | grep -i "yourfunction"

# 查看文件大小
ls -lh ./MovaX

# ENABLE_STACK_TRACE=ON:  约 50-60 MB
# ENABLE_STACK_TRACE=OFF: 约 40-50 MB
```

## 🚨 常见问题

### Q: 为什么只显示地址或符号名，没有文件名和行号？
**A:** **必须启用 `FORCE_DEBUG_INFO`！**

```cmake
# 方法1：在 cmake/_custom.cmake 中设置（推荐）
set(FORCE_DEBUG_INFO ON)

# 方法2：使用Debug构建类型（自动包含-g）
cmake -DCMAKE_BUILD_TYPE=Debug ..

# 方法3：命令行临时启用
cmake -DFORCE_DEBUG_INFO=ON ..
```

**错误配置**：
```cmake
# cmake/_custom.cmake
set(FORCE_DEBUG_INFO OFF)  # ← 这会导致看不到文件名和行号

# 或使用Release模式且不设置FORCE_DEBUG_INFO
cmake -DCMAKE_BUILD_TYPE=Release ..  # 默认没有-g
```

### Q: lambda函数显示为 ??? 怎么办？
**A:** 使用 `-g` 编译后，lambda会显示定义位置：
```
// 不带-g：
#1 MovaX(???) [0x123456]

// 带-g：
#1 MyClass::onButtonClick()::{lambda()#1}::operator()() at main.cpp:123 [0x123456]
```

### Q: Release版本应该如何配置？
**A:** 
- **选项1（推荐）**：`ENABLE_STACK_TRACE OFF` + 不带`-g`，最小体积
- **选项2（便于诊断）**：`ENABLE_STACK_TRACE ON` + 带`-g`，可现场调试
- 根据需求选择，建议测试环境使用选项2，生产环境使用选项1

### Q: 关闭后崩溃了怎么办？
**A:** 
1. 使用 `gdb` 加载core dump文件（需要符号文件）
2. 或者编译一个开启堆栈跟踪 + `-g` 的版本重现问题
3. 使用 `addr2line` 手动解析地址

### Q: 会影响性能吗？
**A:** 
- `-rdynamic` 对运行时性能影响很小（< 1%）
- `-g` 不影响运行时性能，只增加文件体积
- 堆栈跟踪使用`addr2line`批量解析，性能开销可接受（崩溃时才执行）
- 正常运行完全不受影响

### Q: 这个开关影响哪些目标？
**A:** `ENABLE_STACK_TRACE` 只影响主程序 `MovaX`，库不需要（主程序的`-rdynamic`会导出所有符号）

## 📚 相关文档

- [README.md](./README.md) - 详细实现说明
- [PLATFORM_COMPARISON.md](../PLATFORM_COMPARISON.md) - Windows vs Linux对比

## ✅ 快速检查清单

- [ ] 确认 `cmake/_custom.cmake` 中的 `ENABLE_STACK_TRACE` 设置
- [ ] Debug/Dev环境：设置为 `ON`
- [ ] Release/Prod环境：设置为 `OFF`
- [ ] 重新运行 cmake 配置
- [ ] 检查构建输出中的提示信息
- [ ] 测试崩溃处理是否正常工作
