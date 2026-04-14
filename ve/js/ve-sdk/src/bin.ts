/**
 * VeBinTcpClient — MessagePack binary IPC client for VE BinTcpServer.
 *
 * Protocol: frame-based MessagePack over TCP (default port 11000).
 *
 * Frame layout:
 *   [flag : 1 byte] [length : 4 bytes LE] [payload : msgpack]
 *
 * Flag bits [7:6]:
 *   0x00  REQUEST   — client → server
 *   0x40  RESPONSE  — server → client (reply to a request)
 *   0x80  NOTIFY    — server → client (subscription push)
 *   0xC0  ERROR     — server → client (error reply)
 *
 * Request payload (Dict):  { op, path, id, ?args, ?data }
 * Response payload (Dict): { id, code, ?data }
 * Notify payload (Dict):   { path, value }
 *
 * Requires Node.js `net` module — this client is for server-side (Node / Bun / Deno) use.
 * Browser environments should use VeWsClient instead.
 *
 * @example
 * ```ts
 * import { VeBinTcpClient } from '@ve/sdk';
 *
 * const client = new VeBinTcpClient({ host: '127.0.0.1', port: 11000 });
 * await client.connect();
 *
 * const value = await client.get('movax/robot/global/state/power');
 *
 * client.subscribe('movax/robot/global/state/power', (value) => {
 *   console.log('power changed:', value);
 * });
 *
 * await client.set('test/hello', 42);
 * await client.close();
 * ```
 */

import type { VarValue } from './types';

// ---------------------------------------------------------------------------
// Inline msgpack encode/decode (minimal, no external dependency)
// Supports: null, bool, int (up to 32-bit), double, string, bin, array, map.
// Enough for VE Var payloads. No ext types.
// ---------------------------------------------------------------------------

function msgpackEncode(value: unknown): Uint8Array {
  const parts: Uint8Array[] = [];
  encodeValue(value, parts);
  const total = parts.reduce((s, p) => s + p.length, 0);
  const out = new Uint8Array(total);
  let offset = 0;
  for (const p of parts) {
    out.set(p, offset);
    offset += p.length;
  }
  return out;
}

function encodeValue(value: unknown, parts: Uint8Array[]): void {
  if (value === null || value === undefined) {
    parts.push(new Uint8Array([0xc0]));
    return;
  }
  if (typeof value === 'boolean') {
    parts.push(new Uint8Array([value ? 0xc3 : 0xc2]));
    return;
  }
  if (typeof value === 'number') {
    if (Number.isInteger(value)) {
      encodeInt(value, parts);
    } else {
      // float64
      const buf = new ArrayBuffer(9);
      const view = new DataView(buf);
      view.setUint8(0, 0xcb);
      view.setFloat64(1, value, false); // big-endian
      parts.push(new Uint8Array(buf));
    }
    return;
  }
  if (typeof value === 'string') {
    const encoded = new TextEncoder().encode(value);
    const len = encoded.length;
    if (len < 32) {
      parts.push(new Uint8Array([0xa0 | len]));
    } else if (len < 256) {
      parts.push(new Uint8Array([0xd9, len]));
    } else if (len < 65536) {
      const h = new Uint8Array(3);
      h[0] = 0xda;
      h[1] = (len >> 8) & 0xff;
      h[2] = len & 0xff;
      parts.push(h);
    } else {
      const h = new Uint8Array(5);
      h[0] = 0xdb;
      new DataView(h.buffer).setUint32(1, len, false);
      parts.push(h);
    }
    parts.push(encoded);
    return;
  }
  if (value instanceof Uint8Array) {
    const len = value.length;
    if (len < 256) {
      parts.push(new Uint8Array([0xc4, len]));
    } else if (len < 65536) {
      const h = new Uint8Array(3);
      h[0] = 0xc5;
      h[1] = (len >> 8) & 0xff;
      h[2] = len & 0xff;
      parts.push(h);
    } else {
      const h = new Uint8Array(5);
      h[0] = 0xc6;
      new DataView(h.buffer).setUint32(1, len, false);
      parts.push(h);
    }
    parts.push(value);
    return;
  }
  if (Array.isArray(value)) {
    const len = value.length;
    if (len < 16) {
      parts.push(new Uint8Array([0x90 | len]));
    } else if (len < 65536) {
      const h = new Uint8Array(3);
      h[0] = 0xdc;
      h[1] = (len >> 8) & 0xff;
      h[2] = len & 0xff;
      parts.push(h);
    } else {
      const h = new Uint8Array(5);
      h[0] = 0xdd;
      new DataView(h.buffer).setUint32(1, len, false);
      parts.push(h);
    }
    for (const item of value) {
      encodeValue(item, parts);
    }
    return;
  }
  if (typeof value === 'object') {
    const entries = Object.entries(value as Record<string, unknown>);
    const len = entries.length;
    if (len < 16) {
      parts.push(new Uint8Array([0x80 | len]));
    } else if (len < 65536) {
      const h = new Uint8Array(3);
      h[0] = 0xde;
      h[1] = (len >> 8) & 0xff;
      h[2] = len & 0xff;
      parts.push(h);
    } else {
      const h = new Uint8Array(5);
      h[0] = 0xdf;
      new DataView(h.buffer).setUint32(1, len, false);
      parts.push(h);
    }
    for (const [k, v] of entries) {
      encodeValue(k, parts);
      encodeValue(v, parts);
    }
    return;
  }
  // fallback: encode as string
  encodeValue(String(value), parts);
}

function encodeInt(value: number, parts: Uint8Array[]): void {
  if (value >= 0) {
    if (value < 128) {
      parts.push(new Uint8Array([value]));
    } else if (value < 256) {
      parts.push(new Uint8Array([0xcc, value]));
    } else if (value < 65536) {
      const h = new Uint8Array(3);
      h[0] = 0xcd;
      h[1] = (value >> 8) & 0xff;
      h[2] = value & 0xff;
      parts.push(h);
    } else {
      const h = new Uint8Array(5);
      h[0] = 0xce;
      new DataView(h.buffer).setUint32(1, value, false);
      parts.push(h);
    }
  } else {
    if (value >= -32) {
      parts.push(new Uint8Array([value & 0xff]));
    } else if (value >= -128) {
      parts.push(new Uint8Array([0xd0, value & 0xff]));
    } else if (value >= -32768) {
      const h = new Uint8Array(3);
      h[0] = 0xd1;
      new DataView(h.buffer).setInt16(1, value, false);
      parts.push(h);
    } else {
      const h = new Uint8Array(5);
      h[0] = 0xd2;
      new DataView(h.buffer).setInt32(1, value, false);
      parts.push(h);
    }
  }
}

interface DecodeResult {
  value: unknown;
  offset: number;
}

function msgpackDecode(buf: Uint8Array): unknown {
  return decodeValue(buf, 0).value;
}

function decodeValue(buf: Uint8Array, offset: number): DecodeResult {
  if (offset >= buf.length) throw new Error('msgpack: unexpected end of buffer');
  const byte = buf[offset];
  const view = new DataView(buf.buffer, buf.byteOffset, buf.byteLength);

  // positive fixint
  if (byte < 0x80) return { value: byte, offset: offset + 1 };
  // fixmap
  if ((byte & 0xf0) === 0x80) return decodeMap(buf, offset + 1, byte & 0x0f);
  // fixarray
  if ((byte & 0xf0) === 0x90) return decodeArray(buf, offset + 1, byte & 0x0f);
  // fixstr
  if ((byte & 0xe0) === 0xa0) return decodeStr(buf, offset + 1, byte & 0x1f);
  // negative fixint
  if (byte >= 0xe0) return { value: byte - 256, offset: offset + 1 };

  switch (byte) {
    case 0xc0: return { value: null, offset: offset + 1 };
    case 0xc2: return { value: false, offset: offset + 1 };
    case 0xc3: return { value: true, offset: offset + 1 };
    // bin 8/16/32
    case 0xc4: return decodeBin(buf, offset + 2, buf[offset + 1]);
    case 0xc5: return decodeBin(buf, offset + 3, view.getUint16(offset + 1, false));
    case 0xc6: return decodeBin(buf, offset + 5, view.getUint32(offset + 1, false));
    // float32 / float64
    case 0xca: return { value: view.getFloat32(offset + 1, false), offset: offset + 5 };
    case 0xcb: return { value: view.getFloat64(offset + 1, false), offset: offset + 9 };
    // uint 8/16/32
    case 0xcc: return { value: buf[offset + 1], offset: offset + 2 };
    case 0xcd: return { value: view.getUint16(offset + 1, false), offset: offset + 3 };
    case 0xce: return { value: view.getUint32(offset + 1, false), offset: offset + 5 };
    // int 8/16/32
    case 0xd0: return { value: view.getInt8(offset + 1), offset: offset + 2 };
    case 0xd1: return { value: view.getInt16(offset + 1, false), offset: offset + 3 };
    case 0xd2: return { value: view.getInt32(offset + 1, false), offset: offset + 5 };
    // str 8/16/32
    case 0xd9: return decodeStr(buf, offset + 2, buf[offset + 1]);
    case 0xda: return decodeStr(buf, offset + 3, view.getUint16(offset + 1, false));
    case 0xdb: return decodeStr(buf, offset + 5, view.getUint32(offset + 1, false));
    // array 16/32
    case 0xdc: return decodeArray(buf, offset + 3, view.getUint16(offset + 1, false));
    case 0xdd: return decodeArray(buf, offset + 5, view.getUint32(offset + 1, false));
    // map 16/32
    case 0xde: return decodeMap(buf, offset + 3, view.getUint16(offset + 1, false));
    case 0xdf: return decodeMap(buf, offset + 5, view.getUint32(offset + 1, false));
    // uint64 / int64 — read as Number (loses precision above 2^53)
    case 0xcf: return { value: Number(view.getBigUint64(offset + 1, false)), offset: offset + 9 };
    case 0xd3: return { value: Number(view.getBigInt64(offset + 1, false)), offset: offset + 9 };
    default:
      throw new Error(`msgpack: unsupported type 0x${byte.toString(16)}`);
  }
}

function decodeStr(buf: Uint8Array, offset: number, len: number): DecodeResult {
  const str = new TextDecoder().decode(buf.subarray(offset, offset + len));
  return { value: str, offset: offset + len };
}

function decodeBin(buf: Uint8Array, offset: number, len: number): DecodeResult {
  return { value: buf.slice(offset, offset + len), offset: offset + len };
}

function decodeArray(buf: Uint8Array, offset: number, count: number): DecodeResult {
  const arr: unknown[] = [];
  for (let i = 0; i < count; i++) {
    const r = decodeValue(buf, offset);
    arr.push(r.value);
    offset = r.offset;
  }
  return { value: arr, offset };
}

function decodeMap(buf: Uint8Array, offset: number, count: number): DecodeResult {
  const map: Record<string, unknown> = {};
  for (let i = 0; i < count; i++) {
    const kr = decodeValue(buf, offset);
    const vr = decodeValue(buf, kr.offset);
    map[String(kr.value)] = vr.value;
    offset = vr.offset;
  }
  return { value: map, offset };
}

// ---------------------------------------------------------------------------
// Frame constants
// ---------------------------------------------------------------------------

const FLAG_REQUEST = 0x00;
const FLAG_RESPONSE = 0x40;
const FLAG_NOTIFY = 0x80;
const FLAG_ERROR = 0xc0;
const FLAG_TYPE_MASK = 0xc0;

const FRAME_HEADER_SIZE = 5;

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

export type NotifyHandler = (path: string, value: VarValue) => void;
export type ConnectionHandler = (connected: boolean) => void;

export interface VeBinTcpClientOptions {
  host?: string;
  port?: number;
  /** Request timeout in ms (default: 30000) */
  timeout?: number;
  /** Auto-reconnect on disconnect (default: true) */
  autoReconnect?: boolean;
  /** Initial reconnect delay in ms (default: 1000) */
  reconnectInterval?: number;
  /** Max reconnect delay in ms (default: 30000) */
  maxReconnectInterval?: number;
}

interface PendingRequest {
  resolve: (result: { flag: number; data: Record<string, unknown> }) => void;
  reject: (error: Error) => void;
  timer: ReturnType<typeof setTimeout>;
}

// ---------------------------------------------------------------------------
// VeBinTcpClient
// ---------------------------------------------------------------------------

export class VeBinTcpClient {
  private host: string;
  private port: number;
  private timeout: number;
  private autoReconnect: boolean;
  private reconnectInterval: number;
  private maxReconnectInterval: number;
  private currentReconnectDelay: number;
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;

  private socket: import('node:net').Socket | null = null;
  private recvBuf = new Uint8Array(0);
  private nextId = 0;
  private pending = new Map<number, PendingRequest>();
  private intentionalClose = false;
  private _connected = false;

  private notifyHandlers = new Map<string, Set<NotifyHandler>>();
  private connectionHandlers = new Set<ConnectionHandler>();

  constructor(options: VeBinTcpClientOptions = {}) {
    this.host = options.host ?? '127.0.0.1';
    this.port = options.port ?? 11000;
    this.timeout = options.timeout ?? 30000;
    this.autoReconnect = options.autoReconnect ?? true;
    this.reconnectInterval = options.reconnectInterval ?? 1000;
    this.maxReconnectInterval = options.maxReconnectInterval ?? 30000;
    this.currentReconnectDelay = this.reconnectInterval;
  }

  get connected(): boolean {
    return this._connected;
  }

  // -------------------------------------------------------------------------
  // Connection
  // -------------------------------------------------------------------------

  async connect(): Promise<void> {
    if (this._connected) return;
    this.intentionalClose = false;

    const net = await import('node:net');

    return new Promise<void>((resolve, reject) => {
      const sock = new net.Socket();
      this.socket = sock;

      sock.on('connect', () => {
        this._connected = true;
        this.currentReconnectDelay = this.reconnectInterval;
        this.notifyConnectionChange(true);
        resolve();
      });

      sock.on('data', (chunk: Buffer) => {
        this.onData(chunk);
      });

      sock.on('close', () => {
        const was = this._connected;
        this._connected = false;
        this.socket = null;

        // Reject all pending requests
        for (const [id, p] of this.pending) {
          clearTimeout(p.timer);
          p.reject(new Error('Connection closed'));
        }
        this.pending.clear();

        if (was) this.notifyConnectionChange(false);
        if (!this.intentionalClose && this.autoReconnect) {
          this.scheduleReconnect();
        }
      });

      sock.on('error', (err: Error) => {
        if (!this._connected) {
          reject(err);
        }
        sock.destroy();
      });

      sock.connect(this.port, this.host);
    });
  }

  close(): void {
    this.intentionalClose = true;
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
    this.socket?.destroy();
    this.socket = null;
    this._connected = false;
  }

  // -------------------------------------------------------------------------
  // Node operations
  // -------------------------------------------------------------------------

  async get(path: string): Promise<VarValue> {
    const { flag, data } = await this.sendRequest('get', path);
    if ((flag & FLAG_TYPE_MASK) === FLAG_ERROR) return null;
    return (data.data ?? null) as VarValue;
  }

  async set(path: string, value: VarValue): Promise<boolean> {
    const { flag } = await this.sendRequest('set', path, value);
    return (flag & FLAG_TYPE_MASK) !== FLAG_ERROR;
  }

  async list(path: string): Promise<string[]> {
    const { flag, data } = await this.sendRequest('ls', path);
    if ((flag & FLAG_TYPE_MASK) === FLAG_ERROR) return [];
    const d = data.data;
    return Array.isArray(d) ? d as string[] : [];
  }

  async tree(path: string): Promise<VarValue> {
    return this.get(path);
  }

  async trigger(path: string): Promise<boolean> {
    const { flag } = await this.sendRequest('set', path);
    return (flag & FLAG_TYPE_MASK) !== FLAG_ERROR;
  }

  async command(name: string, args?: VarValue): Promise<VarValue> {
    const { flag, data } = await this.sendRequest(name, '/', args);
    if ((flag & FLAG_TYPE_MASK) === FLAG_ERROR) {
      throw new Error(String(data.data ?? 'command failed'));
    }
    return (data.data ?? null) as VarValue;
  }

  // -------------------------------------------------------------------------
  // Subscribe
  // -------------------------------------------------------------------------

  async subscribe(path: string, handler: NotifyHandler): Promise<() => void> {
    if (!this.notifyHandlers.has(path)) {
      this.notifyHandlers.set(path, new Set());
      // Send subscribe request to server
      if (this._connected) {
        await this.sendRequest('subscribe', path).catch(() => {});
      }
    }

    this.notifyHandlers.get(path)!.add(handler);

    return () => {
      const handlers = this.notifyHandlers.get(path);
      if (!handlers) return;
      handlers.delete(handler);
      if (handlers.size === 0) {
        this.notifyHandlers.delete(path);
        if (this._connected) {
          this.sendRequest('unsubscribe', path).catch(() => {});
        }
      }
    };
  }

  onConnectionChange(handler: ConnectionHandler): () => void {
    this.connectionHandlers.add(handler);
    return () => this.connectionHandlers.delete(handler);
  }

  // -------------------------------------------------------------------------
  // Internal: frame send / receive
  // -------------------------------------------------------------------------

  private sendRequest(
    op: string, path: string, data?: unknown
  ): Promise<{ flag: number; data: Record<string, unknown> }> {
    return new Promise((resolve, reject) => {
      if (!this.socket || !this._connected) {
        return reject(new Error('Not connected'));
      }

      const id = ++this.nextId;
      const payload: Record<string, unknown> = { op, path, id };
      if (data !== undefined) payload.data = data;

      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new Error(`Request timeout (${this.timeout}ms)`));
      }, this.timeout);

      this.pending.set(id, { resolve, reject, timer });

      const encoded = msgpackEncode(payload);
      const header = new Uint8Array(FRAME_HEADER_SIZE);
      header[0] = FLAG_REQUEST;
      new DataView(header.buffer).setUint32(1, encoded.length, true); // LE

      this.socket.write(header);
      this.socket.write(encoded);
    });
  }

  private onData(chunk: Buffer): void {
    // Append to recv buffer
    const newBuf = new Uint8Array(this.recvBuf.length + chunk.length);
    newBuf.set(this.recvBuf);
    newBuf.set(new Uint8Array(chunk.buffer, chunk.byteOffset, chunk.length), this.recvBuf.length);
    this.recvBuf = newBuf;

    this.processFrames();
  }

  private processFrames(): void {
    while (this.recvBuf.length >= FRAME_HEADER_SIZE) {
      const flag = this.recvBuf[0];
      const length = new DataView(
        this.recvBuf.buffer, this.recvBuf.byteOffset, this.recvBuf.byteLength
      ).getUint32(1, true); // LE

      if (this.recvBuf.length < FRAME_HEADER_SIZE + length) break; // incomplete

      const payload = this.recvBuf.slice(FRAME_HEADER_SIZE, FRAME_HEADER_SIZE + length);
      this.recvBuf = this.recvBuf.slice(FRAME_HEADER_SIZE + length);

      let msg: Record<string, unknown>;
      try {
        msg = msgpackDecode(payload) as Record<string, unknown>;
      } catch {
        continue; // skip malformed frames
      }

      const frameType = flag & FLAG_TYPE_MASK;

      if (frameType === FLAG_NOTIFY) {
        // Subscription push
        const path = String(msg.path ?? '');
        const value = msg.value as VarValue;
        const handlers = this.notifyHandlers.get(path);
        if (handlers) {
          for (const h of handlers) {
            try { h(path, value); } catch { /* ignore callback errors */ }
          }
        }
        continue;
      }

      // Response or error — match to pending request
      const id = msg.id as number | undefined;
      if (id !== undefined && this.pending.has(id)) {
        const p = this.pending.get(id)!;
        this.pending.delete(id);
        clearTimeout(p.timer);
        p.resolve({ flag, data: msg });
      }
    }
  }

  // -------------------------------------------------------------------------
  // Reconnect
  // -------------------------------------------------------------------------

  private scheduleReconnect(): void {
    if (this.reconnectTimer) return;
    this.reconnectTimer = setTimeout(async () => {
      this.reconnectTimer = null;
      try {
        await this.connect();
        // Re-subscribe all
        for (const path of this.notifyHandlers.keys()) {
          this.sendRequest('subscribe', path).catch(() => {});
        }
      } catch {
        this.currentReconnectDelay = Math.min(
          this.currentReconnectDelay * 2,
          this.maxReconnectInterval,
        );
        if (this.autoReconnect && !this.intentionalClose) {
          this.scheduleReconnect();
        }
      }
    }, this.currentReconnectDelay);
  }

  private notifyConnectionChange(connected: boolean): void {
    for (const h of this.connectionHandlers) {
      try { h(connected); } catch { /* ignore */ }
    }
  }
}
