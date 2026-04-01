# VersatileEngine Service Reference

VE provides multiple network services for accessing the Node tree. All services operate on the same shared data tree and are enabled via configuration.

## Services Overview

| Service | Transport | Port | Protocol | Subscribe | Use Case |
|---------|-----------|------|----------|-----------|----------|
| NodeHttpServer | HTTP | 5080 | REST + JSON-RPC 2.0 | No | Browser, curl, any HTTP client |
| NodeWsServer | WebSocket | 5081 | JSON commands | Yes | Real-time web apps |
| NodeTcpServer | TCP | 5082 | JSON + newline | Yes | Embedded, scripts, IoT |
| NodeUdpServer | UDP | 5083 | JSON datagram | No | Fire-and-forget, telemetry |
| BinTcpServer | TCP | 5065 | MessagePack frames | Yes | High-performance IPC |
| TerminalReplServer | TCP | 5061 | Text commands | No | Interactive debugging |

All services are enabled by default. Configure via `ve.json`:

```json
{
  "ve": {
    "server": {
      "node": {
        "http": { "enable": true, "config": { "port": 5080 } },
        "ws":   { "enable": true, "config": { "port": 5081 } },
        "tcp":  { "enable": true, "config": { "port": 5082 } },
        "udp":  { "enable": true, "config": { "port": 5083 } }
      },
      "bin": {
        "tcp": { "enable": true, "config": { "port": 5065 } }
      },
      "terminal": {
        "repl": { "enable": true, "config": { "port": 5061 } }
      }
    }
  }
}
```

---

## NodeHttpServer (HTTP)

Port 5080. Supports REST API and JSON-RPC 2.0 on the same server.

### REST API

**Get node value**
```bash
GET /api/node/{path}

curl http://localhost:5080/api/node/config/port
# {"path":"config/port","value":5080}
```

**Set node value**
```bash
PUT /api/node/{path}
Body: JSON value

curl -X PUT http://localhost:5080/api/node/test -H "Content-Type: application/json" -d '42'
# {"ok":true,"path":"test"}
```

**Trigger node**
```bash
POST /api/node/{path}
Body: optional JSON value

curl -X POST http://localhost:5080/api/node/test/trigger
# {"ok":true,"path":"test/trigger"}
```

**Get subtree**
```bash
GET /api/tree/{path}

curl http://localhost:5080/api/tree/config
# {"port":5080,"log":{"level":"info"}}
```

**Import subtree**
```bash
POST /api/tree/{path}
Body: JSON object

curl -X POST http://localhost:5080/api/tree/test -H "Content-Type: application/json" -d '{"a":1,"b":2}'
# {"ok":true,"path":"test"}
```

**List children**
```bash
GET /api/children/{path}

curl http://localhost:5080/api/children/config
# ["port","log"]
```

**List commands**
```bash
GET /api/cmd

curl http://localhost:5080/api/cmd
# {"commands":[{"name":"ls","help":"..."},{"name":"get","help":"..."},...]}
```

**Run command**
```bash
POST /api/cmd/{name}
Body: {"args": ...}

curl -X POST http://localhost:5080/api/cmd/ls -H "Content-Type: application/json" -d '{"body":"/"}'
# {"ok":true,"result":[...]}
```

**Health check**
```bash
GET /health

curl http://localhost:5080/health
# {"status":"ok","uptime_s":42}
```

### JSON-RPC 2.0

All methods available via `POST /jsonrpc`.

**Methods:**

| Method | Params | Description |
|--------|--------|-------------|
| `node.get` | `{"path": "/config"}` | Get node value |
| `node.set` | `{"path": "/test", "value": 42}` | Set node value |
| `node.list` | `{"path": "/"}` | List children with metadata |
| `command.run` | `{"name": "ls", "args": {...}}` | Run a command |

**Examples:**

```bash
# Get
curl -X POST http://localhost:5080/jsonrpc \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"node.get","params":{"path":"/config"},"id":1}'
# {"jsonrpc":"2.0","result":{"found":true,"value":...,"path":"config"},"id":1}

# Set
curl -X POST http://localhost:5080/jsonrpc \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"node.set","params":{"path":"/test","value":42},"id":2}'
# {"jsonrpc":"2.0","result":{"success":true,"path":"test"},"id":2}

# List
curl -X POST http://localhost:5080/jsonrpc \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"node.list","params":{"path":"/"},"id":3}'
# {"jsonrpc":"2.0","result":{"found":true,"path":"","children":[...]},"id":3}
```

**Standard JSON-RPC libraries work directly:**
```python
# Python
from jsonrpcclient import request
import requests
r = requests.post("http://localhost:5080/jsonrpc", json=request("node.get", path="/"))
```

```javascript
// JavaScript
import { JSONRPCClient } from "json-rpc-2.0";
const client = new JSONRPCClient(req =>
  fetch("http://localhost:5080/jsonrpc", {
    method: "POST", body: JSON.stringify(req),
    headers: {"content-type": "application/json"}
  }).then(r => r.json())
);
await client.request("node.get", {path: "/"});
```

### Static File Serving

Configure `static_root` to serve files from a directory:
```json
"http": {
  "enable": true,
  "config": {
    "port": 5080,
    "static_root": "./www",
    "default_file": "index.html"
  }
}
```

---

## NodeWsServer (WebSocket)

Port 5081. Persistent connections with subscription support.

### JSON Commands

Connect to `ws://localhost:5081` and send JSON messages:

**Get**
```json
{"cmd":"get","path":"/config","id":1}
-> {"type":"data","path":"config","value":{...},"id":1}
```

**Set**
```json
{"cmd":"set","path":"/test","value":42,"id":2}
-> {"type":"ok","path":"test","id":2}
```

**Subscribe** (receive push on node change)
```json
{"cmd":"subscribe","path":"/config","id":3}
-> {"type":"subscribed","path":"/config","id":3}

// When /config changes:
-> {"type":"event","path":"config","value":{...}}
```

**Unsubscribe**
```json
{"cmd":"unsubscribe","path":"/config","id":4}
-> {"type":"unsubscribed","path":"/config","id":4}
```

### Browser Usage

Include `veservice.js` for auto-connecting WebSocket client:
```html
<script src="veservice.js"></script>
<script>
veService.get("/config").then(v => console.log(v));
veService.set("/test", 42);
veService.subscribe("/config", v => console.log("changed:", v));
</script>
```

---

## NodeTcpServer (TCP)

Port 5082. JSON text protocol, newline-delimited (`\n`). Same commands as WebSocket JSON mode. Supports subscribe.

### Usage

```bash
# Connect
nc localhost 5082

# Get
{"cmd":"get","path":"/config","id":1}
-> {"type":"data","path":"config","value":{...},"id":1}

# Set
{"cmd":"set","path":"/test","value":42,"id":2}
-> {"type":"ok","path":"test","id":2}

# List
{"cmd":"list","path":"/","id":3}
-> {"type":"data","path":"","children":["config","test",...],"id":3}

# Subscribe
{"cmd":"subscribe","path":"/test","id":4}
-> {"type":"subscribed","path":"/test","id":4}
// Push on change:
-> {"type":"event","path":"test","value":42}
```

### Python Example

```python
import socket, json

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.connect(("localhost", 5082))

def send(cmd):
    s.sendall((json.dumps(cmd) + "\n").encode())
    return json.loads(s.recv(4096).decode().strip())

print(send({"cmd": "get", "path": "/config", "id": 1}))
print(send({"cmd": "set", "path": "/test", "value": 42, "id": 2}))
```

---

## NodeUdpServer (UDP)

Port 5083. Stateless, one JSON object per datagram. No subscribe support (no persistent connection).

### Usage

```bash
# Get
echo '{"cmd":"get","path":"/config","id":1}' | nc -u localhost 5083

# Set
echo '{"cmd":"set","path":"/test","value":42,"id":1}' | nc -u localhost 5083
```

### Python Example

```python
import socket, json

s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

def send(cmd):
    s.sendto(json.dumps(cmd).encode(), ("localhost", 5083))
    data, _ = s.recvfrom(4096)
    return json.loads(data.decode())

print(send({"cmd": "get", "path": "/config", "id": 1}))
```

---

## BinTcpServer (MessagePack Binary)

Port 5065. High-performance binary protocol with frame-based messaging.

### Frame Format

```
[flag:1][length:4 LE][payload]
```

Flag bits `[7:6]`:
- `0x00` REQUEST
- `0x40` RESPONSE
- `0x80` NOTIFY (server push)
- `0xC0` ERROR

Payload: MessagePack-encoded Var Dict.

### Request Format

```
{"op": "get", "path": "/config", "args": [...], "id": 1}
```

### Response Format

```
{"id": 1, "code": 0, "data": ...}
```

### Operations

All operations use `command::call(op, args)` internally. Standard ops:

| Op | Args | Description |
|----|------|-------------|
| `get` | `[path]` | Get node value |
| `set` | `[path, value]` | Set node value |
| `ls` | `[path]` | List children |
| `subscribe` | path | Subscribe to changes |
| `unsubscribe` | path | Unsubscribe |

### Push Notifications

On subscribe, the server sends `FLAG_NOTIFY` frames when subscribed nodes change:
```
{"path": "config/port", "value": 5080}
```

### Python Example

```python
import socket, struct, msgpack

def send_frame(sock, op, path, req_id):
    payload = msgpack.packb({"op": op, "path": path, "id": req_id})
    header = struct.pack("<BI", 0x00, len(payload))  # FLAG_REQUEST
    sock.sendall(header + payload)

def recv_frame(sock):
    header = sock.recv(5)
    flag, length = struct.unpack("<BI", header)
    payload = sock.recv(length)
    return flag, msgpack.unpackb(payload)

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.connect(("localhost", 5065))
send_frame(s, "get", "/config", 1)
flag, response = recv_frame(s)
print(response)
```

---

## TerminalReplServer (Text REPL)

Port 5061. Interactive text terminal for debugging.

### Usage

```bash
# Connect
nc localhost 5061

# Commands
> ls /
> get /config/port
> set /test 42
> help
```

### Built-in Commands

| Command | Usage | Description |
|---------|-------|-------------|
| `ls` | `ls [path]` | List children |
| `get` | `get path [from to]` | Get node value |
| `set` | `set path value` | Set node value |
| `info` | `info [path]` | Node info (type, children count) |
| `add` | `add path [value]` | Create node |
| `rm` | `rm path` | Remove node |
| `mv` | `mv src dst` | Move node |
| `mk` | `mk path` | Create path |
| `find` | `find pattern` | Find nodes by pattern |
| `erase` | `erase path` | Erase node and children |
| `json` | `json [path]` | Export subtree as JSON |
| `child` | `child path` | List child names |
| `shadow` | `shadow path` | Show shadow info |
| `watch` | `watch path` | Watch node changes |
| `iter` | `iter path` | Iterate children |
| `schema` | `schema path` | Show schema |
| `cmd` | `cmd name [args]` | Run named command |
| `help` | `help [cmd]` | Show help |

---

## Subscription System

NodeWsServer, NodeTcpServer, and BinTcpServer support subscriptions via `SubscribeService`.

### Modes

**Direct (default)** - only notified when the exact node changes:
```json
{"cmd":"subscribe","path":"/config/port"}
// Only fires when /config/port itself changes
```

**Bubble** - notified when any descendant changes:
```json
{"cmd":"subscribe","path":"/config","bubble":true}
// Fires when /config/port, /config/log/level, etc. change
```

### Lifecycle

- Subscribing to a node connects an observer to that node's signal
- When a session disconnects, all its subscriptions are automatically removed
- When a node is deleted, connected observers are automatically invalidated (no leak)
- No global tree watching - only subscribed nodes are monitored

---

## Python Client (ve_client)

Located at `ve/py/`. Install with `pip install -e ve/py`.

```python
from ve_client import VeClient

# REST API (default)
client = VeClient("http://localhost:5080")

# JSON-RPC transport
client = VeClient("http://localhost:5080", transport="jsonrpc")

# Operations
client.get("/config")         # Get node value
client.set("/test", 42)       # Set node value
client.list("/")              # List children
client.tree("/config")        # Get subtree
client.command("ls", {})      # Run command
client.ping()                 # Health check
```

---

## Port Summary

| Port | Service | Protocol |
|------|---------|----------|
| 5061 | TerminalReplServer | Text REPL |
| 5065 | BinTcpServer | MessagePack binary |
| 5080 | NodeHttpServer | HTTP REST + JSON-RPC |
| 5081 | NodeWsServer | WebSocket JSON |
| 5082 | NodeTcpServer | TCP JSON (newline) |
| 5083 | NodeUdpServer | UDP JSON (datagram) |
