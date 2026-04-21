# VE 协议迁移指南

## 协议变更历史

### 2026-04-20: 统一协议重写 (commit 9a559d1)

**变更内容：**

所有网络服务（HTTP/WebSocket/TCP/UDP）统一使用 `dispatchNodeProtocol`，协议字段从 `cmd` 改为 `op`。

**旧协议（3a82003 之前）：**

```json
{"cmd": "set", "path": "test/value", "value": 42}
{"cmd": "subscribe", "path": "test/value"}
```

**新协议（9a559d1 之后）：**

```json
{"op": "node.set", "path": "test/value", "value": 42, "id": 1}
{"op": "subscribe", "path": "test/value", "id": 2}
```

**统一回复格式：**

```json
{"ok": true, "id": 1, "data": {"path": "test/value"}}
{"ok": true, "id": 2, "accepted": true, "task_id": "abcd1234"}
{"ok": false, "id": 3, "code": "not_found", "error": "node not found: foo/bar"}
```

**支持的操作（11 个）：**

| 操作 | 说明 |
|------|------|
| `node.get` | 获取节点值（支持 depth、meta） |
| `node.list` | 列出子节点 |
| `node.set` | 设置节点值 |
| `node.put` | 导入树形结构（自动增删改） |
| `node.remove` | 删除节点 |
| `node.trigger` | 触发节点变更信号 |
| `command.list` | 列出所有命令 |
| `command.run` | 执行命令（支持 wait/async） |
| `subscribe` | 订阅节点变更（WS/TCP only） |
| `unsubscribe` | 取消订阅（WS/TCP only） |
| `batch` | 批量操作 |

---

## 客户端更新状态

### JavaScript

所有 JS 客户端已更新至新协议：

- `ve-sdk/http.ts` (VeHttpClient) - ✅ 完整支持
- `ve-sdk/ws.ts` (VeWsClient) - ✅ 完整支持
- `veservice.js` (vanilla WS) - ✅ 完整支持

### Python

- `ve_client` - ✅ 已更新（commit 7daf089）

### 服务端

所有服务端已统一：

- `NodeHttpServer` (12000) - ✅
- `NodeWsServer` (12100) - ✅
- `NodeTcpServer` (12200) - ✅
- `NodeUdpServer` (12300) - ✅
- `BinTcpServer` (11000) - ✅ (MessagePack envelope)

---

## 迁移检查清单

如果你在使用旧版本的 VE 客户端或服务端，请检查：

### 1. 检查协议字段

❌ 旧代码：
```js
ws.send(JSON.stringify({ cmd: "set", path: "/test", value: 42 }));
```

✅ 新代码：
```js
ws.send(JSON.stringify({ op: "node.set", path: "/test", value: 42, id: 1 }));
```

### 2. 检查回复格式

❌ 旧代码期望：
```json
{"path": "/test", "value": 42}
```

✅ 新代码期望：
```json
{"ok": true, "id": 1, "data": {"path": "/test", "value": 42}}
```

### 3. 更新客户端库

- JS: 使用 `ve/js/veservice.js` 或 `@ve/sdk` 最新版本
- Python: 使用 `ve_client` 最新版本（需要 `httpx` 或 `websockets`）

### 4. 检查构建输出

如果你的项目使用了 build 输出目录里的 `veservice.js`，确保重新构建：

```bash
cmake --build build --target ve_program
```

构建后的文件在：
- `build/bin/webapp/veservice.js`
- `build/bin/webapp/index.html`

---

## 常见问题

### Q: 为什么我的旧客户端连不上新服务端？

A: 新服务端要求 `op` 字段，旧客户端发送的是 `cmd` 字段。请更新客户端代码或使用最新的 `veservice.js`。

### Q: 如何判断服务端是新协议还是旧协议？

A: 发送测试请求：

```bash
# 新协议
curl -X POST http://localhost:12000/ve \
  -H "Content-Type: application/json" \
  -d '{"op":"node.get","path":"","id":1}'

# 如果返回 {"ok":true,...} 就是新协议
# 如果返回 {"error":"unknown op"} 或连接失败，可能是旧协议
```

### Q: `node.put` 和 `node.set` 有什么区别？

A: 
- `node.set` - 只设置单个节点的值
- `node.put` - 导入整个树形结构，自动增删改子节点（`auto_insert/auto_remove/auto_update`）

### Q: `batch` 操作有什么限制？

A: 默认限制 500 个操作/批次（可通过 `batchLimit` 参数调整）。批量操作会返回数组：

```json
{
  "ok": true,
  "id": 1,
  "data": {
    "items": [
      {"ok": true, "data": {...}},
      {"ok": false, "code": "not_found", "error": "..."}
    ]
  }
}
```

---

## 相关文档

- [SERVICE.md](SERVICE.md) - 完整服务端协议说明
- [ve/js/README.md](../ve/js/README.md) - JS 客户端使用指南
- [ve/py/README.md](../ve/py/README.md) - Python 客户端使用指南
