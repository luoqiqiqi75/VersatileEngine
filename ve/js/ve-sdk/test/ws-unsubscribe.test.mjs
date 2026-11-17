import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { test } from 'node:test';
import vm from 'node:vm';
import { VeWsClient } from '../dist/index.js';

const legacySource = readFileSync(new URL('../../veservice.js', import.meta.url), 'utf8');

async function fixture(t, implementation) {
  const sockets = [];
  class Socket {
    static OPEN = 1;
    constructor() {
      this.readyState = 0;
      this.sent = [];
      sockets.push(this);
    }
    open() { this.readyState = 1; this.onopen(); }
    send(raw) { this.sent.push(JSON.parse(raw)); }
    message(message) { this.onmessage({ data: JSON.stringify(message) }); }
    close() { this.readyState = 3; }
  }

  let client;
  const originalWebSocket = globalThis.WebSocket;
  if (implementation === 'SDK') {
    globalThis.WebSocket = Socket;
    client = new VeWsClient({ url: 'ws://test', timeout: 500 });
  } else {
    const context = vm.createContext({
      WebSocket: Socket, setTimeout, clearTimeout, console, veService: {},
    });
    vm.runInContext(legacySource, context);
    client = new context.VEService('ws://test');
    client.timeout = 500;
  }
  t.after(() => {
    client.disconnect();
    if (implementation === 'SDK') globalThis.WebSocket = originalWebSocket;
  });

  const connecting = client.connect();
  const socket = sockets.at(-1);
  socket.open();
  await connecting;

  return {
    client,
    socket,
    pending: implementation === 'SDK' ? client.pending : client.pendingRequests,
    reply(code = 0) {
      socket.message({ id: socket.sent.at(-1).id, code });
    },
    push(path) {
      socket.message({ event: 'node.changed', path, data: 42 });
    },
    operations() { return socket.sent.map(message => message.op); },
  };
}

for (const implementation of ['SDK', 'veservice.js']) {
  test(`${implementation}: an in-flight push does not block the unsubscribe reply`, async (t) => {
    const h = await fixture(t, implementation);
    let cancelled = 0, active = 0;
    const unsubscribe = h.client.subscribe('test/watch', () => cancelled++);
    h.reply();
    h.client.subscribe('test/other', () => active++);
    h.reply();

    // No acknowledgement has arrived: unsubscribe already returns to its caller.
    assert.equal(unsubscribe(), undefined);
    assert.equal(h.pending.size, 1);
    assert.equal(h.socket.sent.at(-1).op, 'unsubscribe');
    assert.deepEqual(h.socket.sent.at(-1).params, { path: 'test/watch' });

    h.push('test/watch');
    h.push('test/other');
    h.reply();

    assert.equal(cancelled, 0);
    assert.equal(active, 1);
    assert.equal(h.pending.size, 0);
    unsubscribe();
    assert.deepEqual(h.operations(), ['subscribe', 'subscribe', 'unsubscribe']);
  });

  test(`${implementation}: only the last callback sends an unsubscribe request`, async (t) => {
    const h = await fixture(t, implementation);
    let cancelled = 0, active = 0;
    const unsubscribeFirst = h.client.subscribe('test/watch', () => cancelled++);
    h.reply();
    const unsubscribeLast = h.client.subscribe('test/watch', () => active++);

    unsubscribeFirst();
    assert.deepEqual(h.operations(), ['subscribe']);
    h.push('test/watch');
    assert.equal(cancelled, 0);
    assert.equal(active, 1);

    unsubscribeLast();
    h.push('test/watch');
    h.reply();
    assert.equal(active, 1);
    assert.equal(h.pending.size, 0);
    unsubscribeFirst();
    unsubscribeLast();
    assert.deepEqual(h.operations(), ['subscribe', 'unsubscribe']);
  });

  test(`${implementation}: path unsubscribe processes a reply for an already deleted node`, async (t) => {
    const h = await fixture(t, implementation);
    let callbacks = 0;
    h.client.subscribe('test/watch', () => callbacks++);
    h.reply();
    h.client.subscribe('test/watch', () => callbacks++);

    assert.equal(h.client.unsubscribe('test/watch'), undefined);
    assert.equal(h.pending.size, 1);
    h.push('test/watch');
    h.reply(-3);

    assert.equal(callbacks, 0);
    assert.equal(h.pending.size, 0);
    assert.deepEqual(h.operations(), ['subscribe', 'unsubscribe']);
  });
}
