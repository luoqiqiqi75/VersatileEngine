# VE JavaScript / TypeScript 客户端

本目录包含 VE 运行时的 JS / TS 客户端与相关工具。核心原则只有一句：

**HTTP / WS / BinTcp 只是不同 transport，主语义都围绕同一套 VE 协议。**

---

## 包一览

| 路径 | 说明 |
|------|------|
| `ve-sdk/` | TypeScript SDK：`VeHttpClient`、`VeWsClient`、`VeBinTcpClient` |
| `veservice.js` | 浏览器直接用的 WebSocket 客户端 |
| `ve-mcp/` | Cursor MCP 适配器，内部通过 `POST /ve` 调用 VE |
| `ve-app/` | React 管理端 |

---

## HTTP 面

NodeHttpServer 现在有四类入口：

- `GET /health`
- `GET/POST/PUT/DELETE /at/<path>`
- `POST /cmd/<name>`
- `POST /ve`
- `POST /jsonrpc`

职责划分：

- `/at/<path>`：便于浏览器 / curl 直接读写 schema JSON
- `/cmd/<name>`：便于浏览器 / curl 直接运行 command
- `/ve`：VE 原生协议主入口
- `/jsonrpc`：给不用 VE 原生协议的标准客户端

### `/at/<path>`

- `GET /at/<path>`：导出节点或子树
- `POST /at/<path>`：设置值，或通过 `?trigger=1` 触发
- `PUT /at/<path>`：把 raw JSON 导入为子树
- `DELETE /at/<path>`：删除节点

示例：

```bash
curl http://127.0.0.1:12000/at/ve/server
curl http://127.0.0.1:12000/at/ve/server?children=1
curl http://127.0.0.1:12000/at/ve/server?structure=1
curl -X POST http://127.0.0.1:12000/at/test/value -H "Content-Type: application/json" -d '42'
curl -X POST "http://127.0.0.1:12000/at/test/value?trigger=1"
curl -X PUT "http://127.0.0.1:12000/at/test/tree?auto_remove=1" -H "Content-Type: application/json" -d '{"a":1,"b":2}'
curl -X DELETE http://127.0.0.1:12000/at/test/tree
```

`GET /at/<path>` 支持：

- `auto_ignore=0|1`
- `depth=<n>`
- `children=1`
- `structure=1`
- `meta=1`

`PUT /at/<path>` 支持：

- `auto_insert=0|1`
- `auto_remove=0|1`
- `auto_update=0|1`

### `/cmd/<name>`

用于人类友好地直接调用 command。

```bash
curl -X POST http://127.0.0.1:12000/cmd/search \
  -H "Content-Type: application/json" \
  -d '["port", "/", "--top", "5"]'

curl -X POST "http://127.0.0.1:12000/cmd/save?context=ve/server&async=1" \
  -H "Content-Type: application/json" \
  -d '{"args":["json","node","-f","server.json"]}'
```

### `/ve`

`POST /ve` 的 body 是统一 envelope：

```json
{
  "op": "node.get",
  "id": 1,
  "path": "ve/server/node/http",
  "depth": 1,
  "meta": true
}
```

常用 `op`：

- `node.get`
- `node.list`
- `node.set`
- `node.put`
- `node.remove`
- `node.trigger`
- `command.list`
- `command.run`
- `subscribe`
- `unsubscribe`
- `batch`

统一回复：

```json
{"ok":true,"id":1,"data":{...}}
{"ok":true,"id":1,"accepted":true,"task_id":"abcd1234"}
{"ok":false,"id":1,"code":"not_found","error":"node not found: foo/bar"}
```

---

## `@ve/sdk`

### 1. 本地路径引用

```json
{
  "dependencies": {
    "@ve/sdk": "file:../VersatileEngine/ve/js/ve-sdk"
  }
}
```

然后先构建：

```bash
cd ve/js/ve-sdk
npm install
npm run build
```

### 2. 最小示例

```ts
import { VeHttpClient } from '@ve/sdk';

const ve = new VeHttpClient('http://127.0.0.1:12000');

await ve.health();

const node = await ve.getNode('ve/server/node/http/runtime/port', { meta: true });
const tree = await ve.getTree('ve/server');
const cmds = await ve.listCommands();

const run = await ve.runCommand('save', {
  args: ['json', '/ve', '-f', 've.json'],
  wait: true,
});
```

### 3. `VeHttpClient`

`VeHttpClient` 的核心方法是：

- `call(op, payload)`：直接打 `/ve`

上层保留了这些薄封装：

- `getNode(path, options)`
- `listNodes(path, meta?)`
- `setNode(path, value)`
- `triggerNode(path)`
- `removeNode(path)`
- `getTree(path, depth?)`
- `exportTree(path, depth?)`
- `importTree(path, json)`
- `listCommands()`
- `runCommand(name, { args, wait, id })`

说明：

- `runCommand()` 默认建议显式传 `wait`
- 如果服务端返回 `accepted + task_id`，说明命令已经被受理，后续结果落在 `ve/server/tasks/<task_id>`

---

## `VeWsClient`

WebSocket 协议和 `/ve` 使用同一套 envelope，只是 transport 换成了 WS。

请求示例：

```json
{"op":"node.get","path":"ve/server","id":1}
{"op":"command.run","name":"save","args":["json","/ve"],"wait":false,"id":2}
{"op":"subscribe","path":"ve/server/node/http/runtime/port","id":3}
```

推送示例：

```json
{"event":"node.changed","path":"ve/server/node/http/runtime/port","value":12000}
{"event":"task.result","id":2,"task_id":"abcd1234","ok":true,"data":"Saved"}
```

TypeScript 示例：

```ts
import { VeWsClient } from '@ve/sdk';

const ws = new VeWsClient({ url: 'ws://127.0.0.1:12100' });
ws.connect();

ws.onMessage((msg) => {
  if ('event' in msg && msg.event === 'task.result') {
    console.log('task result:', msg);
  }
});

const unsub = ws.subscribe('ve/server/node/http/runtime/port', (path, value) => {
  console.log(path, value);
});
```

---

## `VeBinTcpClient`

`VeBinTcpClient` 仍然走 MessagePack frame，但 payload 语义已经和 `/ve` / WS 对齐。

Frame：

```text
[flag:1][length:4 LE][payload: MessagePack]
```

Flag：

| Flag | Value | Direction |
|------|-------|-----------|
| REQUEST | `0x00` | client → server |
| RESPONSE | `0x40` | server → client |
| NOTIFY | `0x80` | server → client |
| ERROR | `0xC0` | server → client |

示例：

```ts
import { VeBinTcpClient } from '@ve/sdk';

const client = new VeBinTcpClient({ host: '127.0.0.1', port: 11000 });
await client.connect();

const port = await client.get('ve/server/node/http/runtime/port');
await client.set('test/value', 42);

const run = await client.command('save', ['json', '/ve'], true);

const unsub = await client.subscribe('ve/server/node/http/runtime/port', (path, value) => {
  console.log(path, value);
});
```

---

## 只有浏览器脚本、不用打包器时

页面里直接引入：

```html
<script src="path/to/veservice.js"></script>
<script>
  _ve_ws_url = 'ws://127.0.0.1:12100';
</script>
```

全局会有 `veService`，它使用新的 WS envelope，并提供：

- `get(path)`
- `set(path, value)`
- `trigger(path)`
- `subscribe(path, callback, immediateGet?)`
- `command(name, args, options?)`
- `call(op, payload)`

---

## 选型建议

- 只读/调试/curl：优先 `/at/<path>`
- 业务 HTTP 集成：优先 `/ve`
- 标准客户端：用 `/jsonrpc`
- 浏览器实时订阅：用 `VeWsClient` 或 `veservice.js`
- Node.js 高频通信：用 `VeBinTcpClient`

---

## 与 `AGENTS.md` / MCP 的关系

- `ve-mcp` 通过 `POST /ve` 的 `command.list` / `command.run` 暴露 Cursor 工具
- 健康检查仍然看 `/health`
- 运行时端口与状态优先从 `GET /at/ve/server/...` 读取
