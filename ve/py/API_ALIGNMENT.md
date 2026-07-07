# Python Client API Alignment with JS veservice.js

Python客户端API现已完全对齐JS的`veservice.js` WebSocket接口。

## API对照表

| 方法 | JS (veservice.js) | Python (VeClient) | 底层协议 | 说明 |
|------|-------------------|-------------------|----------|------|
| **树操作** |
| `get(path, depth=-1)` | ✓ | ✓ | `export` | 获取树或值（默认depth=-1返回完整树） |
| `set(path, tree)` | ✓ | ✓ | `import` | 设置树结构（merge） |
| **单值操作** |
| `val(path)` | ✓ | ✓ | `get` | 读取单个节点值 |
| `val(path, value)` | ✓ | ✓ | `set` | 设置单个节点值 |
| **结构操作** |
| `list(path)` | ✓ | ✓ | `children` | 列出子节点 |
| `rm(path)` | ✓ | ✓ | `erase` | 删除节点 |
| `trigger(path)` | ✓ | ✓ | `trigger` | 触发NODE_CHANGED信号 |
| **订阅** |
| `subscribe(path, callback, options)` | ✓ | `subscribe(path, callback, depth/once/immediate)` | `subscribe` | 订阅节点变化 |
| `unsubscribe(path, callback)` | ✓ | `unsubscribe(path)` | `unsubscribe` | 取消订阅 |
| **命令** |
| `run(name, args)` | ✓ | `command(name, args)` | `cmd` 字段 | 执行命令 |
| `cmds()` | ✓ | `commands()` | `commands` | 列出可用命令 |
| `send({op, params})` | ✓ | `op(name, **params)` | 任意 op | 透传 v2.1 信封，可达服务端新增 op |
| - | - | `describe(name)` | `describe` | 取命令的 instruction 子树（usage / input_schema 等） |
| **批量操作** |
| `batch(items)` | ✓ | ✓ | `batch` 顶层字段 | 批量执行操作 |
| **辅助方法** |
| `tree(path)` | - | ✓ | `export` depth=-1 | Python便捷方法（等同于get） |
| `ping()` | - | ✓ | - | 测试连接 |
| `close()` | - | ✓ | - | 关闭连接 |

## 使用示例对比

### JavaScript (veservice.js)

```javascript
// 连接
await veService.connect();

// 树操作
const tree = await veService.get("/config");           // 获取树（depth=-1）
await veService.set("/config", {port: 8080});          // 设置树结构

// 单值操作
const port = await veService.val("/config/port");      // 读取单值
await veService.val("/test", 42);                      // 设置单值

// 结构操作
const children = await veService.list("/");            // 列出子节点
await veService.rm("/test");                           // 删除节点
await veService.trigger("/config");                    // 触发信号

// 订阅
const unsub = veService.subscribe("/test", (data, path) => {
    console.log(`${path} changed:`, data);
}, {immediate: true, depth: -1});
unsub();  // 取消订阅

// 命令
const result = await veService.run("search", ["config"]);
const commands = await veService.commands();

// 批量操作
const results = await veService.batch([
    {op: "get", params: {path: "config"}},
    {op: "set", params: {path: "test", value: 42}}
]);
```

### Python (VeClient)

```python
# 连接（自动连接）
client = VeClient("http://localhost:12000")

# 树操作
tree = client.get("/config")                           # 获取树（depth=-1）
client.set("/config", {"port": 8080})                  # 设置树结构

# 单值操作
port = client.val("/config/port")                      # 读取单值
client.val("/test", 42)                                # 设置单值

# 结构操作
children = client.list("/")                            # 列出子节点
client.rm("/test")                                     # 删除节点
client.trigger("/config")                              # 触发信号

# 订阅（仅TCP JSON和MsgPack支持）
unsub = client.subscribe("/test", lambda path, data: 
    print(f"{path} changed: {data}"))
unsub()  # 取消订阅

# 命令
result = client.command("search", {"pattern": "config"})
commands = client.cmds()

# 批量操作
results = client.batch([
    {"op": "get", "params": {"path": "config"}},
    {"op": "set", "params": {"path": "test", "value": 42}}
])

# 关闭连接
client.close()
```

### Python Async (AsyncVeClient)

```python
# 异步版本
async with AsyncVeClient("http://localhost:12000") as client:
    tree = await client.get("/config")
    await client.val("/test", 42)
    port = await client.val("/config/port")
    await client.set("/config", {"port": 8080})
    
    children = await client.list("/")
    await client.rm("/test")
    await client.trigger("/config")
    
    result = await client.command("search", {"args": ["config"]})
    commands = await client.cmds()
    
    results = await client.batch([
        {"op": "get", "params": {"path": "config"}}
    ])
```

## 关键差异说明

### 1. get/set/val 语义

**JS:**
- `get(path, depth)` - 获取树（默认depth=-1，底层 `export`）
- `set(path, tree)` - 设置树结构（底层 `import`）
- `val(path)` / `val(path, value)` - 读写单值（底层 `get`/`set`）

**Python:** 完全一致

### 2. 订阅选项

**JS:** `subscribe(path, callback, options)` 支持：
- `depth` - 推送深度（-1 全树 / 0 仅值 / N 层，默认 -1）
- `once` - 单次推送后自动退订（默认 false）
- `immediate` - 订阅回复中带当前状态（默认 false）

**Python:** `subscribe(path, callback, depth=-1, once=False, immediate=False)` 完全一致

### 3. 命令参数格式

**JS:** `run(name, args)` - args 为对象（params），不再有 wait

**Python:** `command(name, args)` - args 为字典，直接作为 params

### 4. 传输协议支持

**Python支持4种传输协议：**
- `tcp` (TCP JSON, port 12200) - 默认，支持订阅
- `msgpack` (MessagePack, port 11000) - 高性能，支持订阅
- `http` (HTTP REST, port 12000) - 不支持订阅
- `jsonrpc` (JSON-RPC 2.0, port 12000) - 不支持订阅

**JS只支持WebSocket (port 12100)**

## 迁移指南

如果你之前使用的是旧版Python API：

```python
# 旧版（已废弃）
value = client.get("/config/port")  # 返回单值
client.set("/test", 42)             # 设置单值

# 新版（对齐JS）
value = client.val("/config/port")  # 读取单值
client.val("/test", 42)             # 设置单值

tree = client.get("/config")        # 获取树（depth=-1）
client.set("/config", {"port": 8080})  # 设置树结构
```

## 完整性检查

✓ 所有JS veservice.js的核心方法都已在Python中实现  
✓ 方法签名和语义完全对齐  
✓ 底层协议操作一致（Envelope v2.1：get/set/export/import/children/erase/trigger 等）  
✓ 支持同步和异步两种API风格  
✓ 支持多种传输协议（TCP JSON、MsgPack、HTTP、JSON-RPC）
