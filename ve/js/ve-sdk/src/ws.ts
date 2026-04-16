/**
 * VeWsClient — WebSocket client for VE NodeWsServer.
 *
 * Full-featured client with:
 * - Request/response correlation (Promise-based get/set/command)
 * - Subscribe with per-path callback routing
 * - Trigger (re-fire NODE_CHANGED)
 * - Auto-reconnect with re-subscribe
 *
 * Protocol: JSON commands over WebSocket (default port 12100).
 *
 * @example
 * ```ts
 * import { VeWsClient } from '@ve/sdk';
 *
 * const client = new VeWsClient({ url: 'ws://127.0.0.1:12100' });
 * client.connect();
 *
 * const value = await client.get('movax/robot/available');
 * await client.set('test/hello', 42);
 * await client.trigger('test/hello');
 *
 * const unsub = client.subscribe('movax/robot/global/state/power', (path, value) => {
 *   console.log(`${path} changed:`, value);
 * });
 *
 * const result = await client.command('ros/topic/list', []);
 *
 * unsub();
 * client.disconnect();
 * ```
 */

import type { VarValue } from './types';

export type WsNotifyHandler = (path: string, value: VarValue) => void;
export type WsStateHandler = (connected: boolean) => void;

export interface VeWsClientOptions {
  url?: string;
  /** Request timeout in ms (default: 5000) */
  timeout?: number;
  reconnectInterval?: number;
  maxReconnectInterval?: number;
}

interface PendingRequest {
  resolve: (value: VarValue) => void;
  reject: (error: Error) => void;
  timer: ReturnType<typeof setTimeout>;
}

export class VeWsClient {
  private url: string;
  private timeout: number;
  private ws: WebSocket | null = null;
  private reconnectInterval: number;
  private maxReconnectInterval: number;
  private currentInterval: number;
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  private intentionalClose = false;

  private nextId = 0;
  private pending = new Map<number, PendingRequest>();
  private subscriptions = new Map<string, Set<WsNotifyHandler>>();
  private stateHandlers = new Set<WsStateHandler>();

  constructor(options: VeWsClientOptions = {}) {
    this.url = options.url ?? 'ws://localhost:12100';
    this.timeout = options.timeout ?? 5000;
    this.reconnectInterval = options.reconnectInterval ?? 1000;
    this.maxReconnectInterval = options.maxReconnectInterval ?? 30000;
    this.currentInterval = this.reconnectInterval;
  }

  get connected(): boolean {
    return this.ws?.readyState === WebSocket.OPEN;
  }

  // -------------------------------------------------------------------------
  // Connection
  // -------------------------------------------------------------------------

  connect(): void {
    this.intentionalClose = false;
    this.createConnection();
  }

  disconnect(): void {
    this.intentionalClose = true;
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
    // Reject all pending
    for (const [, p] of this.pending) {
      clearTimeout(p.timer);
      p.reject(new Error('Disconnected'));
    }
    this.pending.clear();
    this.ws?.close();
    this.ws = null;
  }

  // -------------------------------------------------------------------------
  // Node operations (Promise-based)
  // -------------------------------------------------------------------------

  get(path = ''): Promise<VarValue> {
    return this.sendRequest({ cmd: 'get', path });
  }

  set(path: string, value: VarValue): Promise<VarValue> {
    return this.sendRequest({ cmd: 'set', path, value });
  }

  async trigger(path: string): Promise<VarValue> {
    return this.sendRequest({ cmd: 'set', path });
  }

  list(path = ''): Promise<VarValue> {
    return this.sendRequest({ cmd: 'list', path });
  }

  /**
   * Run a VE command.
   * @param name - Command name (e.g. 'ros/topic/list')
   * @param args - Arguments array
   * @param wait - Wait for completion (default: true)
   */
  command(name: string, args: VarValue[] = [], wait = true): Promise<VarValue> {
    return this.sendRequest({ cmd: 'command.run', name, args, wait });
  }

  // -------------------------------------------------------------------------
  // Subscribe
  // -------------------------------------------------------------------------

  /**
   * Subscribe to node changes. Returns an unsubscribe function.
   * @param immediateGet - If true, immediately fetch current value and call handler
   */
  subscribe(path: string, handler: WsNotifyHandler, immediateGet = false): () => void {
    if (!this.subscriptions.has(path)) {
      this.subscriptions.set(path, new Set());
      if (this.connected) {
        this.ws!.send(JSON.stringify({ cmd: 'subscribe', path }));
      }
    }

    this.subscriptions.get(path)!.add(handler);

    if (immediateGet) {
      this.get(path).then(value => {
        try { handler(path, value); } catch { /* ignore */ }
      }).catch(() => {});
    }

    return () => {
      const handlers = this.subscriptions.get(path);
      if (!handlers) return;
      handlers.delete(handler);
      if (handlers.size === 0) {
        this.subscriptions.delete(path);
        if (this.connected) {
          this.ws!.send(JSON.stringify({ cmd: 'unsubscribe', path }));
        }
      }
    };
  }

  unsubscribe(path: string): void {
    this.subscriptions.delete(path);
    if (this.connected) {
      this.ws!.send(JSON.stringify({ cmd: 'unsubscribe', path }));
    }
  }

  onConnectionChange(handler: WsStateHandler): () => void {
    this.stateHandlers.add(handler);
    return () => this.stateHandlers.delete(handler);
  }

  // -------------------------------------------------------------------------
  // Internal
  // -------------------------------------------------------------------------

  private sendRequest(cmd: Record<string, unknown>): Promise<VarValue> {
    return new Promise((resolve, reject) => {
      if (!this.ws || !this.connected) {
        return reject(new Error('Not connected'));
      }

      const id = ++this.nextId;
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new Error(`Request timeout (${this.timeout}ms)`));
      }, this.timeout);

      this.pending.set(id, { resolve, reject, timer });
      this.ws.send(JSON.stringify({ ...cmd, id }));
    });
  }

  private createConnection(): void {
    try {
      this.ws = new WebSocket(this.url);
    } catch {
      this.scheduleReconnect();
      return;
    }

    this.ws.onopen = () => {
      this.currentInterval = this.reconnectInterval;
      this.notifyState(true);
      // Re-subscribe all
      for (const path of this.subscriptions.keys()) {
        this.ws!.send(JSON.stringify({ cmd: 'subscribe', path }));
      }
    };

    this.ws.onclose = () => {
      this.notifyState(false);
      // Reject all pending
      for (const [, p] of this.pending) {
        clearTimeout(p.timer);
        p.reject(new Error('Connection closed'));
      }
      this.pending.clear();
      if (!this.intentionalClose) this.scheduleReconnect();
    };

    this.ws.onerror = () => {
      this.ws?.close();
    };

    this.ws.onmessage = (event) => {
      try {
        const msg = JSON.parse(event.data as string);
        this.handleMessage(msg);
      } catch { /* ignore malformed */ }
    };
  }

  private handleMessage(msg: Record<string, unknown>): void {
    const type = msg.type as string;
    const id = msg.id as number | undefined;

    // Event push from subscription
    if (type === 'event') {
      const path = msg.path as string ?? '';
      const value = msg.value as VarValue;
      const handlers = this.subscriptions.get(path);
      if (handlers) {
        for (const h of handlers) {
          try { h(path, value); } catch { /* ignore */ }
        }
      }
      return;
    }

    // Async command: accepted → keep pending, wait for result
    if (type === 'accepted') return;

    // Match to pending request
    if (id !== undefined && this.pending.has(id)) {
      const p = this.pending.get(id)!;
      this.pending.delete(id);
      clearTimeout(p.timer);

      if (type === 'error') {
        p.reject(new Error((msg.msg as string) ?? 'unknown error'));
      } else if (type === 'result') {
        if (msg.ok) {
          p.resolve((msg.result ?? null) as VarValue);
        } else {
          p.reject(new Error((msg.msg as string) ?? 'command failed'));
        }
      } else {
        // 'data', 'ok', 'subscribed', 'unsubscribed', etc.
        p.resolve((msg.value ?? msg.result ?? msg) as VarValue);
      }
    }
  }

  private scheduleReconnect(): void {
    if (this.reconnectTimer) return;
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null;
      this.currentInterval = Math.min(this.currentInterval * 2, this.maxReconnectInterval);
      this.createConnection();
    }, this.currentInterval);
  }

  private notifyState(connected: boolean): void {
    for (const h of this.stateHandlers) {
      try { h(connected); } catch { /* ignore */ }
    }
  }
}
