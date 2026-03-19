# VE Core 模块文档

> `core/` 目录是 VE 的纯 C++17 核心，不依赖 Qt。构建产出为 `libve` 共享库。
> 
> **2026-03-18**：535 个单元测试全部通过（MSVC 19.44, x64 Release）。

---

## 目录结构

```
core/
├── include/ve/
│   ├── global.h                 全局宏 (VE_API, VE_AUTO_RUN, VE_DECLARE_PRIVATE, ...)
│   ├── core/
│   │   ├── base.h               Object, Manager, 容器, 类型工具, KVAccessor
│   │   ├── var.h                Var (16B variant, 10 类型 + CUSTOM)
│   │   ├── node.h               Node (响应式数据树, Vector+Hash, Pool, shadow, schema)
│   │   ├── command.h            Command 命令系统
│   │   ├── step.h               Step 单步执行单元
│   │   ├── pipeline.h           Pipeline 状态机
│   │   ├── result.h             Result (SUCCESS/FAIL/ACCEPT + Var content)
│   │   ├── data.h               AnyData<T>, DataManager, DataList, DataDict, YAML 序列化
│   │   ├── factory.h            Factory<Sig>, Pool<T>, Pooled<T>, PoolPtr<T>
│   │   ├── module.h             Module 生命周期 (NONE→INIT→READY→DEINIT)
│   │   ├── loop.h               Loop<T>, EventLoop, LoopRef, loop::main/pool
│   │   ├── convert.h            Convert<T> (toString/fromString/toBin/fromBin)
│   │   ├── log.h                日志接口 (spdlog 后端)
│   │   ├── rescue.h             崩溃处理公共 API
│   │   ├── schema.h             Schema 结构化序列化
│   │   └── impl/
│   │       ├── hashfuncs.h      哈希函数 (DJB2, MurmurHash3, Wang64→32, fmix32)
│   │       ├── ordered_hashmap.h OrderedHashMap (Robin Hood + 插入顺序)
│   │       ├── json.h           JSON 序列化 (simdjson 解析, stringify, exportTree)
│   │       └── bin.h            Binary 序列化 (CBS 兼容)
│   ├── service/
│   │   ├── terminal.h           Terminal REPL (TCP, 端口 5061)
│   │   ├── http_server.h        HTTP Server (REST-like, 端口 8080)
│   │   ├── ws_server.h          WebSocket Server (端口 8081)
│   │   ├── tcp_bin_server.h     TCP Binary Server (CBS, 端口 5065)
│   │   └── subscribe_service.h  Node 变更订阅服务
│   └── entry.h                  Entry 生命周期 + plugin + version
│
├── src/
│   ├── base.cpp / object.cpp    Object / Manager 实现
│   ├── var.cpp                  Var 实现
│   ├── node.cpp                 Node 实现 (树操作, 信号, Pool)
│   ├── command.cpp              Command 注册和调用
│   ├── builtin_commands.cpp     20+ 内建命令 (ls/get/set/add/rm/mv/mk/find/json...)
│   ├── step.cpp                 Step 实现
│   ├── pipeline.cpp             Pipeline 状态机实现
│   ├── entry.cpp                Entry 生命周期 (setup/init/run/deinit)
│   ├── loop.cpp                 EventLoop + main/pool
│   ├── schema.cpp               Schema export/import
│   ├── json.cpp                 JSON 序列化 (simdjson)
│   ├── bin.cpp                  Binary 序列化
│   ├── data.cpp                 AnyData / DataManager 实现
│   ├── hashfuncs.cpp            非内联哈希函数
│   ├── log.cpp                  spdlog 日志初始化
│   ├── module.cpp               Module 状态机实现
│   └── service/                 Terminal, HTTP, WS, TCP Binary 服务实现
│
├── platform/                    平台崩溃处理
│   ├── win/                     Windows (SEH + StackWalk64)
│   ├── linux/                   Linux (signal + backtrace)
│   └── unsupported/             其他平台回退 stub
│
├── test/                        单元测试 (535 cases, 自定义框架)
│   ├── ve_test.h                轻量测试框架 (VE_TEST, VE_ASSERT_*, VE_RUN_ALL)
│   ├── main.cpp                 测试入口
│   ├── test_basic_traits.cpp    basic:: 类型工具 (43 cases)
│   ├── test_containers.cpp      Vector/List/Map/HashMap/Dict (18 cases)
│   ├── test_ordered_hashmap.cpp OrderedHashMap + OrderedDict (12 cases)
│   ├── test_small_vector.cpp    SmallVector<T,N> (38 cases)
│   ├── test_object.cpp          Object 信号/生命周期/线程安全 (15 cases)
│   ├── test_manager.cpp         Manager 对象管理 (9 cases)
│   ├── test_data.cpp            AnyData + DataManager (17 cases)
│   ├── test_data_serialize.cpp  字符串/YAML 序列化 (12 cases)
│   ├── test_hashfuncs.cpp       哈希函数正确性 (18 cases)
│   ├── test_log.cpp             日志系统 smoke test (6 cases)
│   ├── test_values.cpp          Values 单位转换 (10 cases)
│   ├── test_var.cpp             Var 类型系统 (42 cases)
│   ├── test_command.cpp         Command/Step/Pipeline (22 cases)
│   ├── test_loop.cpp            EventLoop/LoopRef (cases)
│   ├── test_node_basic.cpp      Node 创建/查询/计数/遍历
│   ├── test_node_signal.cpp     Node 信号 (ADDED/REMOVED/ACTIVATED/冒泡)
│   ├── test_node_path.cpp       key/path/resolve/ensure/erase/shadow/schema
│   ├── test_node_value.cpp      Node 值操作 + ve::n() 全局访问
│   ├── test_node_navigation.cpp parent/indexOf/sibling/prev/next/isAncestorOf
│   ├── test_node_management.cpp insert/append/take/remove/clear/name 验证
│   └── test_node_bench.cpp      压力测试 + 复杂结构 + 性能基准
│
└── CMakeLists.txt               构建 libve 共享库
```

## 核心组件

### var.h — ve::Var

| 特性 | 说明 |
|------|------|
| 大小 | **16 bytes**（比 QVariant 更紧凑） |
| 类型 | NONE/BOOL/INT/INT64/DOUBLE/STRING/BIN/LIST/DICT/POINTER/CUSTOM |
| 性能 | get<int>() 15ns, set(int) 0.37µs, set(string) 0.54µs |
| 扩展 | CUSTOM 类型通过 CustomData 基类支持任意 C++ 类型 |
| 转换 | Convert<T> 扩展点 (toString/fromString/toBin/fromBin) |

### node.h — ve::Node

| 特性 | 说明 |
|------|------|
| 存储 | Vector<Node*> + Hash<SmallVector<int>>，Pool 池化 |
| 信号 | NODE_CHANGED / NODE_ACTIVATED / NODE_ADDED / NODE_REMOVED |
| 冒泡 | WATCHING flag 控制 activated 信号向上传播 |
| 同名 | 支持同名子节点 `#N` 索引 |
| shadow | 原型链机制，resolve 时 fallback 到 shadow 节点 |
| schema | 结构化导入/导出 (JSON/Binary) |
| 性能 | child(index) 590x, iterator 135x, indexOf 42x (vs imol) |

### command.h — 命令系统

| 组件 | 说明 |
|------|------|
| `ve::Step` | 单步执行单元: `Result(const Var&)` |
| `ve::Pipeline` | 状态机: IDLE→RUNNING→PAUSED→DONE/ERRORED |
| `ve::Command` | 命名步骤序列, `pipeline()` 创建执行实例 |
| 内建命令 | ls/info/get/set/add/rm/mv/mk/find/erase/json/help/child/shadow/watch/iter/schema/cmd |

### service/ — 服务层

| 服务 | 端口 | 协议 | 说明 |
|------|------|------|------|
| Terminal | 5061 | TCP 文本 | 数据树 REPL, Tab 补全 |
| HTTP | 8080 | HTTP | REST-like Node 访问 |
| WebSocket | 8081 | WS | 实时 Node 变更推送 |
| TCP Binary | 5065 | CBS | 高效二进制 IPC |

### base.h

| 组件 | 说明 |
|------|------|
| `ve::Object` | 基类：名称、父子、信号/槽、线程安全、LoopRef 派发 |
| `ve::Manager` | Object 容器（HashMap<string, Object*>） |
| `ve::Vector<T>` / `ve::List<T>` | 扩展 STL 容器 |
| `ve::Map<K,V>` / `ve::HashMap<K,V>` / `ve::Dict<V>` | 键值容器 |
| `ve::OrderedHashMap<K,V>` | Robin Hood + 插入顺序 (Godot 移植) |
| `ve::SmallVector<T,N>` | 内联缓冲 + 堆溢出 |
| `ve::basic::FnTraits<F>` / `Meta` | 函数签名内省 + RTTI 辅助 |

### data.h

| 组件 | 说明 |
|------|------|
| `ve::AnyData<T>` | 类型安全响应式数据，信号通知、bind()、YAML 序列化 |
| `ve::DataManager` | 路径注册表 (`data::create`/`data::get`/`data::at`) |
| `ve::DataList` / `ve::DataDict` | 异构类型集合 |

## 构建说明

`core/` 独立构建为 `libve`（Windows: `libve.dll`, Linux: `libve.so`）。

```bash
# 仅构建 core + tests (不需要 Qt)
cmake -B build_test -DVE_BUILD_TEST=ON -DVE_BUILD_QT=OFF -DVE_BUILD_DDS=OFF -DVE_BUILD_RTT=OFF
cmake --build build_test --target ve_test --config Release
./build_test/bin/Release/ve_test    # Windows
./build_test/ve_test                # Linux
```

## 平台崩溃处理 (Rescue)

| 文档 | 内容 |
|------|------|
| [platform-comparison.md](platform-comparison.md) | Windows vs Linux 实现对比 |
| [linux-rescue.md](linux-rescue.md) | Linux 实现详解 |
| [linux-config-guide.md](linux-config-guide.md) | Linux 符号导出配置指南 |
| [linux-quick-start.md](linux-quick-start.md) | Linux 快速配置 |

## 相关文档

- [ARCHITECTURE.md](../ARCHITECTURE.md) - 整体架构与演进方向

---

*更新日期: 2026-03-18*
