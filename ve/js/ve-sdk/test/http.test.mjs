import assert from 'node:assert/strict';
import { test } from 'node:test';
import { VeHttpClient } from '../dist/index.js';

test('importRest accepts the empty response from a successful merge import', async (t) => {
  const fetch = t.mock.method(globalThis, 'fetch', async () => new Response('', { status: 200 }));
  const client = new VeHttpClient('http://localhost:12000/');

  assert.equal(await client.importRest('/test/tree', '{"a":1}'), undefined);

  const [url, init] = fetch.mock.calls[0].arguments;
  assert.equal(url, 'http://localhost:12000/at/test/tree');
  assert.equal(init.method, 'PUT');
  assert.equal(init.headers['Content-Type'], 'application/json');
  assert.equal(init.body, '{"a":1}');
});

test('importRest reports HTTP failures even when the body is not JSON', async (t) => {
  t.mock.method(globalThis, 'fetch', async () => new Response('node not found', { status: 404 }));
  const client = new VeHttpClient('http://localhost:12000');

  await assert.rejects(client.importRest('missing', '{}'), /HTTP 404 \/at\/missing: node not found/);
});
