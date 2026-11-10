import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { once } from 'node:events';
import { VeHttpClient, VeResponseSizeError } from '../src/http.ts';

async function fixture(t, handler) {
  const server = createServer(handler);
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  t.after(async () => {
    server.closeAllConnections();
    await new Promise((resolve) => server.close(resolve));
  });
  return `http://127.0.0.1:${server.address().port}`;
}

test('existing calls retain v2.1 envelopes, defaults and falsy values', async (t) => {
  const calls = [];
  const base = await fixture(t, async (req, res) => {
    let text = '';
    for await (const chunk of req) text += chunk;
    const message = JSON.parse(text);
    calls.push(message);
    res.end(
      JSON.stringify({
        code: 0,
        data:
          message.op === 'get'
            ? { value: 0 }
            : message.op === 'export'
              ? { tree: false }
              : { children: [] },
      }),
    );
  });
  const client = new VeHttpClient(base + '/');
  assert.equal(await client.get('value'), 0);
  assert.equal(await client.export('value'), false);
  assert.deepEqual(await client.children('value'), { children: [] });
  assert.deepEqual(calls, [
    { op: 'get', params: { path: 'value' } },
    { op: 'export', params: { path: 'value', depth: -1 } },
    { op: 'children', params: { path: 'value' } },
  ]);
});

test('describe reads the existing operation and reports native failures', async (t) => {
  const base = await fixture(t, async (req, res) => {
    let text = '';
    for await (const chunk of req) text += chunk;
    const message = JSON.parse(text);
    assert.equal(message.op, 'describe');
    res.end(
      JSON.stringify(
        message.params.name === 'children'
          ? {
              code: 0,
              data: {
                name: 'children',
                input_schema: { type: 'object', properties: { path: { type: 'string' } } },
              },
            }
          : { code: -3, message: 'not found: schema' },
      ),
    );
  });
  const client = new VeHttpClient(base);
  assert.equal((await client.describe('children')).input_schema.properties.path.type, 'string');
  await assert.rejects(client.describe('schema'), /-3: not found: schema/);
});

test('raw replies preserve numeric tokens and UTF-8 across streamed chunks', async (t) => {
  const wire =
    '{"code":0,"data":{"tree":{"large":9007199254740993,"double":1.234567890123456789,"text":"节点"}}}';
  const bytes = new TextEncoder().encode(wire);
  const base = await fixture(t, (_req, res) => {
    for (const byte of bytes) res.write(Uint8Array.of(byte));
    res.end();
  });
  const client = new VeHttpClient(base, { maxResponseBytes: bytes.length });
  assert.equal(await client.sendRaw({ op: 'export', params: { path: 'value', depth: 1 } }), wire);
  assert.deepEqual(await client.exportRest('value'), wire);
});

test('response limits count bytes, cancel the stream and can be overridden per request', async (t) => {
  const text = '节点';
  const base = await fixture(t, (_req, res) => res.end(text));
  const client = new VeHttpClient(base, { maxResponseBytes: 5 });
  await assert.rejects(
    client.sendRaw({ op: 'get' }),
    (error) => error instanceof VeResponseSizeError && error.limit === 5 && error.received === 6,
  );
  assert.equal(await client.sendRaw({ op: 'get' }, { maxResponseBytes: 6 }), text);
  assert.equal(await client.sendRaw({ op: 'get' }, { maxResponseBytes: 0 }), text);
});

test('pre-aborted requests do not call fetch and preserve the cancellation reason', async () => {
  let calls = 0;
  const client = new VeHttpClient('http://unused', {
    fetchImpl: async () => {
      calls++;
      return new Response('{}');
    },
  });
  const controller = new AbortController();
  const reason = new Error('navigation changed');
  controller.abort(reason);
  await assert.rejects(
    client.get('value', { signal: controller.signal }),
    (error) => error === reason,
  );
  assert.equal(calls, 0);
});

test('caller cancellation interrupts a response body', async (t) => {
  let started;
  const receiving = new Promise((resolve) => {
    started = resolve;
  });
  const base = await fixture(t, (_req, res) => {
    res.write('{"code":0,"data":{"value":');
    started();
  });
  const client = new VeHttpClient(base);
  const controller = new AbortController();
  const pending = client.get('value', { signal: controller.signal });
  await receiving;
  controller.abort();
  await assert.rejects(pending, (error) => error.name === 'AbortError');
});

test('a response that exceeds the limit cancels the remaining body', async () => {
  let cancelled = false;
  const client = new VeHttpClient('http://unused', {
    maxResponseBytes: 5,
    fetchImpl: async () =>
      new Response(
        new ReadableStream({
          start(controller) {
            controller.enqueue(new Uint8Array(6));
          },
          cancel() {
            cancelled = true;
          },
        }),
      ),
  });
  await assert.rejects(client.sendRaw({ op: 'get' }), VeResponseSizeError);
  assert.equal(cancelled, true);
});

test('timeout covers response-body reads and does not abort the caller signal', async (t) => {
  const base = await fixture(t, (_req, res) => res.write('{"code":0,"data":{"value":'));
  const controller = new AbortController();
  const client = new VeHttpClient(base, { timeout: 25 });
  await assert.rejects(
    client.get('value', { signal: controller.signal }),
    (error) => error.name === 'TimeoutError',
  );
  assert.equal(controller.signal.aborted, false);
});

test('per-request timeout zero disables the client deadline', async (t) => {
  const base = await fixture(t, (_req, res) =>
    setTimeout(() => res.end('{"code":0,"data":{"value":false}}'), 50),
  );
  const client = new VeHttpClient(base, { timeout: 5 });
  assert.equal(await client.get('value', { timeout: 0 }), false);
});

test('HTTP and malformed-JSON errors remain distinct from VE errors', async (t) => {
  const base = await fixture(t, (req, res) => {
    if (req.url === '/health') {
      res.statusCode = 503;
      res.end('offline');
    } else res.end('not-json');
  });
  const client = new VeHttpClient(base);
  await assert.rejects(client.health(), /HTTP 503 \/health: offline/);
  await assert.rejects(client.get('value'), /Invalid JSON from \/ve/);
});

test('invalid request limits fail before network I/O', async () => {
  for (const options of [
    { timeout: -1 },
    { timeout: Infinity },
    { maxResponseBytes: 1.5 },
    { maxResponseBytes: -1 },
  ])
    assert.throws(() => new VeHttpClient('', options), RangeError);
  const client = new VeHttpClient('http://unused', {
    fetchImpl: async () => {
      assert.fail('unexpected fetch');
    },
  });
  await assert.rejects(client.get('value', { timeout: NaN }), RangeError);
});
