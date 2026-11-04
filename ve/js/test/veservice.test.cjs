const assert = require('node:assert/strict');
const { readFileSync } = require('node:fs');
const { join } = require('node:path');
const { test } = require('node:test');
const vm = require('node:vm');

const source = readFileSync(join(__dirname, '../veservice.js'), 'utf8');

function setup(options = {}) {
    let now = 0, nextTimer = 0;
    const timers = new Map();
    const sockets = [];
    const initialized = [];

    class Socket {
        static OPEN = 1;
        constructor(url) {
            if (options.constructError) throw new Error('Invalid URL');
            this.url = url;
            this.readyState = 0;
            this.sent = [];
            this.closeCalls = 0;
            sockets.push(this);
        }
        open() { this.readyState = 1; this.onopen(); }
        message(message) { this.onmessage({ data: JSON.stringify(message) }); }
        error() { this.onerror({ type: 'error' }); }
        closed() {
            this.readyState = 3;
            this.onclose({ code: 1006, reason: '', wasClean: false });
        }
        close() { this.closeCalls++; if (this.readyState < 2) this.readyState = 2; }
        send(data) {
            if (this.readyState !== 1) throw new Error('Socket not open');
            this.sent.push(JSON.parse(data));
        }
    }

    const context = vm.createContext({
        WebSocket: options.unsupported ? undefined : Socket,
        console: { log() {}, warn() {}, error() {} },
        setTimeout(fn, delay) {
            const id = ++nextTimer;
            timers.set(id, { fn, due: now + delay });
            return id;
        },
        clearTimeout(id) { timers.delete(id); },
        _ve_ws_url: 'ws://injected:12100',
        veServiceCallback(service) { initialized.push(service); },
    });
    if (!options.auto) context.veService = {};
    vm.runInContext(source, context);
    const service = options.auto ? context.veService : new context.VEService();

    function tick(ms) {
        const end = now + ms;
        while (timers.size) {
            const [id, timer] = [...timers].sort((a, b) => a[1].due - b[1].due)[0];
            if (timer.due > end) break;
            timers.delete(id);
            now = timer.due;
            timer.fn();
        }
        now = end;
    }
    function nextDelay() { return Math.min(...[...timers.values()].map(timer => timer.due - now)); }
    async function connect() {
        const promise = service.connect();
        sockets.at(-1).open();
        await promise;
        return sockets.at(-1);
    }
    return { service, sockets, timers, initialized, context, tick, nextDelay, connect, options };
}

test('concurrent and repeated connect calls create one socket', async () => {
    const h = setup();
    const states = [];
    h.service.onConnectionChange(value => states.push(value));
    const first = h.service.connect();
    assert.equal(h.service.connect(), first);
    assert.equal(h.sockets.length, 1);
    h.sockets[0].open();
    await first;
    await h.service.connect();
    assert.equal(h.sockets.length, 1);
    assert.deepEqual(states, [true]);
    assert.equal(h.timers.size, 0);
    h.service.disconnect();
});

test('unsupported WebSocket and constructor failures leave connect retryable', async () => {
    const unsupported = setup({ unsupported: true });
    await assert.rejects(unsupported.service.connect(), /WebSocket unsupported/);
    assert.equal(unsupported.timers.size, 0);
    const h = setup({ constructError: true });
    await assert.rejects(h.service.connect(), /Invalid URL/);
    assert.equal(h.timers.size, 0);
    assert.equal(h.service.transport, null);
    h.options.constructError = false;
    await h.connect();
    h.service.disconnect();
});

test('connection timeout closes the socket without retrying the first attempt', async () => {
    const h = setup();
    h.service.connectTimeout = 50;
    const rejected = assert.rejects(h.service.connect(), /connection timeout/);
    h.tick(49);
    assert.equal(h.sockets[0].readyState, 0);
    h.tick(1);
    await rejected;
    assert.equal(h.sockets[0].closeCalls, 1);
    assert.equal(h.service.transport, null);
    h.sockets[0].open();
    h.sockets[0].closed();
    assert.equal(h.service.isConnected, false);
    assert.equal(h.timers.size, 0);
    await h.connect();
    h.service.disconnect();
});

test('close without error and error without close both reject a pending connection', async () => {
    for (const event of ['closed', 'error']) {
        const h = setup();
        const rejected = assert.rejects(h.service.connect(), /connection (closed|error)/);
        h.sockets[0][event]();
        await rejected;
        assert.equal(h.service.transport, null);
        assert.equal(h.timers.size, 0);
        await h.connect();
        h.service.disconnect();
    }
});

test('disconnect cancels a pending connection immediately and is repeatable', async () => {
    const h = setup();
    const rejected = assert.rejects(h.service.connect(), /disconnected/);
    const old = h.sockets[0];
    h.service.disconnect();
    h.service.disconnect();
    await rejected;
    assert.equal(h.timers.size, 0);
    old.closed();
    assert.equal(h.timers.size, 0);
    const socket = await h.connect();
    socket.closed();
    assert.equal(h.nextDelay(), 1000);
    h.service.disconnect();
    h.tick(10000);
    assert.equal(h.sockets.length, 2);
});

test('disconnect rejects in-flight requests, clears subscriptions and notifies once', async () => {
    const h = setup();
    const states = [];
    h.service.onConnectionChange(value => states.push(value));
    const socket = await h.connect();
    h.service.subscribe('robot/state', () => {});
    const first = assert.rejects(h.service.get('robot/state'), /disconnected/);
    const second = assert.rejects(h.service.set('robot/state', 1), /disconnected/);
    h.service.disconnect();
    await Promise.all([first, second]);
    socket.closed();
    h.service.disconnect();
    h.tick(10000);
    assert.deepEqual(states, [true, false]);
    assert.equal(h.service.pendingRequests.size, 0);
    assert.equal(h.service.subscriptions.size, 0);
    assert.equal(h.timers.size, 0);
});

test('late events from an old socket cannot affect a replacement connection', async () => {
    const h = setup();
    const states = [], messages = [];
    h.service.onConnectionChange(value => states.push(value));
    h.service.onMessage(message => messages.push(message));
    const old = await h.connect();
    h.service.disconnect();
    const socket = await h.connect();
    const reply = h.service.get('robot/state');
    const id = socket.sent.at(-1).id;
    old.message({ id, code: 0, data: { value: 'stale' } });
    old.message({ event: 'node.changed', path: 'robot/state', data: 'stale' });
    old.error();
    old.closed();
    old.open();
    assert.equal(h.service.transport, socket);
    assert.equal(h.service.isConnected, true);
    assert.equal(h.service.pendingRequests.size, 1);
    assert.deepEqual(states, [true, false, true]);
    assert.deepEqual(messages, []);
    socket.message({ id, code: 0, data: { value: 42 } });
    assert.equal(await reply, 42);
    assert.equal(h.timers.size, 0);
    h.service.disconnect();
});

test('request replies, failures, timeouts and send errors clean up their timers', async () => {
    const h = setup();
    const socket = await h.connect();
    const reply = h.service.get('robot.state');
    assert.equal(socket.sent.at(-1).params.path, 'robot/state');
    socket.message({ id: socket.sent.at(-1).id, code: 0, data: { value: false } });
    assert.equal(await reply, false);
    const failure = assert.rejects(h.service.get('missing'), /not found/);
    socket.message({ id: socket.sent.at(-1).id, code: -3, message: 'not found' });
    await failure;
    const timeout = assert.rejects(h.service.get('slow'), /Request timeout/);
    h.tick(h.service.timeout);
    await timeout;
    socket.send = () => { throw new Error('Send failed'); };
    await assert.rejects(h.service.get('robot/state'), /Send failed/);
    socket.readyState = 2;
    await assert.rejects(h.service.get('robot/state'), /not connected/);
    assert.equal(h.service.pendingRequests.size, 0);
    assert.equal(h.timers.size, 0);
    h.service.disconnect();
});

test('a remote close rejects in-flight requests before retrying', async () => {
    const h = setup();
    const socket = await h.connect();
    const rejected = assert.rejects(h.service.get('robot/state'), /connection closed/);
    socket.closed();
    await rejected;
    assert.equal(h.service.pendingRequests.size, 0);
    assert.equal(h.timers.size, 1);
    assert.equal(h.nextDelay(), 1000);
    h.service.disconnect();
});

test('reconnect backs off to its limit and restores subscriptions after recovery', async () => {
    const h = setup();
    const values = [];
    h.service.subscribe('robot/state', (value, path) => values.push([value, path]), { depth: 0, once: true, immediate: true });
    const first = await h.connect();
    first.message({ id: first.sent[0].id, code: 0 });
    first.closed();
    for (const delay of [1000, 2000, 4000, 8000, 8000]) {
        assert.equal(h.timers.size, 1);
        assert.equal(h.nextDelay(), delay);
        h.tick(delay);
        h.sockets.at(-1).closed();
    }
    h.tick(8000);
    const socket = h.sockets.at(-1);
    socket.open();
    assert.deepEqual(socket.sent[0].params, { path: 'robot/state', depth: 0, once: true, immediate: true });
    socket.message({ id: socket.sent[0].id, code: 0 });
    socket.message({ event: 'node.changed', path: 'robot/state', data: 7 });
    assert.deepEqual(values, [[7, 'robot/state']]);
    assert.equal(h.timers.size, 0);
    socket.closed();
    assert.equal(h.nextDelay(), 1000);
    h.service.disconnect();
});

test('retry timeouts and error followed by close each schedule only one retry', async () => {
    const h = setup();
    const socket = await h.connect();
    socket.closed();
    h.tick(1000);
    h.sockets.at(-1).error();
    h.sockets.at(-1).closed();
    assert.equal(h.timers.size, 1);
    assert.equal(h.nextDelay(), 2000);
    h.tick(2000);
    h.tick(h.service.connectTimeout);
    assert.equal(h.timers.size, 1);
    assert.equal(h.nextDelay(), 4000);
    h.options.constructError = true;
    h.tick(4000);
    assert.equal(h.timers.size, 1);
    assert.equal(h.nextDelay(), 8000);
    h.service.disconnect();
});

test('an explicit connect replaces a scheduled retry', async () => {
    const h = setup();
    const socket = await h.connect();
    socket.closed();
    await h.connect();
    h.tick(1000);
    assert.equal(h.sockets.length, 2);
    assert.equal(h.timers.size, 0);
    h.service.disconnect();
});

test('connect handles a socket already closing before its close event arrives', async () => {
    const h = setup();
    const old = await h.connect();
    old.readyState = 2;
    const rejected = assert.rejects(h.service.get('robot/state'), /not connected/);
    const socket = await h.connect();
    await rejected;
    old.closed();
    assert.equal(h.service.transport, socket);
    assert.equal(h.timers.size, 0);
    h.service.disconnect();
});

test('a disconnect inside a connection callback cancels the scheduled retry', async () => {
    const h = setup();
    h.service.onConnectionChange(connected => { if (!connected) h.service.disconnect(); });
    const socket = await h.connect();
    socket.closed();
    h.tick(10000);
    assert.equal(h.sockets.length, 1);
    assert.equal(h.timers.size, 0);
});

test('a connection callback can reconnect immediately without a duplicate retry', async () => {
    const h = setup();
    const socket = await h.connect();
    let retry;
    const stop = h.service.onConnectionChange(connected => { if (!connected) retry = h.service.connect(); });
    socket.closed();
    h.sockets.at(-1).open();
    await retry;
    h.tick(10000);
    assert.equal(h.sockets.length, 2);
    assert.equal(h.timers.size, 0);
    stop();
    h.service.disconnect();
});

test('injected initialization runs once, with the configured URL, across reconnects and reinjection', async () => {
    const h = setup({ auto: true });
    assert.equal(h.sockets[0].url, 'ws://injected:12100');
    h.sockets[0].open();
    await Promise.resolve();
    assert.deepEqual(h.initialized, [h.service]);
    h.sockets[0].closed();
    h.tick(1000);
    h.sockets.at(-1).open();
    await Promise.resolve();
    vm.runInContext(source, h.context);
    assert.equal(h.sockets.length, 2);
    assert.deepEqual(h.initialized, [h.service]);
    h.service.disconnect();
});

test('failed injected startup neither calls initialization nor retries in the background', async () => {
    const h = setup({ auto: true });
    h.tick(h.service.connectTimeout);
    await Promise.resolve();
    h.tick(10000);
    assert.deepEqual(h.initialized, []);
    assert.equal(h.sockets.length, 1);
    assert.equal(h.timers.size, 0);
});
