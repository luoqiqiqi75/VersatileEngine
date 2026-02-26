# Windows与Linux堆栈跟踪实现对比

## 功能对比表

| 功能 | Windows | Linux | 说明 |
|------|---------|-------|------|
| 崩溃信号捕获 | ✅ (SEH) | ✅ (Signal Handler) | 都能捕获程序崩溃 |
| 当前线程堆栈 | ✅ 完整 | ✅ 完整 | 都能获取完整堆栈 |
| 其他线程堆栈 | ✅ 完整 | ⚠️ 有限 | Linux正常模式下无法获取 |
| 符号名称解析 | ✅ 完整 | ✅ 完整 | 都能解析函数名 |
| C++名称解码 | ✅ 自动 | ✅ 手动 | 都支持C++符号解码 |
| 文件名和行号 | ✅ (需要PDB) | ⚠️ (需要调试符号) | 都需要额外的调试信息 |
| 导出数据功能 | ✅ | ✅ | 功能相同 |

## 实现细节对比

### Windows实现 (`win/rescue.cpp`)

#### 关键API和技术
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
- Windows系统库

---

### Linux实现 (`linux/rescue.cpp`)

#### 关键API和技术
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
- 标准C/C++库

---

## 架构对比

### Windows架构
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

### Linux架构
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
# 自动包含符号（使用PDB文件）
# 无需特殊链接选项
```

### Linux（统一配置方式）

**在 `cmake/_custom.cmake` 中控制开关**：
```cmake
# 启用堆栈跟踪（开发/调试环境）
option(ENABLE_STACK_TRACE "Enable stack trace for Linux" ON)

# 禁用堆栈跟踪（生产环境，减小体积）
# set(ENABLE_STACK_TRACE OFF)
```

**在 `cmake/platform.cmake` 中统一实现**：
```cmake
if(UNIX AND NOT ANDROID AND NOT APPLE)
    if(ENABLE_STACK_TRACE)
        add_link_options(-rdynamic)  # 自动应用到所有目标
    endif()
endif()
```

**单独的库依赖**：
```cmake
# 仅需要dladdr的库才链接dl
target_link_libraries(${VE_CORE_LIBRARY} PRIVATE dl)
```

**关键差异**:
- Windows: PDB文件包含完整调试信息，与二进制文件分离
- Linux: 需要 `-rdynamic` 将符号嵌入二进制文件
- **优势**: 统一管理，通过一个开关控制整个项目，避免污染Release版本

---

## 限制和权衡

### Windows优势
1. ✅ 可以获取任意线程的堆栈（通过挂起线程）
2. ✅ 完整的文件名和行号信息（通过PDB）
3. ✅ 成熟的DbgHelp API

### Windows劣势
1. ❌ 仅Windows平台
2. ❌ 需要PDB文件获取完整信息
3. ❌ 依赖Windows特定API

### Linux优势
1. ✅ 标准POSIX接口，可移植性好
2. ✅ 轻量级实现，依赖少
3. ✅ 开源工具链支持好

### Linux劣势
1. ❌ 正常模式下无法获取其他线程堆栈
2. ❌ 符号信息精度依赖编译选项
3. ❌ `-rdynamic` 增加二进制文件大小

---

## 使用建议

### 开发调试阶段
**Windows**:
- 使用Debug配置生成PDB
- 可以使用Visual Studio调试器配合

**Linux**:
- 在 `cmake/_custom.cmake` 中设置 `ENABLE_STACK_TRACE ON`
- 使用 `-g -O0` 编译
- 使用GDB配合调试

### 生产环境
**Windows**:
- 提供PDB符号服务器
- 使用Release配置 + PDB

**Linux** (推荐配置):

**方案1：完全禁用符号（最小体积）**
```cmake
# cmake/_custom.cmake
set(ENABLE_STACK_TRACE OFF)  # 禁用-rdynamic
```
```bash
cmake -DCMAKE_BUILD_TYPE=Release ..
```

**方案2：保留符号但分离（折中方案）**
```cmake
# cmake/_custom.cmake
set(ENABLE_STACK_TRACE OFF)  # 禁用-rdynamic，减小运行时体积
```
```bash
# 编译时保留调试符号
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-g" ..
# 编译后分离符号
objcopy --only-keep-debug movax movax.debug
strip movax
# 需要时添加符号链接
objcopy --add-gnu-debuglink=movax.debug movax
```

**方案3：保留堆栈跟踪（便于现场诊断）**
```cmake
# cmake/_custom.cmake
set(ENABLE_STACK_TRACE ON)  # 保留堆栈跟踪能力
```
```bash
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-g -O2" ..
```

### 配置对比

| 配置 | 体积 | 堆栈跟踪 | 调试能力 | 适用场景 |
|------|------|---------|---------|---------|
| ENABLE_STACK_TRACE=OFF + strip | 最小 | ❌ | ❌ | 生产环境 |
| ENABLE_STACK_TRACE=OFF + -g | 中等 | ❌ | ✅ (GDB) | 需离线调试 |
| ENABLE_STACK_TRACE=ON + -g | 较大 | ✅ | ✅ | 开发/诊断 |

---

## 改进方向

### Windows
1. 考虑使用MiniDump写入崩溃转储文件
2. 集成Windows Error Reporting (WER)
3. 添加堆状态分析

### Linux
1. 集成libunwind实现跨线程堆栈跟踪
2. 使用libbacktrace获取源码位置
3. 添加core dump配置
4. 支持远程符号服务器

---

## 测试检查清单

### 共同测试项
- [ ] 正常运行时打印当前线程堆栈
- [ ] 空指针解引用崩溃
- [ ] 除零异常
- [ ] 多线程环境测试
- [ ] 符号名称正确解析
- [ ] C++名称正确解码
- [ ] 导出数据功能

### Windows特定
- [ ] 访问违例（Access Violation）
- [ ] 未处理的C++异常
- [ ] 所有线程堆栈完整显示

### Linux特定
- [ ] SIGSEGV信号处理
- [ ] SIGABRT信号处理
- [ ] SIGFPE信号处理
- [ ] 线程列表正确枚举

---

## 结论

两个平台的实现都能满足基本的堆栈跟踪需求，但有各自的优缺点：

- **Windows实现**更完整，可以获取所有线程信息
- **Linux实现**更轻量，但正常模式下只能跟踪当前线程

对于崩溃处理，两个平台都能提供足够的信息用于问题诊断。建议根据实际需求选择合适的配置和优化级别。
