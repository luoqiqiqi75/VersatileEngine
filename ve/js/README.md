# VE JavaScript / TypeScript Clients

This directory contains JavaScript / TypeScript clients and tools for the VE runtime.

**HTTP / WS / BinTcp are different transports for the same VE protocol semantics.**

---

## Packages

| Path | Description |
|------|------|
| `ve-sdk/` | TypeScript SDK: `VeHttpClient`, `VeWsClient`, `VeBinTcpClient` |
| `veservice.js` | Standalone WebSocket client for browsers |
| `ve-mcp/` | Cursor MCP adapter that calls VE through `POST /ve` |
| `ve-app/` | React administration interface |

---

## HTTP Endpoints

NodeHttpServer exposes the following endpoints:

- `GET /health`
- `GET/POST/PUT/DELETE /at/<path>`
- `POST /cmd/<name>`
- `POST /ve`
- `POST /jsonrpc`

Endpoint responsibilities:

- `/at/<path>`: read and write schema JSON directly from a browser or curl
- `/cmd/<name>`: run commands directly from a browser or curl
- `/ve`: main endpoint for the native VE protocol
- `/jsonrpc`: endpoint for standard JSON-RPC clients

### `/at/<path>`

- `GET /at/<path>`: export a node or subtree
- `POST /at/<path>`: set a value, or trigger a signal with `?trigger=1`
- `PUT /at/<path>`: import raw JSON as a subtree
- `DELETE /at/<path>`: remove a node

Examples:

```bash
curl http://127.0.0.1:12000/at/ve/server
curl http://127.0.0.1:12000/at/ve/server?children=1
curl http://127.0.0.1:12000/at/ve/server?structure=1
curl -X POST http://127.0.0.1:12000/at/test/value -H "Content-Type: application/json" -d '42'
curl -X POST "http://127.0.0.1:12000/at/test/value?trigger=1"
curl -X PUT "http://127.0.0.1:12000/at/test/tree?auto_remove=1" -H "Content-Type: application/json" -d '{"a":1,"b":2}'
curl -X DELETE http://127.0.0.1:12000/at/test/tree
```

`GET /at/<path>` supports:

- `auto_ignore=0|1`
- `depth=<n>`
- `children=1`
- `structure=1`
- `meta=1`

`PUT /at/<path>` supports:

- `auto_insert=0|1`
- `auto_remove=0|1`
- `auto_update=0|1`

### `/cmd/<name>`

Call a command directly using a simple HTTP request.

```bash
curl -X POST http://127.0.0.1:12000/cmd/search \
  -H "Content-Type: application/json" \
  -d '["port", "/", "--top", "5"]'

curl -X POST "http://127.0.0.1:12000/cmd/save?context=ve/server&async=1" \
  -H "Content-Type: application/json" \
  -d '{"file":"server.json","path":"/ve/server/node"}'
```

### `/ve`

The `POST /ve` request body uses a shared envelope:

```json
{
  "op": "node.get",
  "id": 1,
  "path": "ve/server/node/http",
  "depth": 1,
  "meta": true
}
```

Common `op` values:

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

Response envelopes:

```json
{"ok":true,"id":1,"data":{...}}
{"ok":true,"id":1,"accepted":true,"task_id":"abcd1234"}
{"ok":false,"id":1,"code":"not_found","error":"node not found: foo/bar"}
```

---

## `@ve/sdk`

### 1. Local Path Dependency

```json
{
  "dependencies": {
    "@ve/sdk": "file:../VersatileEngine/ve/js/ve-sdk"
  }
}
```

Build the SDK before using it:

```bash
cd ve/js/ve-sdk
npm install
npm run build
```

### 2. Minimal Example

```ts
import { VeHttpClient } from '@ve/sdk';

const ve = new VeHttpClient('http://127.0.0.1:12000');

await ve.health();

const tree = await ve.get('ve/server');
const port = await ve.val('ve/server/node/http/runtime/port');
const commands = await ve.cmds();

await ve.run('save', ['ve.json', '/ve']);
```

### 3. `VeHttpClient`

The core `VeHttpClient` method is:

- `call(op, payload)` - call `/ve` directly

Convenience methods:

- `get(path, depth?)` - get a tree (`depth=-1` by default for the full tree)
- `set(path, tree)` - set a tree
- `val(path)` / `val(path, value)` - get or set a single value
- `list(path)` - list child nodes
- `rm(path)` - remove a node
- `trigger(path)` - trigger a signal
- `run(name, args, wait?)` - run a command
- `cmds()` - list commands
- `batch(items)` - perform a batch of operations
- `exportTree(path, depth?)` - export a tree as a JSON string
- `importTree(path, json)` - import a tree from JSON using REST PUT

Notes:

- Pass `wait` explicitly when calling `runCommand()`.
- If the server returns `accepted + task_id`, the command has been accepted and its result will be stored at `ve/server/tasks/<task_id>`.

---

## `VeWsClient`

The WebSocket protocol uses the same envelope as `/ve` over a WebSocket transport.

Request examples:

```json
{"op":"node.get","path":"ve/server","id":1}
{"op":"command.run","name":"save","args":["ve.json","/ve"],"wait":false,"id":2}
{"op":"subscribe","path":"ve/server/node/http/runtime/port","id":3}
```

Push notification examples:

```json
{"event":"node.changed","path":"ve/server/node/http/runtime/port","value":12000}
{"event":"task.result","id":2,"task_id":"abcd1234","ok":true,"data":"Saved"}
```

TypeScript example:

```ts
import { VeWsClient } from '@ve/sdk';

const ws = new VeWsClient({ url: 'ws://127.0.0.1:12100' });
ws.connect();

// Get the full tree.
const tree = await ws.get('ve/server');

// Get or set a single value.
const port = await ws.val('ve/server/node/http/runtime/port');
await ws.val('test/value', 42);

// Subscribe with tree:true by default to receive the full subtree.
const stop = ws.watch('ve/server/node/http/runtime', (path, value) => {
  console.log(path, value);
});

// Run a command.
await ws.run('save', ['ve.json', '/ve']);
```

`VeWsClient` methods:

- `get(path, depth?)` - get a tree (`depth=-1` by default for the full tree)
- `set(path, tree)` - set a tree
- `val(path)` / `val(path, value)` - get or set a single value
- `list(path)` - list child nodes
- `rm(path)` - remove a node
- `trigger(path)` - trigger a signal
- `watch(path, handler, opts?)` - subscribe (`tree:true` by default)
- `unwatch(path)` - unsubscribe
- `run(name, args, wait?)` - run a command
- `cmds()` - list commands
- `batch(items)` - perform a batch of operations
- `call(op, payload)` - make a low-level protocol call
- `onConnectionChange(handler)` - listen for connection state changes
- `onMessage(handler)` - listen for all messages

---

## `VeBinTcpClient`

`VeBinTcpClient` uses MessagePack frames with the same payload semantics as `/ve` and WebSocket.

Frame format:

```text
[flag:1][length:4 LE][payload: MessagePack]
```

Flags:

| Flag | Value | Direction |
|------|-------|-----------|
| REQUEST | `0x00` | client -> server |
| RESPONSE | `0x40` | server -> client |
| NOTIFY | `0x80` | server -> client |
| ERROR | `0xC0` | server -> client |

Example:

```ts
import { VeBinTcpClient } from '@ve/sdk';

const client = new VeBinTcpClient({ host: '127.0.0.1', port: 11000 });
await client.connect();

const port = await client.get('ve/server/node/http/runtime/port');
await client.set('test/value', 42);

const run = await client.command('save', ['ve.json', '/ve'], true);

const unsub = await client.subscribe('ve/server/node/http/runtime/port', (path, value) => {
  console.log(path, value);
});
```

---

## Browser Script Without a Bundler

Include the script directly in the page:

```html
<script>
  var _ve_ws_url = 'ws://127.0.0.1:12100';
  function veServiceCallback(svc) {
    svc.get('ve/server/node/ws/runtime/port').then(console.log);
  }
</script>
<script src="path/to/veservice.js"></script>
```

Define `_ve_ws_url` and `veServiceCallback` before loading or injecting the script.
The script creates the global `veService` and starts connecting. After the first
successful connection, it calls `veServiceCallback(svc)` once to initialize injected
code. Automatic reconnections do not call it again; use `onConnectionChange(fn)`
to observe connection state changes.

You can also create `new VEService(url)` and call `connect()` explicitly. Repeated
calls while connecting return the same Promise; calls while connected resolve
immediately. Set `connectTimeout` for the connection timeout (5000 ms by default)
and `timeout` for the request response timeout (3000 ms by default) on the instance.

If the initial connection fails, the Promise is rejected and the caller decides
whether to retry. After a successful connection, a dropped connection triggers
automatic reconnection with delays of 1, 2, 4, and 8 seconds, capped at 8 seconds.
Subscriptions are restored after reconnection. `disconnect()` cancels connection
attempts and retries, rejects pending requests, and clears subscriptions. Calling
`connect()` explicitly afterward reconnects and enables automatic reconnection.

The browser script uses the `{op, id, params}` / `{cmd, id, params}` protocol and
provides:

- `get(path)` / `set(path, value)` - get or set a single value
- `export(path, depth?)` / `import(path, tree, flags?)` - export or import a subtree
- `children(path)` - list child node information
- `erase(path)` - remove a node
- `trigger(path)` - trigger a signal
- `subscribe(path, fn, options?)` - subscribe with callback `fn(data, path)` and return an unsubscribe function
- `unsubscribe(path, fn?)` - remove a specific callback or all subscriptions for the path
- `run(name, args?)` - run a command and return the full response
- `commands()` - list commands
- `batch(items)` - perform a batch of operations
- `send(message)` - make a low-level protocol call and return the full response
- `onConnectionChange(fn)` / `onMessage(fn)` - register a listener and return a function to remove it

Run the connection lifecycle regression tests (Node.js 18+, no dependencies required):

```bash
node --test ve/js/test/veservice.test.cjs
```

---

## Choosing an Interface

- Read-only access, debugging, or curl: use `/at/<path>`
- Application integration over HTTP: use `/ve`
- Standard JSON-RPC clients: use `/jsonrpc`
- Real-time browser subscriptions: use `VeWsClient` or `veservice.js`
- High-frequency Node.js communication: use `VeBinTcpClient`

---

## `AGENTS.md` / MCP Integration

- `ve-mcp` exposes Cursor tools through `command.list` / `command.run` on `POST /ve`.
- Use `/health` for health checks.
- Read runtime ports and state through `GET /at/ve/server/...`.
