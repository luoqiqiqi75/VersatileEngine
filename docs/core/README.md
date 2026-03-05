# VE Core 模块文档

> `core/` 目录是 VE 的纯 C++ 核心，不依赖 Qt。

---

## 目录内容

```
core/
├── include/ve/
│   ├── global.h              全局宏、标准库包含
│   └── core/
│       ├── base.h            Object, Manager, 容器, 类型工具
│       ├── convert.h         convert<T> 定制点 (placeholder)
│       ├── factory.h         Factory<Sig> 工厂模板
│       ├── log.h             日志接口（纯 C++ API）
│       └── node.h            ve::Node (placeholder, Phase 1)
├── src/
│   └── base.cpp              Object/Manager 实现
├── platform/
│   ├── win/                  Windows 崩溃处理 (SEH + StackWalk64)
│   │   ├── rescue.h/cpp
│   │   └── StackWalker/      第三方堆栈遍历库
│   ├── linux/                Linux 崩溃处理 (signal + backtrace)
│   │   ├── rescue.h/cpp
│   └── unsupported/          其他平台回退 stub
│       └── rescue.h
└── CMakeLists.txt            构建 libve 共享库
```

## 构建说明

`core/` 独立构建为 `libve`（纯 C++17 共享库，零 Qt 依赖）。
Qt 模块 `libveqt` 通过 `target_link_libraries(libveqt PUBLIC libve)` 链接核心库。

## 平台崩溃处理 (Rescue)

平台崩溃处理模块提供了跨平台的堆栈跟踪和崩溃捕获能力：

| 文档 | 内容 |
|------|------|
| [platform-comparison.md](platform-comparison.md) | Windows vs Linux 实现对比 |
| [linux-rescue.md](linux-rescue.md) | Linux 实现详解 |
| [linux-config-guide.md](linux-config-guide.md) | Linux 符号导出配置指南 |
| [linux-quick-start.md](linux-quick-start.md) | Linux 快速配置 |

## 相关文档

- [ARCHITECTURE.md](../ARCHITECTURE.md) — 整体架构与演进方向
- [plan/README.md](../internal/plan/README.md) — 分阶段重构计划
- [phase1-value-and-node.md](../internal/plan/phase1-value-and-node.md) — ve::Value + ve::Node 设计

---

*更新日期: 2026-03-04*
