# VE JavaScript / TypeScript Clients

This directory contains JavaScript / TypeScript clients and tools for the VE runtime.

**HTTP / WS / BinTcp are different transports for the same VE protocol semantics.**

---

## Packages

| Path           | Description                                                    |
| -------------- | -------------------------------------------------------------- |
| `ve-sdk/`      | TypeScript SDK: `VeHttpClient`, `VeWsClient`, `VeBinTcpClient` |
| `veservice.js` | Standalone WebSocket client for browsers                       |
| `ve-mcp/`      | Cursor MCP adapter that calls VE through `POST /ve`            |
| `ve-app/`      | React administration interface                                 |

---

## HTTP Endpoints

NodeHttpServer exposes the following endpoints:

- `GET /health`
- `GET/POST/PUT/DELETE /at/<path>`
- `POST /cmd/<name>`
- `POST /ve`

Endpoint responsibilities:

- `/at/<path>`: read and write schema JSON directly from a browser or curl
- `/cmd/<name>`: run commands directly from a browser or curl
- `/ve`: main endpoint for the native VE protocol

`/jsonrpc` is no longer supported. Use `POST /ve` with the native VE envelope.

### `/at/<path>`

- `GET /at` or `GET /at/<path>`: export the root or a subtree as schema JSON
- `PUT /at/<path>`: merge raw schema JSON into an existing node (`COPY_DEFAULT`), preserving children absent from the body
- `POST /at/<path>`: strictly import raw schema JSON into an existing node (`COPY_STRICT`), removing children absent from the body
- `DELETE /at/<path>`: remove a node

Writes require an existing target (HTTP 404 otherwise). Successful writes return
HTTP 200 with an empty body. PUT and POST require valid JSON; an empty body
returns HTTP 400. Query parameters are ignored, including `auto_insert`,
`auto_remove`, `auto_update` and `trigger`.

Examples:

```bash
curl http://127.0.0.1:12000/at/ve/server
curl -X POST http://127.0.0.1:12000/ve -H "Content-Type: application/json" -d '{"op":"set","params":{"path":"test/tree","value":null}}'
curl -X PUT http://127.0.0.1:12000/at/test/tree -H "Content-Type: application/json" -d '{"a":1,"b":2}'
curl -X POST http://127.0.0.1:12000/at/test/tree -H "Content-Type: application/json" -d '{"a":1}'
curl -X POST http://127.0.0.1:12000/ve -H "Content-Type: application/json" -d '{"op":"trigger","params":{"path":"test/tree"}}'
curl -X DELETE http://127.0.0.1:12000/at/test/tree
```

Use `POST /ve` with `op:"set"` to set only a node's value, `op:"trigger"` to
emit a signal, or `op:"import"` with `params.flags` to control import behavior
and create missing paths. `VeHttpClient.importRest()` uses the merge PUT route
and resolves without a value on success.

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
  "op": "get",
  "id": 1,
  "params": { "path": "ve/server/node/http" }
}
```

Common `op` values:

- `get` / `set`
- `export` / `import`
- `children`
- `erase`
- `trigger`
- `commands` / `describe`
- `subscribe`
- `unsubscribe`

User commands use `{cmd, params}`. Batches use a top-level `{batch: [...]}` envelope, not an `op:"batch"` request.

Response envelopes:

```json
{"id":1,"code":0,"data":{"value":12000}}
{"id":1,"code":-3,"message":"not found: foo/bar"}
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
npm run typecheck
npm run build
```

### 2. Minimal Example

```ts
import { VeHttpClient } from '@ve/sdk';

const ve = new VeHttpClient('http://127.0.0.1:12000');

await ve.health();

const value = await ve.get('config/value');
const tree = await ve.export('config', 2);
const metadata = await ve.meta('config');
const children = await ve.children('config', { limit: 8, meta: true });
const operation = await ve.describe('children');
```

### 3. `VeHttpClient`

The core `VeHttpClient` method is:

- `call(op, params, options?)` - call `/ve` and return the full reply
- `send(message, options?)` - send an op, cmd or batch envelope
- `sendRaw(message, options?)` - return the response envelope as unparsed text

Convenience methods:

- `get(path, options?)` / `set(path, value, options?)` - read or write a value
- `export(path, depth?, options?)` / `import(path, tree, flags?, options?)` - export or import a subtree
- `meta(path, options?)` - read the native node type and direct child count without transferring its value
- `children(path, childrenOptions?, options?)` - read key windows; `childrenOptions` contains `offset/limit/value/meta`, while final `options` controls the HTTP request
- `erase(path, options?)` / `trigger(path, options?)` - erase a node or trigger a signal
- `run(name, args?, options?)` - run a user command with named arguments
- `commands(options?)` / `describe(name, options?)` - list user commands or inspect an operation signature
- `batch(items, options?)` - perform a batch of operations
- `exportRest(path, options?)` / `importRest(path, json, options?)` - use the existing REST endpoints

Notes:

- Optional per-request cancellation, deadlines, response limits and numeric precision are documented in the [HTTP client guide](ve-sdk/README.md).
- Existing calls keep their defaults. `get` and `export` parse JSON numbers as JavaScript Number; `sendRaw` keeps their original tokens.
- Capability signatures describe advertised behavior. They do not prove that a remote service has been upgraded or that its runtime behavior has been verified.

---

## `VeWsClient`

The WebSocket protocol uses the same envelope as `/ve` over a WebSocket transport.

Request examples:

```json
{"op":"get","params":{"path":"config/value"},"id":1}
{"cmd":"search","params":{"pattern":"port","root":"config"},"id":2}
{"op":"subscribe","params":{"path":"config/value","depth":0,"immediate":true},"id":3}
```

Push notification examples:

```json
{ "event": "node.changed", "path": "config/value", "data": 12000 }
```

TypeScript example:

```ts
import { VeWsClient } from '@ve/sdk';

const ws = new VeWsClient({ url: 'ws://127.0.0.1:12100' });
ws.connect();

const value = await ws.get('config/value');
const tree = await ws.export('config', 2);

const stop = ws.subscribe('config/value', (path, value) => {
  console.log(path, value);
});

// Run a command.
await ws.run('save', { file: 'config.json', path: 'config' });
```

`VeWsClient` methods:

- `get(path)` / `set(path, value)` - read or write a value
- `export(path, depth?)` / `import(path, tree, flags?)` - export or import a subtree
- `meta(path)` - read node type/count without transferring the value
- `children(path, {offset, limit, value, meta}?)` - read child key windows with optional value/type/count
- `erase(path)` - remove a node
- `trigger(path)` - trigger a signal
- `subscribe(path, handler, opts?)` / `unsubscribe(path)` - subscribe or unsubscribe
- `run(name, args?)` - run a command with named arguments
- `commands()` - list user commands
- `batch(items)` - perform a batch of operations
- `call(op, params)` - make a low-level protocol call
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

| Flag     | Value  | Direction        |
| -------- | ------ | ---------------- |
| REQUEST  | `0x00` | client -> server |
| RESPONSE | `0x40` | server -> client |
| NOTIFY   | `0x80` | server -> client |
| ERROR    | `0xC0` | server -> client |

Example:

```ts
import { VeBinTcpClient } from '@ve/sdk';

const client = new VeBinTcpClient({ host: '127.0.0.1', port: 11000 });
await client.connect();

const port = await client.get('ve/server/node/http/runtime/port');
await client.set('test/value', 42);

const run = await client.run('save', { file: 'config.json', path: 'config' });

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
- `meta(path)` - node `{type, count}` without the value
- `children(path, {offset, limit, value, meta}?)` - child entries `{key}` plus requested `value` / `type`, `count`
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
- Real-time browser subscriptions: use `VeWsClient` or `veservice.js`
- High-frequency Node.js communication: use `VeBinTcpClient`

---

## `AGENTS.md` / MCP Integration

- `ve-mcp` exposes Cursor tools through `command.list` / `command.run` on `POST /ve`.
- Use `/health` for health checks.
- Read runtime ports and state through `GET /at/ve/server/...`.
