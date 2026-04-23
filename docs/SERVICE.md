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

Request:

```json
{
  "op": "node.get",
  "id": 1,
  "path": "ve/server/node/http",
  "depth": 1,
  "meta": true
}
```

Response:

```json
{"ok":true,"id":1,"data":{...}}
{"ok":true,"id":1,"accepted":true,"task_id":"abcd1234"}
{"ok":false,"id":1,"code":"not_found","error":"node not found: foo/bar"}
```

Common operations:

| Op | Key fields | Description |
|----|------------|-------------|
| `node.get` | `path`, `depth?`, `meta?` | Read one node; optional `tree` and `meta` in response |
| `node.list` | `path`, `meta?` | List direct children |
| `node.set` | `path`, `value` | Set raw node value |
| `node.put` | `path`, `tree` | Import/overwrite subtree |
| `node.remove` | `path` | Remove node |
| `node.trigger` | `path` | Fire `NODE_CHANGED` without changing value |
| `command.list` | none | List registered commands |
| `command.run` | `name`, `args`, `wait?` | Run a command |
| `batch` | `items` | Execute multiple envelope requests |
| `subscribe` | `path`, `bubble?` | Stateful transports only |
| `unsubscribe` | `path` | Stateful transports only |

Examples:

```bash
curl -X POST http://localhost:12000/ve \
  -H "Content-Type: application/json" \
  -d '{"op":"node.get","path":"ve/server/node/http/runtime/port","meta":true}'

curl -X POST http://localhost:12000/ve \
  -H "Content-Type: application/json" \
  -d '{"op":"node.set","path":"test/value","value":42}'

curl -X POST http://localhost:12000/ve \
  -H "Content-Type: application/json" \
  -d '{"op":"command.list"}'

curl -X POST http://localhost:12000/ve \
  -H "Content-Type: application/json" \
  -d '{"op":"command.run","name":"save","args":["json","/config","-f","config.json"],"wait":true}'
```

### `POST /jsonrpc`

JSON-RPC remains for standard clients that do not want to speak the VE native envelope directly. Internally it maps onto the same dispatcher as `/ve`.

Supported methods:

- `node.get`
- `node.list`
- `node.set`
- `node.put`
- `node.remove`
- `node.trigger`
- `command.list`
- `command.run`

Example:

```bash
curl -X POST http://localhost:12000/jsonrpc \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"node.get","params":{"path":"ve/server"},"id":1}'
```

---

## NodeWsServer

Port `12100`. WebSocket transport for the same VE envelope.

Requests:

```json
{"op":"node.get","path":"ve/server","id":1,"depth":-1}
{"op":"command.run","name":"save","args":["json","/config"],"wait":false,"id":2}
{"op":"subscribe","path":"ve/server/node/http/runtime/port","tree":true,"id":3}
```

**Subscribe parameters (all default to `false`):**
- `bubble`: monitor all descendant changes; push fires for each changed leaf with that leaf's path and scalar value
- `tree`: when the subscribed node fires `NODE_CHANGED`, push the entire subtree as a Var dict instead of the node's scalar value

Without either flag, only the exact subscribed node is monitored and the push carries its scalar value (`node.get()`).

Immediate replies:

```json
{"ok":true,"id":1,"data":{"path":"ve/server","value":null,"tree":{...}}}
{"ok":true,"id":2,"accepted":true,"task_id":"abcd1234"}
{"ok":false,"id":9,"code":"not_found","error":"node not found: bad/path"}
```

Push events:

```json
{"event":"node.changed","path":"ve/server/node/http/runtime/port","value":12000}
{"event":"node.changed","path":"ve/server","value":{"node":{"http":{"runtime":{"port":12000}}}}}
{"event":"task.result","id":2,"task_id":"abcd1234","ok":true,"data":"Saved to config.json"}
```

**Push event behavior:**
- Default (`bubble=false, tree=false`): `{"event":"node.changed","path":"ve/server/node/http/runtime/port","value":12000}` (scalar value)
- `tree=true`: `{"event":"node.changed","path":"ve/server","value":{"node":{"http":{"runtime":{"port":12000}}}}}` (entire subtree as Var dict)
- `bubble=true`: fires for each descendant change with that descendant's path and scalar value

---

## NodeTcpServer

Port `12200`. Same JSON envelope as WebSocket, but newline-delimited over TCP.

Example session:

```text
{"op":"node.get","path":"ve/server","id":1}
{"ok":true,"id":1,"data":{"path":"ve/server","value":null}}

{"op":"subscribe","path":"ve/server/node/http/runtime/port","id":2}
{"ok":true,"id":2,"data":{"path":"ve/server/node/http/runtime/port","subscribed":true}}

{"event":"node.changed","path":"ve/server/node/http/runtime/port","value":12000}
```

---

## NodeUdpServer

Port `12300`. Stateless JSON envelope, one datagram per request.

- No subscribe support
- No async result push
- `command.run(wait=false)` can still return `accepted + task_id`

Example:

```bash
echo '{"op":"node.get","path":"ve/server","id":1}' | nc -u localhost 12300
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
REQUEST: {"op":"node.get","path":"ve/server","id":1}
RESPONSE: {"ok":true,"id":1,"data":{"path":"ve/server","value":null}}
NOTIFY: {"event":"node.changed","path":"ve/server/node/http/runtime/port","value":12000}
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

## Subscription System

NodeWsServer, NodeTcpServer, and BinTcpServer support subscriptions through `SubscribeService`.

Modes:

- Direct: only the exact node
- Bubble: descendant changes too (`bubble=true`)

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
  -d '{"op":"node.get","path":"docs/http-plan/.../Feature 1","depth":1}'

# 4. List document structure
curl -X POST http://localhost:12000/ve \
  -d '{"op":"node.list","path":"docs/http-plan/VE HTTP Service Enhancement Plan"}'
```

**Benefits for AI**:
- Structured retrieval by heading hierarchy
- Incremental loading (no need to read entire doc)
- Cross-document search via `search` command
- Real-time updates via WebSocket subscriptions
