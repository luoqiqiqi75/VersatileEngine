# Cursor 使用 MCP 连接 VersatileEngine

本文档给出在 Cursor 中接入 VE 的最短可用流程。

## 1. 先启动 VE 的 HTTP 服务

确保配置里 HTTP 已开启（默认端口 `8080`）：

- 文件：`ve/program/ve.json`
- 关键项：`ve.service.http.enabled = true`

启动 VE（示例）：

```powershell
.\ve.exe .\ve.json
```

可选健康检查：

```powershell
curl.exe http://127.0.0.1:8080/health
```

## 2. 方案 A（推荐）：TypeScript MCP 适配器

仓库已提供 `ve/js/ve-mcp`：

```powershell
cd ve/js/ve-mcp
npm install
npm run build
```

其行为：

- `tools/list` -> `GET /api/cmd`
- `tools/call` -> `POST /api/cmd/{name}`

## 3. 在 Cursor 配置 MCP Server

在 Cursor MCP 配置中添加（示例）：

```json
{
  "mcpServers": {
    "ve": {
      "command": "node",
      "args": [
        "D:/workspace/github/VersatileEngine/ve/js/ve-mcp/dist/index.js"
      ],
      "env": {
        "VE_HTTP_BASE": "http://127.0.0.1:8080"
      }
    }
  }
}
```

配置后重载 Cursor / 重新连接 MCP，模型应能看到 VE 的工具列表（来自 `/api/cmd`）。

## 4. 方案 B（可选）：C++ 独立 `ve_mcp`

仓库也提供了独立可执行 `ve_mcp`（stdio MCP）：

- CMake 开关：`VE_BUILD_MCP=ON`
- 目标：`ve/program/mcp/main.cpp`

Cursor 配置可改为直接启动 `ve_mcp.exe`（并传入 `--config`）。

## 5. Cursor 内验证

可以在聊天里让模型执行类似请求：

- “列出 VE 可用命令工具”
- “调用 `help`”
- “调用 `ls`，参数为根路径”

若工具调用成功，说明 MCP 链路已打通。

## 6. 常见问题

- **看不到工具列表**
  - 检查 VE 是否启动、`/api/cmd` 是否可访问。
  - 检查 `VE_HTTP_BASE` 端口是否一致。
- **调用报 `unknown command`**
  - 先用 `/api/cmd` 看当前命令清单。
  - 确认命令名大小写与参数结构。
- **Cursor 提示 MCP server 启动失败**
  - 检查 `node` 路径与 `dist/index.js` 绝对路径。
  - 在终端先手动运行 `node dist/index.js` 看报错。

