# Style: Environment & User Context (s_env)

## 用户是谁

对话对象是 **VersatileEngine 的作者**。

他是系统架构师与主要实现者，对以下内容有完整的第一手认知：
- VE 的设计哲学、节点树结构、模块生命周期

不需要向他解释 VE 的基础概念。直接在他的认知框架上对话。

## VE 核心哲学（必须内化）

数据驱动 + 函数式编程，通过合理的数据结构搭配模块划分

关键原则（来自 [DESIGN.md](../DESIGN.md)）：

1. **One tree, many clients** — 所有运行时关注点（配置、状态、命令 I/O）都表达为节点状态，不建平行模型。
2. **Glue layer, not business framework** — VE 坐在边界上，不侵入领域代码。
3. **模块生命周期**：`init` → `ready` → `deinit`，模块通过 `ve::Var` 读写节点树。
4. **operate / subscribe 约定**：写用 operate，读用 subscribe，不直接暴露底层传输。
5. **跨传输一致性**：Terminal (10000) / BinTCP (11000) / HTTP (12000) / WebSocket (12100) 暴露的是同一棵树。

## 关键文档速查

| 文档 | 内容 |
|---|---|
| [docs/DESIGN.md](../DESIGN.md) | 设计原则与分层架构 |
| [docs/QUICK_REFERENCE.md](../QUICK_REFERENCE.md) | `ve::Var` / Node API 速查 |
| [docs/CORE.md](../CORE.md) | 核心实现细节 |
| [docs/SERVICE.md](../SERVICE.md) | 服务层（HTTP/WS/BinTCP）协议 |
| [ros/ARCHITECTURE.md](../ros/ARCHITECTURE.md) | VE ↔ ROS 2 集成架构 |

## 对话默认假设

- 技术问题直接讨论实现细节，跳过科普铺垫。
- 涉及节点路径时，用 VE 约定的斜杠格式（`ve/robot/state/ready`）。
- 跨项目任务的缝合点永远是 VE 节点树——先问"暴露/消费哪条节点路径"，再考虑其他。
