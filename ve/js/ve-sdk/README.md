# @ve/sdk HTTP client

The HTTP client uses the existing v2.1 envelope: `{ op, params }` or `{ cmd, params }`, with `{ code, data, message? }` replies. This client does not add native operations to a server.

```ts
import { VeHttpClient } from '@ve/sdk';

const ve = new VeHttpClient('http://127.0.0.1:12000', {
  timeout: 5000,
  maxResponseBytes: 4 * 1024 * 1024,
});
const controller = new AbortController();
const value = await ve.get('config/value', { signal: controller.signal });
const children = await ve.children('config');
const operation = await ve.describe('children');
```

## Read metadata and child windows

On a service advertising the current `meta` and `children` contract:

```ts
const metadata = await ve.meta('config', { signal: controller.signal });
console.log(metadata.type, metadata.count); // native type and direct child count

const page = await ve.children(
  'config',
  { offset: 0, limit: 8, meta: true }, // VE operation parameters
  { signal: controller.signal }, // HTTP request controls
);
for (const child of page.children ?? []) {
  console.log(child.key, child.type, child.count);
}
```

`meta` does not transfer the node's value. `children` returns keys by default;
`meta:true` adds each child's type/count, and singular `value:true` adds its value.
Both flags default to false. An empty range remains `children:null` in the SDK;
the consuming UI can normalize it to `[]`. Parent count comes from `meta(path)`,
not the page length. No total/index/name/path fields are added to the native reply.

Offsets are zero-based; a negative offset counts from the end. To page forward,
add the returned entry count to the normalized starting offset. A `name#N` key
uses N as its same-name occurrence, not its global child index. Separate requests
are not a snapshot if the tree changes between them.

From the SDK source checkout, run the complete read-only head/tail example after configuring a new service:

```sh
npm run example:meta -- http://127.0.0.1:12000 config
```

The example checks operation signatures first and fails if this contract is not
advertised. It does not fall back to downloading the entire branch. Older
services may ignore unknown flags, so a successful call alone does not prove a
window was honored. WebSocket clients use the same native operations:

```ts
import { VeWsClient } from '@ve/sdk';

const ws = new VeWsClient({ url: 'ws://127.0.0.1:12100' });
ws.onState(async (connected) => {
  if (!connected) return;
  try {
    const metadata = await ws.meta('config');
    const page = await ws.children('config', { limit: 8, meta: true });
    console.log(metadata, page.children ?? []);
  } catch (error) {
    console.error(error);
  }
});
ws.connect(); // call ws.disconnect() when the application is done
```

## Request options

The constructor accepts an optional second argument with `timeout` (milliseconds), `maxResponseBytes` (decoded response-body bytes), and an injectable `fetchImpl`. Both limits default to zero, meaning disabled, so existing callers keep their previous behavior. Limits must be non-negative; the byte limit must be a safe integer.

HTTP methods accept a final request-options argument with `signal`, `timeout`, and `maxResponseBytes`. In `children(path, childrenOptions?, requestOptions?)`, the second argument is sent as VE operation parameters and the third controls the HTTP request. Per-request limits override client defaults; zero explicitly disables a default. A timeout covers both headers and streamed body reads. Cancelling or timing out a request does not abort a caller-owned signal. Abort listeners and deadline timers are released when a request finishes.

Cancellation preserves the caller's reason. A deadline rejects with `TimeoutError`; an oversized body rejects with `VeResponseSizeError`, which exposes `limit` and `received`. HTTP errors and negative VE reply codes retain their existing handling.

## Raw JSON and numeric precision

```ts
const wire = await ve.sendRaw({ op: 'export', params: { path: 'config', depth: 2 } });
```

`sendRaw` returns the complete response envelope as text without JSON parsing or stringifying. It preserves int64 and decimal tokens exactly. HTTP errors still reject, while a negative VE code remains in the returned envelope for the caller to inspect.

`send`, `call`, and typed convenience methods use ordinary JSON.parse and therefore JavaScript Number. They do not preserve integers above the safe range. Use the raw channel and an appropriate precision-preserving consumer when those values matter. `exportRest` also returns text, using the existing REST path semantics.

## Capability discovery

`describe(name, options?)` reads the existing `describe` operation. The returned input/output schemas advertise an operation's contract. The `commands` operation lists user commands and is not a complete list of standard operations or supported parameters.

A service that does not support discovery, a timeout, or an invalid signature leaves a capability unknown. A parameter appearing in a signature is advertised, not proof that its runtime behavior has been tested. Do not enable bounded reads merely because a legacy server ignores unknown parameters.

## Checks

```sh
npm install
npm run build
npm test
```

Tests use Node.js 24+ to load TypeScript source directly. They verify HTTP client behavior with controlled HTTP responses; they are not evidence of native VE traversal performance or new service capabilities.
