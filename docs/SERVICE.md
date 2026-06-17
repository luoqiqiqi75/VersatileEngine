# VersatileEngine Service Reference

VE provides multiple network services for accessing the same shared `ve::Node` tree. Different transports expose different ergonomics, but the core semantics are unified.

## Port numbering

Default listen ports use **five-digit decimal** values. The **first three digits** identify the service (fixed), and the **last two digits** are retry suffixes when the base port is busy.

| First three digits | Typical range | Service |
|-------------------|---------------|---------|
| `100` | 10000–10099 | Terminal REPL (TCP) |
| `110` | 11000–11099 | Bin TCP |
| `120` | 12000–12099 | Node HTTP |
| `121` | 12100–12199 | Node WebSocket |
| `122` | 12200–12299 | Node TCP |
| `123` | 12300–12399 | Node UDP |
| `124` | 12400–12499 | Static file server |

Always read `runtime/port` from the node tree if the configured base port might have fallen back.

## Services Overview

| Service | Transport | Port | Protocol | Subscribe | Use Case |
|---------|-----------|------|----------|-----------|----------|
| NodeHttpServer | HTTP | 12000 | `/at` + `/cmd` + `/ve` + `/jsonrpc` | No | Browser, curl, service integration |
| NodeWsServer | WebSocket | 12100 | JSON envelope | Yes | Real-time web apps |
| NodeTcpServer | TCP | 12200 | JSON envelope + newline | Yes | Scripts, embedded tools |
| NodeUdpServer | UDP | 12300 | JSON envelope datagram | No | Fire-and-forget |
| BinTcpServer | TCP | 11000 | MessagePack envelope frames | Yes | High-performance IPC |
| TerminalReplServer | TCP | 10000 | Text commands | No | Interactive debugging |
| StaticServer | HTTP | 12400 | Static files + proxy | No | Frontend dist hosting |

---

## NodeHttpServer

Port `12000`. This server now has **four** API surfaces:

- `GET /health`
- `GET/POST/PUT/DELETE /at/<path>`
- `POST /cmd/<name>`
- `POST /ve`
- `POST /jsonrpc`

### `GET /health`

```bash
curl http://localhost:12000/health
# {"status":"ok","uptime_s":42}
```

### `/at/<path>`

`/at` is the convenience layer for browser / curl users.

- `GET /at/<path>` — export node or subtree as schema JSON
- `POST /at/<path>` — set node value; empty body or `?trigger=1` triggers without changing value
- `PUT /at/<path>` — import subtree from raw JSON body
- `DELETE /at/<path>` — remove a node

Examples:

```bash
curl http://localhost:12000/at/ve/server

curl http://localhost:12000/at/ve/server?depth=1
curl http://localhost:12000/at/ve/server?children=1
curl http://localhost:12000/at/ve/server/node/http?meta=1
curl http://localhost:12000/at/ve/server?structure=1
curl http://localhost:12000/at/ve/server?auto_ignore=0

curl -X POST http://localhost:12000/at/test/value \
  -H "Content-Type: application/json" \
  -d '42'

curl -X POST "http://localhost:12000/at/test/value?trigger=1"

curl -X PUT http://localhost:12000/at/test/tree \
  -H "Content-Type: application/json" \
  -d '{"a":1,"b":2}'

curl -X PUT "http://localhost:12000/at/test/tree?auto_remove=1" \
  -H "Content-Type: application/json" \
  -d '{"a":1}'

curl -X DELETE http://localhost:12000/at/test/tree
```

Supported query parameters:

- `GET /at/<path>`
  - `auto_ignore=0|1`
  - `depth=<n>`
  - `children=1`
  - `structure=1`
  - `meta=1`
- `PUT /at/<path>`
  - `auto_insert=0|1`
  - `auto_remove=0|1`
  - `auto_update=0|1`
- `POST /at/<path>`
  - `trigger=1`

### `POST /cmd/<name>`

Human-friendly command entry for browser/curl testing.

- request body may be JSON array / object / scalar
- `?async=1` runs asynchronously and returns `task_id`
- `?context=<path>` sets the command current node before argument parsing

Examples:

```bash
curl -X POST http://localhost:12000/cmd/search \
  -H "Content-Type: application/json" \
  -d '["port", "/", "--top", "5"]'

curl -X POST "http://localhost:12000/cmd/save?context=ve/server&async=1" \
  -H "Content-Type: application/json" \
  -d '{"args":["json","node","-f","server.json"]}'
```

### `POST /ve`

`/ve` is the VE native protocol endpoint.

Envelope v2.1: requests use the top-level keys `op` (std operation), `cmd` (user
command), or `batch` (array), with all operation arguments nested under `params`.
`id` stays at the top level and is echoed back.

Request:

```json
{
  "op": "export",
  "id": 1,
  "params": { "path": "ve/server/node/http", "depth": 1 }
}
```

Response:

```json
{"id":1,"code":0,"data":{...}}                            // success (code >= 0)
{"id":1,"code":0}                                         // async accepted (Result::accept) -> HTTP 202
{"id":1,"code":-3,"message":"node not found: foo/bar"}    // failure (code < 0)
```

`code >= 0` is success, `code < 0` is failure (`-2` invalid, `-3` not found,
`-4` unsupported); `message` is present only on failure.

Common operations (all arguments go in `params`):

| Op | Key params | Description |
|----|------------|-------------|
| `get` | `path` | Read one node value |
| `set` | `path`, `value` | Set raw node value |
| `export` | `path`, `depth?` | Export subtree (`depth`: `-1` full, `0` value only, `N` levels) |
| `import` | `path`, `tree`, `flags?` | Import subtree (`flags` `9` merge / `11` replace) |
| `children` | `path` | List direct children |
| `erase` | `path` | Remove node |
| `trigger` | `path` | Fire `NODE_CHANGED` without changing value |
| `commands` | none | List registered commands |
| `subscribe` | `path`, `depth?`, `once?`, `immediate?` | Stateful transports only |
| `unsubscribe` | `path` | Stateful transports only |

User commands use the `cmd` field instead of an `op`; batch uses the top-level
`batch` array (each item is its own `op`/`cmd` request).

Examples:

```bash
curl -X POST http://localhost:12000/ve \
  -H "Content-Type: application/json" \
  -d '{"op":"get","id":1,"params":{"path":"ve/server/node/http/runtime/port"}}'

curl -X POST http://localhost:12000/ve \
  -H "Content-Type: application/json" \
  -d '{"op":"set","id":2,"params":{"path":"test/value","value":42}}'

curl -X POST http://localhost:12000/ve \
  -H "Content-Type: application/json" \
  -d '{"op":"commands","id":3,"params":{}}'

curl -X POST http://localhost:12000/ve \
  -H "Content-Type: application/json" \
  -d '{"cmd":"save","id":4,"params":{"format":"json","path":"/config","file":"config.json"}}'

curl -X POST http://localhost:12000/ve \
  -H "Content-Type: application/json" \
  -d '{"batch":[{"op":"get","params":{"path":"one"}},{"op":"get","params":{"path":"two"}}],"id":5}'
```

### `POST /jsonrpc`

JSON-RPC remains for standard clients that do not want to speak the VE native envelope directly. Internally it maps onto the same dispatcher as `/ve`.

Supported methods (same names as the `/ve` ops):

- `get`
- `set`
- `export`
- `import`
- `children`
- `erase`
- `trigger`
- `commands`

User commands are called by their registered name as the JSON-RPC `method`.

Example:

```bash
curl -X POST http://localhost:12000/jsonrpc \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"get","params":{"path":"ve/server"},"id":1}'
```

---

## NodeWsServer

Port `12100`. WebSocket transport for the same VE envelope.

Requests:

```json
{"op":"export","id":1,"params":{"path":"ve/server","depth":-1}}
{"cmd":"save","id":2,"params":{"format":"json","path":"/config"}}
{"op":"subscribe","id":3,"params":{"path":"ve/server/node/http/runtime/port","depth":0}}
```

**Subscribe parameters:**
- `depth` (default `-1`): push depth on change — `-1` full subtree (Var dict), `0` value only, `N` recurse N levels
- `once` (default `false`): push once, then auto-unsubscribe
- `immediate` (default `false`): return the current state in the subscribe reply's `data` (same shape as `export`), instead of waiting for the first change

Subscriptions are exact-path: a descendant change does not fire a parent's
subscription. To notify on a subtree, `trigger` the parent after updating children.

Immediate replies:

```json
{"id":1,"code":0,"data":{...}}                            // export result
{"id":2,"code":0}                                         // async accepted (Result::accept), no further reply
{"id":3,"code":0}                                         // subscribe confirmed (data present only if immediate=true)
{"id":9,"code":-3,"message":"node not found: bad/path"}   // failure
```

Push events (no `id`; `data` carries value or subtree depending on `depth`):

```json
{"event":"node.changed","path":"ve/server/node/http/runtime/port","data":12000}
{"event":"node.changed","path":"ve/server","data":{"node":{"http":{"runtime":{"port":12000}}}}}
```

**Push event behavior:**
- `depth=0`: `{"event":"node.changed","path":"ve/server/node/http/runtime/port","data":12000}` (scalar value)
- `depth=-1` (default): `{"event":"node.changed","path":"ve/server","data":{"node":{"http":{"runtime":{"port":12000}}}}}` (subtree as Var dict)

---

## NodeTcpServer

Port `12200`. Same JSON envelope as WebSocket, but newline-delimited over TCP.

Example session:

```text
{"op":"get","id":1,"params":{"path":"ve/server"}}
{"id":1,"code":0,"data":{"value":null}}

{"op":"subscribe","id":2,"params":{"path":"ve/server/node/http/runtime/port","depth":0}}
{"id":2,"code":0}

{"event":"node.changed","path":"ve/server/node/http/runtime/port","data":12000}
```

---

## NodeUdpServer

Port `12300`. Stateless JSON envelope, one datagram per request.

- No subscribe support
- No async result push
- async commands (`Result::accept`) cannot push results back over UDP

Example:

```bash
echo '{"op":"get","id":1,"params":{"path":"ve/server"}}' | nc -u localhost 12300
```

---

## BinTcpServer

Port `11000`. MessagePack frame transport with the same envelope semantics.

Frame:

```text
[flag:1][length:4 LE][payload]
```

Flags:

- `0x00` REQUEST
- `0x40` RESPONSE
- `0x80` NOTIFY
- `0xC0` ERROR

Payload is a MessagePack-encoded VE envelope dict.

Examples:

```text
REQUEST: {"op":"get","id":1,"params":{"path":"ve/server"}}
RESPONSE: {"id":1,"code":0,"data":{"value":null}}
NOTIFY: {"event":"node.changed","path":"ve/server/node/http/runtime/port","data":12000}
```

---

## TerminalReplServer

Port `10000`. Interactive text terminal for debugging.

```bash
nc localhost 10000
> ls /
> get /ve/server
> set /test 42
> help
```

---

## StaticServer

Port `12400`. Static file hosting with reverse proxy support. Disabled by default (`enable: false`).

### Configuration

```json
{
  "ve": {
    "server": {
      "static": {
        "enable": true,
        "config": {
          "port": 12400,
          "mounts": [
            {
              "prefix": "/",
              "root": "./dist",
              "default_file": "index.html",
              "spa_fallback": true,
              "proxy": [
                {
                  "prefix": "/api",
                  "target": "http://127.0.0.1:8080/v1"
                },
                {
                  "prefix": "/ws",
                  "target": "http://127.0.0.1:12100"
                }
              ]
            }
          ]
        }
      }
    }
  }
}
```

### Mount points

Mounts are matched by longest prefix first. `"/"` is the root fallback.

| Field | Default | Description |
|-------|---------|-------------|
| `prefix` | `"/"` | URL prefix to match |
| `root` | (required) | Local directory for static files |
| `default_file` | `"index.html"` | Default file when path is empty |
| `spa_fallback` | `false` | Serve `default_file` for unmatched paths (SPA routing) |

### Proxy rules

Each mount can have proxy rules. Proxy is checked before static files.

| Field | Description |
|-------|-------------|
| `prefix` | URL prefix relative to the mount (e.g. `"/api"`) |
| `target` | Upstream URL including base path (e.g. `"http://127.0.0.1:8080/v1"`) |

Request path transformation:

```
Client: GET /{mountPrefix}/{proxyPrefix}/users?id=1
Upstream: GET {targetPath}/users?id=1 -> http://targetHost:targetPort{targetPath}/users?id=1
```

Headers are forwarded with hop-by-hop headers stripped. HTTPS proxy requires `ASIO2_ENABLE_SSL` at build time.

### Dynamic proxy target

Proxy targets support live updates via the node tree. When `ServerModule` starts the static server, it connects `NODE_CHANGED` signals on each proxy rule's `target` node. Changing the target value at runtime updates the cached proxy destination immediately, no restart needed.

```bash
# Terminal - switch API backend on the fly
set ve/server/static/config/mounts/#0/proxy/#0/target http://10.0.0.5:9090/v2

# HTTP
curl -X POST http://localhost:12000/at/ve/server/static/config/mounts/%230/proxy/%230/target \
  -H "Content-Type: application/json" \
  -d '"http://10.0.0.5:9090/v2"'

# WebSocket
{"op":"set","id":1,"params":{"path":"ve/server/static/config/mounts/#0/proxy/#0/target","value":"http://10.0.0.5:9090/v2"}}
```

Setting target to an empty string disables the proxy rule (requests fall through to static file serving).

### Request handling order

1. Proxy (first matching rule wins)
2. Static file
3. SPA fallback (if enabled)
4. 404 Not Found

---

## Subscription System

NodeWsServer, NodeTcpServer, and BinTcpServer support subscriptions through `SubscribeService`.

Matching is exact-path — only the subscribed node's own `NODE_CHANGED` fires a
push; descendant changes do not bubble to a parent subscription. To notify on a
subtree, `trigger` the parent node after updating its children.

Push shape is controlled per-subscription by `depth` (`-1` full subtree, `0`
value only, `N` levels). `once` auto-unsubscribes after the first push;
`immediate` returns the current state in the subscribe reply.

Lifecycle:

- Disconnect removes all subscriptions for that session
- Deleted nodes automatically invalidate observer connections
- Subscriber count is shared across service instances for the same root tree

---

## Python Client

The Python client lives in `ve/py`.

```python
from ve_client import VeClient

client = VeClient("http://localhost:12000", transport="http")
client.get("ve/server/node/http/runtime/port")
client.tree("ve/server")

client = VeClient("http://localhost:12000", transport="jsonrpc")
client = VeClient("tcp://localhost:12200")
client = VeClient("tcp://localhost:11000", transport="msgpack")
```

---

## Port Summary

| Port | Service | Protocol |
|------|---------|----------|
| 10000 | TerminalReplServer | Text REPL |
| 11000 | BinTcpServer | MessagePack envelope |
| 12000 | NodeHttpServer | `/at` + `/ve` + `/jsonrpc` |
| 12100 | NodeWsServer | WebSocket envelope |
| 12200 | NodeTcpServer | TCP JSON envelope |
| 12300 | NodeUdpServer | UDP JSON envelope |
| 12400 | StaticServer | Static files + reverse proxy |

---

## File I/O Commands

VE provides `save` and `load` commands for persisting Node trees to files. These commands support multiple formats and are accessible via all service endpoints.

### Supported Formats

| Format | Extension | Description | Use Case |
|--------|-----------|-------------|----------|
| `json` | `.json` | JSON tree export (schema-oriented, ignores repeated names) | Configuration, human-readable data |
| `xml` | `.xml` | XML tree export (attributes as `@key` children) | Legacy systems, XML-based protocols |
| `md` | `.md` | Markdown export (headings → Node hierarchy) | AI-friendly docs, RAG retrieval |
| `bin` | `.bin` | Binary export (CBS-compatible, preserves full tree) | High-performance IPC, snapshots |
| `var` | `.json` | Var-based export (JSON-stringified Var tree) | Internal serialization |

### save Command

Export Node tree to file or return as string.

**Syntax**:
```bash
save <format> [path] [-f file]
```

**Parameters**:
- `<format>` - Output format: `json`, `xml`, `md`, `bin`, `var`
- `[path]` - Node path to export (default: current context or root)
- `-f <file>` - File path relative to `data_root` (default: `./data/`)

**Examples**:

```bash
# Terminal
save json /config                    # Return JSON string
save json /config -f config.json     # Save to ./data/config.json
save md /docs/plan -f plan.md        # Export as Markdown

# HTTP
curl -X POST http://localhost:12000/cmd/save \
  -d '["json", "/config", "-f", "config.json"]'

# WebSocket (veservice.js)
veService.command("save", {format:"json", path:"/config", file:"config.json"});
```

### load Command

Import Node tree from file or inline data.

**Syntax**:
```bash
load <format> [path] [-f file] [-i data]
```

**Parameters**:
- `<format>` - Input format: `json`, `xml`, `md`, `bin`
- `[path]` - Target node path (default: current context or root)
- `-f <file>` - File path relative to `data_root`
- `-i <data>` - Inline data string

**Examples**:

```bash
# Terminal
load json /config -f config.json           # Load from file
load json /config -i '{"key":"value"}'     # Inline import
load md /docs/plan -f plan.md              # Import Markdown doc

# HTTP
curl -X POST http://localhost:12000/cmd/load \
  -d '["json", "/config", "-f", "config.json"]'

# WebSocket
veService.command("load", {format:"json", path:"/config", file:"config.json"});
```

### Markdown Schema (MdS)

Markdown format provides AI-friendly document storage with hierarchical retrieval.

**Mapping Rules**:
- MD heading → Node (name=cleaned title, value=content after heading)
- Original title → `_title` child (only if name was cleaned due to special chars)
- Heading level → `_level` child (only if level jumped, stores actual level)
- Special chars (`/`, `*`, `#`) → replaced with space in name

**Example**:

```markdown
# Database Configuration
Main database configuration

## MySQL
Production MySQL settings

# Title1
### Title3
Deep content
```

**Converts to Node tree**:
```
/Database Configuration (value: "Main database configuration")
  /MySQL (value: "Production MySQL settings")
/Title1 (value: null)
  /Title3 (value: "Deep content")
    /_level: 3  # Jumped from level 1 to 3
```

**RAG Use Cases**:

```bash
# 1. Import documentation
load md /docs/http-plan -f http-service-enhancement.md

# 2. Search by heading
search "Feature" /docs/http-plan --key
# Returns: ["docs/http-plan/.../Feature 1: 批量节点读取", ...]

# 3. Get specific section
curl -X POST http://localhost:12000/ve \
  -d '{"op":"export","id":1,"params":{"path":"docs/http-plan/.../Feature 1","depth":1}}'

# 4. List document structure
curl -X POST http://localhost:12000/ve \
  -d '{"op":"children","id":2,"params":{"path":"docs/http-plan/VE HTTP Service Enhancement Plan"}}'
```

**Benefits for AI**:
- Structured retrieval by heading hierarchy
- Incremental loading (no need to read entire doc)
- Cross-document search via `search` command
- Real-time updates via WebSocket subscriptions
