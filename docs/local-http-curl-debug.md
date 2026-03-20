# VE 框架联调：用 core HTTP 服务读节点（给后续 AI 的说明）

本文站在 **`ve` 服务层** 说明如何调试 VersatileEngine，**不依赖**具体宿主是 `veQtBrowser`、测试程序还是自研 `main`。HTTP 由 **`ve::HttpModule`（`ve.service.http`）** 拉起 **`ve::HttpServer`**（`ve/include/ve/service/http_server.h`，实现于 `ve/src/service/http_server.cpp`），与 Qt 无必然关系。

## 给 AI 的排查原则

1. **先确认 HTTP 是否活着**：`GET /health`。失败则进程未监听、端口错了、或模块未加载，**不必先翻 CMake 日志**。
2. **端口以运行时为准**：默认配置常为 8080，但应用可能改 `ve/entry/module/ve.service.http/config/port`。用下面「读节点」一条命令即可拿到实际端口（若 HTTP 已起来）。
3. **真相在 `ve::Node` 树**：配置在 `ve/entry/...`，各服务模块可在 `ve/entry/module/<key>/runtime/*` 写运行态。用 **`GET /api/node/<path>`** / **`GET /api/tree/<path>`** 拉 JSON，比从构建系统反推可靠。
4. **CMake / 长日志**：只在需要改编译选项或部署路径时再查；日常联调优先 **curl + `/api/*`** 与 **Terminal（若启用）**。

下文记 **`BASE=http://127.0.0.1:<port>`**，`<port>` 默认 **8080**，请按实际替换。

## Windows PowerShell：务必用 `curl.exe`

`curl` 是 `Invoke-WebRequest` 的别名，参数不兼容。请写：

```powershell
curl.exe -s http://127.0.0.1:8080/health
```

Linux / macOS / cmd 一般可直接 `curl`。

## Core 提供的 HTTP 能力（与宿主无关）

| 方法 | 路径 | 作用 |
|------|------|------|
| GET | `/health` | JSON 存活与运行时长 |
| GET | `/api/node/<path>` | 读节点值（路径为 `ve/...` 风格，斜杠分隔） |
| PUT | `/api/node/<path>` | body 为 JSON，写入节点 |
| GET | `/api/tree/<path>` | 导出子树 JSON |
| GET | `/api/children/<path>` | 子节点名字列表 |
| GET | `/` 等 | 若配置了 `static_root`，静态文件；默认首页文件名 `index.html`（见 `HttpModule` 与 `HttpServer::setDefaultFile`） |

模块是否启动、监听端口：看 **`ve/entry/module/ve.service.http/runtime/`**（`port`, `listening`, `static_root` 等，由 `http_module.cpp` 写入）。

## 推荐命令序列（复制即用）

```powershell
# 1) 存活
curl.exe -s http://127.0.0.1:8080/health

# 2) HTTP 服务自身运行时（确认端口与是否在听）
curl.exe -s http://127.0.0.1:8080/api/node/ve/entry/module/ve.service.http/runtime/port
curl.exe -s http://127.0.0.1:8080/api/node/ve/entry/module/ve.service.http/runtime/listening

# 3) 全局入口与已加载模块配置（缩略窥全貌）
curl.exe -s http://127.0.0.1:8080/api/tree/ve/entry/module

# 4) 单点读配置示例：HTTP 静态根（是否为空可知会不会走静态站）
curl.exe -s http://127.0.0.1:8080/api/node/ve/entry/module/ve.service.http/config/static_root
```

若不知道端口且 **8080 连不上**：让用户说明配置或查其 JSON 里 `ve.service.http.config.port`；**不要**默认假设只有 veQtBrowser。

## 与 Terminal / WebSocket 的关系（同属 core 服务模块）

- **`ve.service.terminal`**：`ve/src/module/terminal_module.cpp`，默认 TCP **5061**，REPL 调命令与扫树（与 HTTP 互补）。
- **`ve.service.ws`**：默认 **8081**，推送用，不是 curl 场景。

二者是否启用由 **`ve/entry/module/<key>/enabled`** 与注册模块决定；仍可通过 **`/api/tree/ve/entry/module`** 看当前树里有哪些模块配置。

## 在 Cursor 里让助手代跑 curl

助手可在**你的本机终端**执行 `curl.exe` 访问 `127.0.0.1`，**前提**：目标进程已启动且 HTTP 在监听。这与「编辑仓库文件」无关：改代码不要求服务已开；curl 要求服务已开。

## Qt / veQtBrowser 仅作附录

若宿主是 `veQtBrowser`，可能还有 **`browser` 模块**把 `static_root` 解析后写回 HTTP 配置、并在 `ve/entry/module/browser/runtime/*` 放 `home_url` 等。**那是应用层约定，不是 core HTTP 的通用前提。** 浏览器静态页、CMake 是否拷贝 `webapp` 等见 **[`internal/observability-services.md`](internal/observability-services.md)** 后半与 Qt 文档。

## 实现索引（便于 AI 下钻代码）

- `ve/src/service/http_server.cpp` - 路由与静态文件
- `ve/src/module/http_module.cpp` - 从 `ve/entry/module/ve.service.http/config` 读端口与 `static_root`
- `ve/src/module/terminal_module.cpp` / `ws_module.cpp` - 另两条通道
