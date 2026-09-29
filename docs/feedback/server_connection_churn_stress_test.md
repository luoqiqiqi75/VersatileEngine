# Server 高频建连/断连压测反馈

> 测试日期：2026-09-03  
> 整理日期：2026-09-04  
> 测试提交：`28486df`（重点变更：`4b1a21a [fix] server socket stuck with same thread`）  
> 结论状态：功能与常规回收通过；每连接独占线程、UDP endpoint 滞留和 cleanup 队头阻塞需要处理

## 1. 结论摘要

本轮没有发现 TCP 连接对象、连接 FD 或线程随建连/断连次数持续线性增长。Terminal 和 Node TCP 分别完成数万次短连接后，线程数和 FD 均能回到基线，RSS 在首次扩容后保持稳定，也没有在正常命令下复现明显卡顿。

但压测确认了以下风险：

1. Terminal 与 Node TCP 的“一 socket 一 `AsioLoop`”实际是“一连接一 OS 线程”。300 个并发连接会增加 300 个线程和约 2.4 GB 虚拟线程栈。
2. 连接线程执行命令后会触发 glibc 创建多个 malloc arena。线程退出后 arena 的虚拟地址空间仍被保留，实测 `VmSize` 可长期停留在约 7 GB；这不是普通堆对象泄漏，但属于明显的地址空间膨胀。
3. 所有连接 loop 由一个 cleanup loop 串行 `join()`。只要一个连接任务永久不返回，后续连接状态都会积压，服务同步停止也可能一直等待。
4. Node UDP 底层会为每个远端 IP:port 创建 session，并保留 60 秒。约 2.1 万个唯一 endpoint 使 RSS 增加约 41 MB；session 超时后内存可复用，但 RSS 高水位不会主动归还 OS。
5. StaticServer 对普通文件缺失逐请求输出 warning，高频 404 会形成日志风暴。

## 2. 测试环境

| 项目 | 值 |
|---|---|
| 构建类型 | Release，`-O3 -DNDEBUG` |
| 系统 | Linux |
| libc | glibc 2.39 |
| CPU | 14 个逻辑核 |
| 测试网络 | 本机 loopback |
| 服务基础线程数 | 13 |
| 单线程默认虚拟栈 | 约 8 MB |
| 单元测试 | 638 passed，0 failed |

构建命令：

```bash
cmake --build build -j4
```

默认端口已被本机另一个 daemon 占用，因此 TCP 服务使用自动回退的 `+1` 端口。Node UDP 和 StaticServer 的默认端口未冲突。

| Server | 默认端口 | 实测端口 |
|---|---:|---:|
| Terminal REPL | 10000 | 10001 |
| Terminal AI | 10100 | 10101 |
| Bin TCP | 11000 | 11001 |
| Node HTTP | 12000 | 12001 |
| Node WebSocket | 12100 | 12101 |
| Node TCP | 12200 | 12201 |
| Node UDP | 12300 | 12300 |
| Static HTTP | 12400 | 12400 |

## 3. 测试方法

### 3.1 短连接与有效请求

除 UDP 外，每次操作都新建连接、发送一条有效协议请求、读取完整响应并关闭连接。UDP 的主测试每次创建一个新 UDP socket，以覆盖大量不同源端口的行为。

| Server | 请求内容 |
|---|---|
| Terminal REPL / AI | 建连并读取 welcome/prompt |
| Bin TCP | MessagePack frame，执行 `op=get` |
| Node HTTP | `GET /health`，`Connection: close` |
| Node WebSocket | HTTP Upgrade，发送文本帧 `op=get` |
| Node TCP | 换行分隔 JSON，执行 `op=get` |
| Node UDP | JSON datagram，执行 `op=get` |
| Static HTTP | 请求不存在的静态路径，验证 404 |

采集内容：

- 每次操作耗时及 P50/P95/P99/max；
- `/proc/<pid>/status` 中的 RSS、`VmSize` 和线程数；
- `/proc/<pid>/fd` 的 FD 数量；
- `/proc/<pid>/smaps_rollup` 中的匿名内存；
- `/proc/<pid>/maps` 中的线程栈和 glibc arena 映射。

### 3.2 并发突发

每个有连接概念的服务同时建立并保持 300 个连接，待协议交互完成后统一关闭，观察：

- 活跃连接对应的线程和 FD 增量；
- RSS、`VmSize` 峰值；
- 关闭后线程和 FD 的回落时间；
- 关闭后是否保留额外虚拟地址空间。

### 3.3 UDP endpoint 生命周期

在干净进程中分批创建新 UDP socket，每个 socket 发送一次请求后关闭。记录不同源端口数量和 RSS；等待底层 60 秒 silence timeout 后，再执行同等规模的第二波请求，判断旧 session 是否释放并复用。

## 4. 短连接结果

以下数据用于同一台机器上的相对比较，不应直接作为生产容量承诺。

| Server | 操作数 | 吞吐 | P50 | P95 | P99 | Max | 错误 |
|---|---:|---:|---:|---:|---:|---:|---:|
| Terminal REPL | 10,000 | 8,384/s | 0.110 ms | 0.188 ms | 0.243 ms | 1.643 ms | 0 |
| Terminal AI | 10,000 | 7,943/s | 0.117 ms | 0.198 ms | 0.248 ms | 1.061 ms | 0 |
| Bin TCP | 10,000 | 8,900/s | 0.101 ms | 0.179 ms | 0.277 ms | 1.017 ms | 0 |
| Node HTTP | 10,000 | 13,130/s | 0.068 ms | 0.101 ms | 0.189 ms | 1.190 ms | 0 |
| Node WebSocket | 5,000 | 6,575/s | 0.134 ms | 0.268 ms | 0.366 ms | 1.243 ms | 0 |
| Node TCP | 10,000 | 5,719/s | 0.166 ms | 0.260 ms | 0.335 ms | 1.202 ms | 0 |
| Node UDP | 20,000 | 13,133/s | 0.064 ms | 0.146 ms | 0.186 ms | 2.042 ms | 0 |
| Static HTTP 404 | 20,000 | 11,132/s | 0.082 ms | 0.121 ms | 0.191 ms | 1.264 ms | 0 |

补充结果：

- Terminal AI 连续执行 10 轮、每轮 5,000 次短连接，RSS 最终稳定在约 9.6 MB，线程和 FD 回到基线。
- Node TCP 连续执行 10 轮、每轮 5,000 次有效请求，RSS 在首次扩容后不再增长，线程和 FD 回到基线。
- 正常负载下最大单次耗时约 2.2 ms，没有观察到明显延迟尖峰。

## 5. 300 并发连接结果

以下六项在同一个已预热进程中测试；基线约为 RSS 39.0 MB、`VmSize` 0.74 GB、13 个线程、25 个 FD。

| Server | 成功连接 | 活跃线程 | 活跃 FD | 活跃 RSS | 活跃 `VmSize` | 关闭后线程/FD |
|---|---:|---:|---:|---:|---:|---|
| Terminal REPL | 300 | 313 | 325 | 41.4 MB | 3.17 GB | 回到 13/25 |
| Terminal AI | 300 | 313 | 325 | 41.4 MB | 3.17 GB | 回到 13/25 |
| Bin TCP | 300 | 13 | 325 | 39.1 MB | 0.74 GB | 回到 13/25 |
| Node HTTP | 300 | 13 | 325 | 39.3 MB | 0.74 GB | 回到 13/25 |
| Node WebSocket | 300 | 13 | 325 | 39.3 MB | 0.74 GB | 回到 13/25 |
| Node TCP | 300 | 313 | 325 | 46.9 MB | 9.85 GB | 回到 13/25 |

所有服务均成功建立 300 个连接，没有协议错误。统一关闭后，连接 FD 和线程约在 40–50 ms 内回到稳定值。

StaticServer 在独立进程中测试：

- 300 个连接全部成功；
- 线程维持 13；
- FD 从 26 增至 326，关闭后回到 26；
- `VmSize` 保持约 0.52 GB；
- RSS 从约 11.3 MB 上升到 14.3 MB，第二轮 20,000 次请求后不再增加。

首次触发连接回收后，进程会额外保留 6 个 FD。它们分别属于两个 `ConnectionLoopCleanup` 的 `eventfd`、`eventpoll` 和 `timerfd`，属于 cleanup loop 的一次性延迟初始化，不随连接次数继续增加。

## 6. 详细问题分析

### 6.1 每连接 loop 实际创建一个线程

[`AsioLoop::start()`](../../ve/src/core/loop.cpp#L247) 中直接创建 `std::thread`：

```cpp
_p->worker = std::thread([this] { _p->run(this); });
```

Terminal 和 Node TCP 在每次连接时分别创建并启动该 loop：

- [`TerminalReplServer::start()`](../../ve/src/service/terminal_service.cpp#L406)
- [`NodeTcpServer::start()`](../../ve/src/service/node_tcp_server.cpp#L188)

因此当前设计并不是轻量的“每连接一个逻辑事件队列”，而是“每连接一个线程”。300 个空闲连接增加约 2.4 GB 虚拟栈，与 300 × 8 MB 的默认线程栈吻合。

这会带来：

- 高并发时大量虚拟地址空间占用；
- 线程创建/销毁成本；
- 调度和上下文切换成本；
- 受进程线程数、容器 PID 限制和平台默认栈大小影响；
- 连接线程执行分配后触发 glibc 多 arena。

### 6.2 glibc malloc arena 保留约 7 GB 地址空间

空闲连接只产生线程栈，连接线程真正处理 JSON、命令或终端输入后会发生堆分配。glibc 会为多个并发分配线程创建 arena；线程退出后这些 arena 可以复用，但对应虚拟地址空间通常不会解除映射。

| 场景 | 基线 `VmSize` | 活跃时 | 连接全部关闭后 | arena 映射 |
|---|---:|---:|---:|---:|
| Node TCP，300 个并发有效请求 | 0.74 GB | 9.85 GB | 7.42 GB | 约 107 |
| Terminal AI，100 个并发命令 | 0.38 GB | 7.75 GB | 6.96 GB | 约 103 |

`/proc/<pid>/maps` 中可见约 103–107 组映射，每组主要包含：

- 约 132 KB 可读写区域；
- 约 65,404 KB `---p` 保留区域。

这不是等量的物理内存泄漏：断连后的 RSS 只比基线高数 MB。但它会影响：

- 有严格虚拟内存限制的容器或服务；
- 32 位平台；
- ASLR/地址空间碎片敏感场景；
- core dump、监控告警和运维判断。

使用以下方式进行对照测试：

```bash
MALLOC_ARENA_MAX=2 build/bin/ve
```

Node TCP 300 并发请求的结果变为：

| 阶段 | `VmSize` | RSS | 线程 |
|---|---:|---:|---:|
| 基线 | 182 MB | 9.4 MB | 13 |
| 300 个连接活跃 | 2.64 GB | 17.5 MB | 313 |
| 全部关闭 | 216 MB | 14.5 MB | 13 |

该设置能抑制 glibc arena 地址空间保留，但不能解决活跃连接的一线程一栈问题，只适合作为 Linux 部署层面的临时缓解。

### 6.3 cleanup 单线程存在队头阻塞

[`ConnectionLoopCleanup::retire()`](../../ve/src/service/server_util.h#L123) 先让连接 loop `quit()`，再把 `stop()` 投递到唯一的 cleanup loop：

```cpp
state->loop->quit();
_loop.post([state = std::move(state)] { state->loop->stop(); });
```

而 [`AsioLoop::stop()`](../../ve/src/core/loop.cpp#L264) 会直接 `join()` worker。这意味着一个不返回的连接任务会阻塞 cleanup loop，后续所有待回收的连接状态都排在它后面。

当前存在两个没有超时的同步等待点：

- Node TCP 的 [`reply.get()`](../../ve/src/service/node_tcp_server.cpp#L140)；
- Terminal 的 [`finished.get()`](../../ve/src/service/terminal_session.cpp#L1237)。

潜在后果：

- 某个命令绑定到已停止或永久阻塞的 loop 时，连接线程不能退出；
- cleanup 队列持续持有断开连接的 `shared_ptr`；
- `ConnectionLoopCleanup::drain()` 一直等待；
- 服务同步停止或析构卡住；
- 如果从当前连接线程内部调用 `stop(true)`，可能形成“连接线程等待 drain，cleanup 线程等待 join 连接线程”的自锁。

本轮正常命令压测没有触发该问题，但从生命周期代码可以确定此风险存在。

### 6.4 Node UDP endpoint session 的 60 秒滞留

Node UDP 对业务层表现为无连接协议，但 asio2 会按远端 IP:port 查找或创建 session：

- [`udp_server.hpp`](../../deps/asio2/include/asio2/udp/udp_server.hpp#L643)
- 默认 [`udp_silence_timeout = 60 * 1000`](../../deps/asio2/include/asio2/base/detail/util.hpp#L216)

干净进程中的第一波结果：

| 累计唯一源端口 | RSS | 相对基线增加 |
|---:|---:|---:|
| 0 | 9.4 MB | 0 |
| 4,597 | 18.3 MB | 8.9 MB |
| 8,389 | 25.6 MB | 16.2 MB |
| 11,605 | 31.8 MB | 22.4 MB |
| 14,299 | 36.9 MB | 27.5 MB |
| 16,581 | 41.3 MB | 31.9 MB |
| 18,449 | 44.8 MB | 35.4 MB |
| 19,990 | 47.7 MB | 38.3 MB |
| 21,315 | 50.3 MB | 40.9 MB |

约合 1.9 KB/唯一 endpoint。

验证结果：

- 复用同一个 UDP socket 连续发送 160,000 次请求，RSS 不增长；
- 等待超过 60 秒后，再创建约 21,000 个不同源端口，RSS 保持 50.3 MB，说明旧 session 已释放且内存被复用；
- glibc 未主动将高水位归还 OS；
- 带着约 21,000 个仍在超时窗口内的 session 停止服务，没有观察到明显关停延迟。

因此它不是永久对象泄漏，但在公网 UDP 场景下，大量不同源 IP/端口可以把 60 秒窗口内的 session 数量放大，形成内存 DoS 风险。

### 6.5 StaticServer 高频 404 日志风暴

[`StaticServer::Private::tryServeFile()`](../../ve/src/service/static_http_server.cpp#L257) 对除少数浏览器探测路径之外的每个文件缺失输出 warning：

```cpp
veLogWs("[static] file missing:", full.string(), ec ? ec.message().c_str() : "");
```

压测 40,000 次不存在路径时，产生了约 4 MB 控制台日志。生产环境中的爬虫、扫描器或错误资源路径可能造成：

- 同步日志 I/O 开销；
- 日志文件快速增长；
- 日志采集与传输压力；
- 真正告警被大量 404 warning 淹没。

本项测试针对连接生命周期和 404 路径，不代表实际静态文件吞吐能力。

## 7. 建议

### 高优先级

1. 将每连接独占 `AsioLoop/std::thread` 改为共享线程池，并为每个连接使用 strand 或等价的串行 executor。这样仍能保证单连接内有序执行，同时避免一连接一线程。
2. 为 Node TCP 和 Terminal 的命令等待增加超时、取消或断连传播机制。
3. 避免让一个阻塞的 `join()` 卡住整个 cleanup 队列；同步停止还应识别当前是否正在目标连接 loop 中执行。
4. 为 UDP session 增加可配置的 silence timeout、endpoint 数量上限和新 endpoint 速率限制。若业务确实是 stateless，应尽量避免为每个 endpoint 保留长生命周期 session。

### 中优先级

1. 架构修改完成前，Linux 部署可临时设置 `MALLOC_ARENA_MAX=2` 或 `GLIBC_TUNABLES=glibc.malloc.arena_max=2`。
2. 在线程创建之前限制最大并发连接数；只在创建线程之后拒绝连接无法避免瞬时资源峰值。
3. StaticServer 的普通 404 日志降为 debug、采样或周期聚合，保留真正的文件读取错误 warning。

## 8. 建议回归验收项

- 300 个 Terminal/Node TCP 连接不再增加 300 个 OS 线程；
- 300 个连接关闭后，`VmSize` 回到基线附近，不保留数 GB arena；
- 100,000 次短连接预热后，RSS、线程和 FD 无持续斜率；
- 一个永久阻塞命令不会阻塞其他连接回收，服务停止有明确超时；
- UDP 大量唯一 endpoint 时内存受配置上限约束；
- 连续 100,000 次普通 404 不逐请求输出 warning；
- 全量单元测试继续保持通过，并增加连接生命周期、阻塞命令和 UDP session 上限测试。
