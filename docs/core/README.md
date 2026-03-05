# VE Core 模块文档

> `core/` 目录是 VE 的纯 C++17 核心，不依赖 Qt。构建产出为 `libve` 共享库。

---

## 目录结构

```
core/
├── include/ve/
│   ├── global.h                 全局宏 (VE_API, VE_AUTO_RUN, VE_DECLARE_PRIVATE, ...)
│   └── core/
│       ├── base.h               Object, Manager, 容器, 类型工具, KVAccessor
│       ├── data.h               AnyData<T>, DataManager, DataList, DataDict, YAML 序列化
│       ├── factory.h            Factory<Sig> 工厂模板
│       ├── module.h             Module 生命周期 (NONE→INIT→READY→DEINIT)
│       ├── log.h                日志接口 (spdlog 后端)
│       ├── node.h               ve::Node (placeholder, Phase 1)
│       ├── convert.h            convert<T> 定制点 (placeholder)
│       ├── rescue.h             崩溃处理公共 API
│       └── impl/
│           ├── hashfuncs.h      哈希函数 (DJB2, MurmurHash3, Wang64→32, fmix32)
│           └── ordered_hashmap.h OrderedHashMap (Robin Hood + 插入顺序)
│
├── src/
│   ├── base.cpp                 Object / Manager 实现
│   ├── data.cpp                 AnyData / DataManager 实现
│   ├── hashfuncs.cpp            非内联哈希函数 (murmur3 float/double/buffer)
│   ├── log.cpp                  spdlog 日志初始化与封装
│   └── module.cpp               Module 状态机实现
│
├── platform/                    平台崩溃处理
│   ├── win/                     Windows (SEH + StackWalk64)
│   │   ├── rescue.h/cpp
│   │   └── StackWalker/         第三方堆栈遍历库
│   ├── linux/                   Linux (signal + backtrace)
│   │   └── rescue.h/cpp
│   └── unsupported/             其他平台回退 stub
│       └── rescue.h
│
├── test/                        单元测试 (140 cases, 自定义框架)
│   ├── ve_test.h                轻量测试框架 (VE_TEST, VE_ASSERT_*, VE_RUN_ALL)
│   ├── main.cpp                 测试入口
│   ├── test_basic_traits.cpp    basic:: 类型工具 (23 cases)
│   ├── test_containers.cpp      Vector/List/Map/HashMap/Dict (18 cases)
│   ├── test_ordered_hashmap.cpp OrderedHashMap + OrderedDict (12 cases)
│   ├── test_object.cpp          Object 信号/生命周期 (11 cases)
│   ├── test_manager.cpp         Manager 对象管理 (9 cases)
│   ├── test_data.cpp            AnyData + DataManager (17 cases)
│   ├── test_data_serialize.cpp  字符串/YAML 序列化 (12 cases)
│   ├── test_hashfuncs.cpp       哈希函数正确性 (18 cases)
│   ├── test_log.cpp             日志系统 smoke test (8 cases)
│   └── test_values.cpp          Values 单位转换 (10 cases)
│
└── CMakeLists.txt               构建 libve 共享库
```

## 核心组件

### base.h

| 组件 | 说明 |
|------|------|
| `ve::Object` | 基类：名称、父子关系、整数信号/槽 (`connect`/`trigger`/`disconnect`) |
| `ve::Manager` | Object 容器（HashMap<string, Object*>），自动管理 parent |
| `ve::Vector<T>` | 扩展 std::vector (append, prepend, has, value, every, toString) |
| `ve::List<T>` | 扩展 std::list (operator[], out_of_range 检查) |
| `ve::Map<K,V>` | 扩展 std::map (has, keys, values, insertOne) |
| `ve::HashMap<K,V>` | 基于 Godot OrderedHashMap 的有序哈希表 |
| `ve::Dict<V>` | = HashMap<string, V>，字符串键快捷容器 |
| `ve::OrderedHashMap<K,V>` | Robin Hood 开放寻址 + 双向链表保序 (Godot 移植) |
| `ve::OrderedDict<V>` | = OrderedHashMap<string, V> |
| `ve::basic::FInfo<F>` | 函数签名内省（RetT, ClassT, ArgCnt, IsMember 等） |
| `ve::basic::Meta` | RTTI 辅助 (typeName, typeInfoName) |
| `StdPairKVAccess` / `ImplKVAccess` | KV 容器迭代策略 (std::pair vs .key/.value) |

### data.h

| 组件 | 说明 |
|------|------|
| `ve::AbstractData` | 数据基类：flags、listener、序列化接口 |
| `ve::AnyData<T>` | 类型安全响应式数据，信号通知、bind()、YAML 序列化 |
| `ve::DataManager` | 路径注册表 (`data::create`/`data::get`/`data::at`) |
| `ve::DataList` | 异构类型列表 (appendRaw) |
| `ve::DataDict` | 异构类型字典 (insertRaw) |

### impl/hashfuncs.h

| 函数族 | 说明 |
|--------|------|
| `hash_djb2*` | DJB2 字符串/缓冲区哈希 |
| `hash_murmur3_one_32/64` | MurmurHash3 整数哈希 (内联) |
| `hash_murmur3_one_float/double` | MurmurHash3 浮点哈希 (±0/NaN 归一化) |
| `hash_murmur3_buffer` | MurmurHash3 通用缓冲区哈希 |
| `hash_one_uint64` | Thomas Wang 64→32 位哈希 |
| `hash_fmix32` | MurmurHash3 finalizer |
| `HashMapHasherDefault` | 类型分发默认哈希器 |
| `HashMapComparatorDefault` | 类型分发默认比较器 (浮点带容差) |

## 构建说明

`core/` 独立构建为 `libve`（Windows: `libve.dll`, Linux: `libve.so`）。

```bash
# 仅构建 core + tests (不需要 Qt)
cmake -B build_test -DVE_BUILD_TEST=ON -DVE_BUILD_QT=OFF -DVE_BUILD_ROS=OFF -DVE_BUILD_RTT=OFF
cmake --build build_test --target ve_test --config Debug
```

依赖链：
- `libve` → `ve_dep_yaml` (PUBLIC), `ve_dep_spdlog` (PRIVATE)
- `ve_test` → `libve` (PRIVATE)
- Qt 模块通过 `target_link_libraries(... PUBLIC libve)` 链接核心库

## 平台崩溃处理 (Rescue)

| 文档 | 内容 |
|------|------|
| [platform-comparison.md](platform-comparison.md) | Windows vs Linux 实现对比 |
| [linux-rescue.md](linux-rescue.md) | Linux 实现详解 |
| [linux-config-guide.md](linux-config-guide.md) | Linux 符号导出配置指南 |
| [linux-quick-start.md](linux-quick-start.md) | Linux 快速配置 |

## 相关文档

- [ARCHITECTURE.md](../ARCHITECTURE.md) — 整体架构与演进方向
- [plan/README.md](../internal/plan/README.md) — 分阶段重构计划
- [core-test-plan.md](../internal/test/core-test-plan.md) — 测试方案 (140 cases 详细设计)
- [phase1-value-and-node.md](../internal/plan/phase1-value-and-node.md) — ve::Value + ve::Node 设计

---

*更新日期: 2026-03-05*
