# VE JavaScript / TypeScript 客户端

本目录包含与 **VE 运行时**（HTTP / WebSocket）对接的脚本与 SDK。C++ 模块与逻辑仍在 VE 进程内；TS/JS 只是 **同一台机器或网络上的客户端**，通过协议访问 `node` 树与已注册 `command`。

---

## 包一览

| 路径 | 说明 |
|------|------|
| `ve-sdk/` | TypeScript 库：`VeHttpClient`（`fetch`）、`VeWsClient`（薄封装）。构建后发布名 **`@ve/sdk`**（见该目录 `package.json`）。 |
| `veservice.js` | 浏览器用 **WebSocket** 客户端（Promise、`get` / `command` / `subscribe`），协议与 `node_ws_server` JSON 一致。 |
| `ve-mcp/` | Cursor MCP 适配器，内部用 HTTP 调 `/api/cmd`。 |
| `ve-app/` | React 管理端（独立前端工程）。 |

---

## npm / pnpm 如何引入 `@ve/sdk`

### 1. 在 monorepo 里用本地路径（开发常用）

在业务项目的 `package.json` 中：

```json
{
  "dependencies": {
    "@ve/sdk": "file:../VersatileEngine/ve/js/ve-sdk"
  }
}
```

路径按你放仓库的位置调整。然后：

```bash
npm install
# 或
pnpm install
```

**先构建 SDK**（生成 `dist/` 与类型声明）：

```bash
cd ve/js/ve-sdk
npm install
npm run build
```

### 2. 复制 / 发布到私有 registry 后

对 `ve-sdk` 改版本号、`npm publish`（或 Verdaccio），业务侧正常：

```bash
pnpm add @ve/sdk
```

### 3. 最小使用示例（Node 18+ 或带 `fetch` 的环境）

```ts
import { VeHttpClient } from '@ve/sdk';

const ve = new VeHttpClient('http://127.0.0.1:12000');

await ve.health();
const nodes = await ve.getNode('ve/entry/module');
await ve.runCommand('save', { args: ['json', '/ve'], wait: false });
```

端口以 **`ve` 配置**为准（默认见 `ve/program/ve.json` 里 `ve/server/node/http/config/port`）。

---

## `VeHttpClient` 和 `runCommand` 是什么？

- **`VeHttpClient`**：`ve-sdk/src/http.ts` 里的一个 **薄封装**，用浏览器 / Node 的 **`fetch`** 调 VE 已提供的 REST 接口，例如：
  - `GET /health`
  - `GET/PUT /api/node/...`
  - `GET /api/tree/...`、`POST /api/tree/...`
  - **`GET /api/cmd`**（列出命令）
  - **`POST /api/cmd/{命令名}`**（执行命令）

- **`runCommand(cmdKey, body)`** 对应 **`POST /api/cmd/{cmdKey}`**，请求体里可带：
  - `args`：命令参数（一般为字符串数组，与终端里一致）
  - `wait`：`true` 时在服务端等到 pipeline 结束再返回；`false`（默认）时可能 **202 Accepted**，仅表示已受理（异步在 `loop::main()` 上跑）

它 **不是** 新协议，只是 TS 里类型安全的 HTTP 调用。

---

## 只有浏览器脚本、不用打包器时

页面里：

```html
<script src="path/to/veservice.js"></script>
<script>
  _ve_ws_url = 'ws://127.0.0.1:12100';
</script>
```

全局会有 `veService`（自动 `connect`）。需要 **HTTP** 时可再引入你自己打包的一小段 `fetch`，或单独用 `ve-sdk` 打成一个 bundle。

---

## HTTP 性能与「本机两个服务」

- **同一台机器、127.0.0.1**：走 **loopback**，不经过物理网卡，延迟通常是 **亚毫秒～几毫秒** 量级；开销主要在 **JSON 序列化、VE 内处理命令、日志**，而不是「HTTP 协议本身很慢」。
- **可以用 TS**：本地一个 Node 服务用 `VeHttpClient` 调 VE，完全可行；很多集成场景 **瓶颈在业务逻辑**，不在 HTTP 这一跳。
- **何时考虑 WebSocket**：需要 **推送**（订阅节点变化）、或希望 **长连接少握手**、或高频小消息时，可再用 **`veservice.js` 协议**（或自写与 `node_ws_server` 相同的 JSON）连 `ws://...`。VE 进程里 HTTP 与 WS **并行**，按场景选用即可。
- **若将来要极致 IPC**：那是另一条路线（命名管道、共享内存、自定义 DLL 导出），与「用 TS 写服务」不矛盾；当前官方集成路径就是 **HTTP + 可选 WS**。

---

## 与 `AGENTS.md` / MCP 的关系

- **MCP**（`ve/js/ve-mcp`）也是通过 **HTTP** 调同一套 `/api/cmd`，适合编辑器工具链。
- 调试端口、健康检查仍以仓库根目录 **`AGENTS.md`** 为准；默认端口以运行时 **`/api/node/.../runtime/port`** 为准。
