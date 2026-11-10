import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { once } from 'node:events';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { fileURLToPath } from 'node:url';
import { VeHttpClient } from '../dist/index.js';

async function fixture(t, reply) {
  const calls = [];
  const server = createServer(async (req, res) => {
    let text = '';
    for await (const chunk of req) text += chunk;
    const message = JSON.parse(text);
    calls.push(message);
    res.setHeader('Content-Type', 'application/json');
    res.end(JSON.stringify({ code: 0, data: reply(message) }));
  });
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  t.after(async () => {
    server.closeAllConnections();
    await new Promise((resolve) => server.close(resolve));
  });
  return { base: `http://127.0.0.1:${server.address().port}`, calls };
}

test('meta and window reads retain the native contract and separate HTTP request controls', async (t) => {
  const { base, calls } = await fixture(t, ({ op, params }) => {
    if (op === 'meta') return { type: 'Dict', count: 3 };
    if (params.limit === 0) return { children: null };
    if (params.value) return { children: [{ key: 'item#0', value: false }] };
    return {
      children: [
        { key: 'item#1', type: 'Int', count: 0 },
        { key: '#2', type: 'Null', count: 0 },
      ],
    };
  });
  const client = new VeHttpClient(base, { maxResponseBytes: 1 });
  const request = { signal: new AbortController().signal, timeout: 2000, maxResponseBytes: 4096 };
  assert.deepEqual(await client.meta('items', request), { type: 'Dict', count: 3 });
  const options = { offset: -2, limit: 2, value: false, meta: true };
  const page = await client.children('items', options, request);
  assert.deepEqual(page, {
    children: [
      { key: 'item#1', type: 'Int', count: 0 },
      { key: '#2', type: 'Null', count: 0 },
    ],
  });
  assert.deepEqual(options, { offset: -2, limit: 2, value: false, meta: true });
  assert.deepEqual(await client.children('items', { limit: 0 }, request), { children: null });
  assert.deepEqual(await client.children('items', { limit: 1, value: true }, request), {
    children: [{ key: 'item#0', value: false }],
  });
  assert.deepEqual(calls, [
    { op: 'meta', params: { path: 'items' } },
    { op: 'children', params: { offset: -2, limit: 2, value: false, meta: true, path: 'items' } },
    { op: 'children', params: { limit: 0, path: 'items' } },
    { op: 'children', params: { limit: 1, value: true, path: 'items' } },
  ]);
});

test('meta and children preserve cancellation reasons before sending any request', async () => {
  const client = new VeHttpClient('http://unused', {
    fetchImpl: async () => assert.fail('unexpected fetch'),
  });
  const controller = new AbortController();
  const reason = new Error('navigation changed');
  controller.abort(reason);
  await assert.rejects(
    client.meta('items', { signal: controller.signal }),
    (error) => error === reason,
  );
  await assert.rejects(
    client.children('items', { limit: 8, meta: true }, { signal: controller.signal }),
    (error) => error === reason,
  );
});

test('the runnable meta example reads head/tail windows and normalizes null only in its consumer', async (t) => {
  const { base, calls } = await fixture(t, ({ op, params }) => {
    if (op === 'describe')
      return {
        name: params.name,
        input_schema: {
          properties: params.name === 'children' ? { value: {}, meta: {} } : { path: {} },
        },
      };
    if (op === 'meta') return { type: 'Null', count: params.path === 'empty' ? 0 : 10 };
    if (params.path === 'empty') return { children: null };
    return {
      children: Array.from({ length: 8 }, (_, index) => ({
        key: `#${params.offset + index}`,
        type: 'Int',
        count: 0,
      })),
    };
  });
  const example = fileURLToPath(new URL('../examples/read-meta.mjs', import.meta.url));
  const { stdout } = await promisify(execFile)(process.execPath, [example, base, 'items']);
  const result = JSON.parse(stdout);
  assert.equal(result.metadata.count, 10);
  assert.deepEqual(
    result.first.map((child) => child.index),
    [0, 1, 2, 3, 4, 5, 6, 7],
  );
  assert.deepEqual(
    result.last.map((child) => child.index),
    [2, 3, 4, 5, 6, 7, 8, 9],
  );
  const empty = JSON.parse(
    (await promisify(execFile)(process.execPath, [example, base, 'empty'])).stdout,
  );
  assert.deepEqual(empty.first, []);
  assert.deepEqual(empty.last, []);
  assert.ok(
    calls
      .filter(({ op }) => op === 'children')
      .every(({ params }) => params.meta === true && !params.value),
  );
});

test('the example refuses an older children signature without downloading a whole branch', async (t) => {
  const { base, calls } = await fixture(t, ({ params }) => ({
    name: params.name,
    input_schema: { properties: { path: { type: 'string' } } },
  }));
  const example = fileURLToPath(new URL('../examples/read-meta.mjs', import.meta.url));
  await assert.rejects(
    promisify(execFile)(process.execPath, [example, base, 'items']),
    (error) => error.code === 1 && /does not advertise/.test(error.stderr),
  );
  assert.ok(calls.every(({ op }) => op === 'describe'));
});
