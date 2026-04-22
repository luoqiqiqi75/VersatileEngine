# ROS Service Call 使用说明

## 功能概述

`ros/service/call` 命令用于调用 ROS 服务。支持通过 HTTP、WebSocket 和 Terminal 接口调用。

## 命令格式

```bash
ros/service/call <service> <type> <request> [payload_format]
```

### 参数说明

- `service` - 服务名称（必需），例如 `/add_two_ints`
- `type` - 服务类型（必需），例如 `example_interfaces/srv/AddTwoInts`
- `request` - 请求数据（必需），YAML 或 Var 格式
- `payload_format` - 数据格式（可选，默认 `yaml`）
  - `yaml` - YAML 格式（推荐）
  - `var` - Var 格式（JSON 兼容）

## 使用示例

### 1. Terminal 调用

```bash
# 调用加法服务
ros/service/call /add_two_ints example_interfaces/srv/AddTwoInts "{a: 10, b: 20}"

# 使用 var 格式
ros/service/call /add_two_ints example_interfaces/srv/AddTwoInts '{"a":10,"b":20}' var
```

### 2. HTTP API 调用

```bash
curl -X POST http://localhost:12000/api/cmd/ros/service/call \
  -H "Content-Type: application/json" \
  -d '{
    "args": [
      "/add_two_ints",
      "example_interfaces/srv/AddTwoInts",
      "{a: 10, b: 20}",
      "yaml"
    ]
  }'
```

### 3. WebSocket 调用

```javascript
// 使用 veservice.js
veService.command("ros/service/call", {
  service: "/add_two_ints",
  type: "example_interfaces/srv/AddTwoInts",
  request: "{a: 10, b: 20}",
  payload_format: "yaml"
});
```

### 4. Python 客户端调用

```python
from ve_client import VEClient

client = VEClient("http://localhost:12000")

# 调用服务
result = client.command("ros/service/call", {
    "service": "/add_two_ints",
    "type": "example_interfaces/srv/AddTwoInts",
    "request": "{a: 10, b: 20}",
    "payload_format": "yaml"
})

print(result)
```

## 返回格式

成功响应：

```json
{
  "ok": true,
  "message": "service call ok",
  "service": "/add_two_ints",
  "type": "example_interfaces/srv/AddTwoInts",
  "payload_format": "yaml",
  "response": {
    "sum": 30
  },
  "yaml": "sum: 30\n"
}
```

失败响应：

```json
{
  "ok": false,
  "message": "service not available: /add_two_ints",
  "service": "/add_two_ints",
  "type": "example_interfaces/srv/AddTwoInts"
}
```

## 后端支持

- **rclcpp** - 完整支持（ROS2 官方后端）
- **fastdds** - 暂不支持（返回 "not implemented in v1"）

## 相关命令

- `ros/service/list [filter]` - 列出所有服务
- `ros/service/info <name>` - 查看服务详情
- `ros/info` - 查看 ROS 后端状态

## 注意事项

1. 服务类型必须正确匹配，否则序列化会失败
2. 请求数据格式必须符合服务定义的 Request 消息结构
3. 默认超时时间为 10 秒
4. 调用结果会保存到 `ve/ros/service_calls/last` 节点
5. 目前不支持 `cdr_hex` 格式的请求数据
