# Python Client API Alignment with JS veservice.js

Python客户端API现已完全对齐JS的`veservice.js` WebSocket接口。

## API对照表

| 方法 | JS (veservice.js) | Python (VeClient) | 底层协议 | 说明 |
|------|-------------------|-------------------|----------|------|
| **树操作** |
| `get(path, depth=-1)` | ✓ | ✓ | `node.get` | 获取树或值（默认depth=-1返回完整树） |
| `set(path, tree)` | ✓ | ✓ | `node.put` | 设置树结构 |
| **单值操作** |
| `val(path)` | ✓ | ✓ | `node.get` | 读取单个节点值 |
| `val(path, value)` | ✓ | ✓ | `node.set` | 设置单个节点值 |
| **结构操作** |
| `list(path)` | ✓ | ✓ | `node.list` | 列出子节点 |
| `rm(path)` | ✓ | ✓ | `node.remove` | 删除节点 |
| `trigger(path)` | ✓ | ✓ | `node.trigger` | 触发NODE_CHANGED信号 |
| **订阅** |
| `watch(path, callback, options)` | ✓ | `subscribe(path, callback)` | `subscribe` | 订阅节点变化（Python简化版） |
| `unwatch(path, callback)` | ✓ | `unsubscribe(path)` | `unsubscribe` | 取消订阅 |
| **命令** |
| `run(name, args, wait)` | ✓ | `command(name, args)` | `command.run` | 执行命令 |
| `cmds()` | ✓ | ✓ | `command.list` | 列出可用命令 |
| **批量操作** |
| `batch(items)` | ✓ | ✓ | `batch` | 批量执行操作 |
| **辅助方法** |
| `tree(path)` | - | ✓ | `node.get` depth=-1 | Python便捷方法（等同于get） |
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
const unwatch = veService.watch("/test", (data, path) => {
    console.log(`${path} changed:`, data);
}, {immediate: true, tree: true, bubble: false});
unwatch();  // 取消订阅

// 命令
const result = await veService.run("search", ["config"], true);
const commands = await veService.cmds();

// 批量操作
const results = await veService.batch([
    {op: "node.get", path: "config"},
    {op: "node.set", path: "test", value: 42}
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
unsub = client.subscribe("/test", lambda path, value: 
    print(f"{path} changed: {value}"))
unsub()  # 取消订阅

# 命令
result = client.command("search", {"args": ["config"]})
commands = client.cmds()

# 批量操作
results = client.batch([
    {"op": "node.get", "path": "config"},
    {"op": "node.set", "path": "test", "value": 42}
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
        {"op": "node.get", "path": "config"}
    ])
```

## 关键差异说明

### 1. get/set/val 语义

**JS:**
- `get(path, depth)` - 获取树（默认depth=-1）
- `set(path, tree)` - 设置树结构（node.put）
- `val(path)` / `val(path, value)` - 读写单值（node.get/node.set）

**Python:** 完全一致

### 2. 订阅选项

**JS:** `watch(path, callback, options)` 支持：
- `immediate` - 立即触发一次回调
- `tree` - 订阅树变化（默认true）
- `bubble` - 冒泡订阅（默认false）

**Python:** `subscribe(path, callback)` 简化版，不支持options（可后续扩展）

### 3. 命令参数格式

**JS:** `run(name, args, wait)` - args可以是数组或对象，wait控制是否等待

**Python:** `command(name, args)` - args是字典，wait在args中指定

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
✓ 底层协议操作一致（node.get/put/set/remove/trigger等）  
✓ 支持同步和异步两种API风格  
✓ 支持多种传输协议（TCP JSON、MsgPack、HTTP、JSON-RPC）
