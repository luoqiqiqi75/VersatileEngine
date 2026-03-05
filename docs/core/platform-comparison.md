# Windows 与 Linux 堆栈跟踪实现对比

> 对应源码: `core/platform/win/rescue.cpp` 和 `core/platform/linux/rescue.cpp`

## 功能对比表

| 功能 | Windows | Linux | 说明 |
|------|---------|-------|------|
| 崩溃信号捕获 | ✅ (SEH) | ✅ (Signal Handler) | 都能捕获程序崩溃 |
| 当前线程堆栈 | ✅ 完整 | ✅ 完整 | 都能获取完整堆栈 |
| 其他线程堆栈 | ✅ 完整 | ⚠️ 有限 | Linux 正常模式下无法获取 |
| 符号名称解析 | ✅ 完整 | ✅ 完整 | 都能解析函数名 |
| C++名称解码 | ✅ 自动 | ✅ 手动 | 都支持 C++ 符号解码 |
| 文件名和行号 | ✅ (需要PDB) | ⚠️ (需要调试符号) | 都需要额外的调试信息 |
| 导出数据功能 | ✅ | ✅ | 功能相同 |

## 实现细节对比

### Windows 实现 (`core/platform/win/rescue.cpp`)

#### 关键 API 和技术
- **线程枚举**: `CreateToolhelp32Snapshot` + `Thread32First/Next`
- **线程挂起**: `OpenThread` + `GetThreadContext`
- **堆栈遍历**: `StackWalk64` (DbgHelp API)
- **符号解析**: `SymFromAddr` + `SymGetLineFromAddr64`
- **崩溃处理**: `SetUnhandledExceptionFilter` (SEH)

#### 代码示例
```cpp
// 获取线程上下文
CONTEXT context = { 0 };
context.ContextFlags = CONTEXT_FULL;
GetThreadContext(hThread, &context);

// 遍历堆栈
StackWalk64(IMAGE_FILE_MACHINE_AMD64, hProcess, hThread,
            &stackFrame, &context, NULL,
            SymFunctionTableAccess64, SymGetModuleBase64, NULL);

// 获取符号
SymFromAddr(hProcess, stackFrame.AddrPC.Offset, &displacement, &symbolInfo);
```

#### 依赖库
- `dbghelp.lib`: 符号和堆栈处理
- Windows 系统库

---

### Linux 实现 (`core/platform/linux/rescue.cpp`)

#### 关键 API 和技术
- **线程枚举**: 读取 `/proc/self/task/` 目录
- **线程ID获取**: `syscall(SYS_gettid)`
- **堆栈遍历**: `backtrace()` (glibc)
- **符号解析**: `dladdr()` + `backtrace_symbols()`
- **名称解码**: `abi::__cxa_demangle()`
- **崩溃处理**: `sigaction()` 注册信号处理器

#### 代码示例
```cpp
// 获取堆栈帧
void* buffer[MAX_FRAMES];
int nptrs = backtrace(buffer, MAX_FRAMES);

// 获取符号信息
Dl_info info;
if (dladdr(addr, &info)) {
    // 解码C++名称
    char* demangled = abi::__cxa_demangle(info.dli_sname, ...);
}

// 注册信号处理
struct sigaction sa;
sa.sa_sigaction = CrashSignalHandler;
sigaction(SIGSEGV, &sa, nullptr);
```

#### 依赖库
- `libdl`: 动态链接和符号解析
- `libpthread`: 线程支持
- 标准 C/C++ 库

---

## 架构对比

### Windows 架构
```
setupRescue()
├── set_default_handler()
│   └── SetUnhandledExceptionFilter(CBaseException::UnhandledExceptionFilter)
├── export信号
│   └── 导出数据到root.bin
└── dump信号
    ├── GetAllThreadIds() (枚举所有线程)
    └── PrintThreadStack() (打印每个线程)
        ├── OpenThread()
        ├── GetThreadContext()
        ├── StackWalk64()
        └── SymFromAddr() + SymGetLineFromAddr64()
```

### Linux 架构
```
setupRescue()
├── SetupCrashHandler()
│   ├── sigaction(SIGSEGV, ...)
│   ├── sigaction(SIGABRT, ...)
│   ├── sigaction(SIGFPE, ...)
│   ├── sigaction(SIGILL, ...)
│   └── sigaction(SIGBUS, ...)
├── export信号
│   └── 导出数据到root.bin
└── dump信号
    ├── GetAllThreadIds() (读取/proc/self/task/)
    └── PrintThreadStack() (仅当前线程)
        ├── backtrace()
        ├── backtrace_symbols()
        └── dladdr() + abi::__cxa_demangle()
```

---

## 符号导出配置对比

### Windows
```cmake
# PDB 文件自动包含符号信息
# Debug 模式默认生成 PDB，无需特殊链接选项
```

### Linux

Linux 需要两个关键的编译/链接选项才能获得完整的堆栈跟踪信息：

| 选项 | 作用 | 影响 |
|------|------|------|
| `-rdynamic` | 导出所有符号到动态符号表 | 二进制增大 10-20% |
| `-g` | 包含调试信息（文件名、行号） | 仅增加 DWARF 信息，不影响运行时性能 |

**在使用 VE 的项目中配置**（VE 自身不强制这些选项，由上层项目决定）：

```cmake
# 项目级 CMakeLists.txt 中根据需要添加：
if(UNIX AND NOT ANDROID AND NOT APPLE)
    # 符号导出（堆栈跟踪可解析符号名）
    add_link_options(-rdynamic)
endif()

# 调试信息（堆栈跟踪可显示文件名和行号）
# 通常通过 CMAKE_BUILD_TYPE=Debug/RelWithDebInfo 自动包含
```

VE 项目自身可通过 `cmake/_local.cmake` 配置本地选项（参见 `cmake/_local.cmake.example`）。

**关键差异**:
- Windows: PDB 文件包含完整调试信息，与二进制文件分离
- Linux: 需要 `-rdynamic` 将符号嵌入二进制文件

---

## 限制和权衡

### Windows 优势
1. ✅ 可以获取任意线程的堆栈（通过挂起线程）
2. ✅ 完整的文件名和行号信息（通过 PDB）
3. ✅ 成熟的 DbgHelp API

### Windows 劣势
1. ❌ 仅 Windows 平台
2. ❌ 需要 PDB 文件获取完整信息
3. ❌ 依赖 Windows 特定 API

### Linux 优势
1. ✅ 标准 POSIX 接口，可移植性好
2. ✅ 轻量级实现，依赖少
3. ✅ 开源工具链支持好

### Linux 劣势
1. ❌ 正常模式下无法获取其他线程堆栈
2. ❌ 符号信息精度依赖编译选项
3. ❌ `-rdynamic` 增加二进制文件大小

---

## 使用建议

### 开发调试阶段
**Windows**:
- 使用 Debug 配置生成 PDB
- 可以使用 Visual Studio 调试器配合

**Linux**:
- 使用 `-g -O0` 编译（Debug 模式自动包含）
- 确保链接时带 `-rdynamic`
- 使用 GDB 配合调试

### 生产环境

**方案1：完全禁用符号（最小体积）**
```bash
cmake -DCMAKE_BUILD_TYPE=Release ..
```

**方案2：保留符号但分离（折中方案）**
```bash
# 编译时保留调试符号
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-g" ..
# 编译后分离符号
objcopy --only-keep-debug myapp myapp.debug
strip myapp
# 需要时添加符号链接
objcopy --add-gnu-debuglink=myapp.debug myapp
```

**方案3：保留堆栈跟踪（便于现场诊断）**
```bash
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-g -O2" ..
# 链接时确保 -rdynamic
```

### 配置对比

| 配置 | 体积 | 堆栈跟踪 | 调试能力 | 适用场景 |
|------|------|---------|---------|---------|
| Release + strip | 最小 | ❌ | ❌ | 生产环境 |
| Release + `-g` (无 -rdynamic) | 中等 | ❌ | ✅ (GDB) | 需离线调试 |
| Release + `-g` + `-rdynamic` | 较大 | ✅ | ✅ | 开发/诊断 |

---

## 改进方向

### Windows
1. 考虑使用 MiniDump 写入崩溃转储文件
2. 集成 Windows Error Reporting (WER)
3. 添加堆状态分析

### Linux
1. 集成 libunwind 实现跨线程堆栈跟踪
2. 使用 libbacktrace 获取源码位置
3. 添加 core dump 配置
4. 支持远程符号服务器

---

## 测试检查清单

### 共同测试项
- [ ] 正常运行时打印当前线程堆栈
- [ ] 空指针解引用崩溃
- [ ] 除零异常
- [ ] 多线程环境测试
- [ ] 符号名称正确解析
- [ ] C++ 名称正确解码
- [ ] 导出数据功能

### Windows 特定
- [ ] 访问违例（Access Violation）
- [ ] 未处理的 C++ 异常
- [ ] 所有线程堆栈完整显示

### Linux 特定
- [ ] SIGSEGV 信号处理
- [ ] SIGABRT 信号处理
- [ ] SIGFPE 信号处理
- [ ] 线程列表正确枚举

---

## 结论

两个平台的实现都能满足基本的堆栈跟踪需求，但有各自的优缺点：

- **Windows 实现**更完整，可以获取所有线程信息
- **Linux 实现**更轻量，但正常模式下只能跟踪当前线程

对于崩溃处理，两个平台都能提供足够的信息用于问题诊断。建议根据实际需求选择合适的配置和优化级别。
