import type {
  ChildrenResponse,
  CommandListResponse,
  CommandRunResponse,
  ExportResponse,
  GetResponse,
  NodeChangedEvent,
  PathResponse,
  VarValue,
  VeErrorReply,
  VeReply,
  WsMessage,
} from './types';

export type WsNotifyHandler = (path: string, value: VarValue) => void;
export type WsStateHandler = (connected: boolean) => void;
export type WsMessageHandler = (message: WsMessage) => void;

export interface VeWsClientOptions {
  url?: string;
  timeout?: number;
  reconnectInterval?: number;
  maxReconnectInterval?: number;
}

interface PendingRequest {
  resolve: (value: VeReply<unknown>) => void;
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
  private messageHandlers = new Set<WsMessageHandler>();

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
    for (const [, pending] of this.pending) {
      clearTimeout(pending.timer);
      pending.reject(new Error('Disconnected'));
    }
    this.pending.clear();
    this.ws?.close();
    this.ws = null;
  }

  // Low-level: send message, add id, return promise for reply
  send<T = VarValue>(message: Record<string, unknown>): Promise<VeReply<T>> {
    return new Promise((resolve, reject) => {
      if (!this.ws || !this.connected) {
        reject(new Error('Not connected'));
        return;
      }

      const id = ++this.nextId;
      message.id = id;

      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new Error(`Request timeout (${this.timeout}ms)`));
      }, this.timeout);

      this.pending.set(id, {
        resolve: resolve as (value: VeReply<unknown>) => void,
        reject,
        timer,
      });
      this.ws.send(JSON.stringify(message));
    });
  }

  // Std operation: {op, id, params}
  call<T = VarValue>(op: string, params: Record<string, unknown> = {}): Promise<VeReply<T>> {
    return this.send<T>({ op, params });
  }

  // ===== Value operations =====

  async get(path: string): Promise<VarValue> {
    const data = this.unwrap(await this.call<GetResponse>('get', { path }));
    return data.value;
  }

  async set(path: string, value: VarValue): Promise<PathResponse> {
    return this.unwrap(await this.call<PathResponse>('set', { path, value }));
  }

  // ===== Tree operations =====

  async export(path = '', depth = -1): Promise<VarValue> {
    const data = this.unwrap(await this.call<ExportResponse>('export', { path, depth }));
    return data.tree;
  }

  async import(path: string, tree: VarValue, flags?: number): Promise<PathResponse> {
    const params: Record<string, unknown> = { path, tree };
    if (flags !== undefined) params.flags = flags;
    return this.unwrap(await this.call<PathResponse>('import', params));
  }

  // ===== Structure operations =====

  async children(path = ''): Promise<ChildrenResponse> {
    return this.unwrap(await this.call<ChildrenResponse>('children', { path }));
  }

  async erase(path: string): Promise<PathResponse> {
    return this.unwrap(await this.call<PathResponse>('erase', { path }));
  }

  async trigger(path: string): Promise<PathResponse> {
    return this.unwrap(await this.call<PathResponse>('trigger', { path }));
  }

  // ===== Subscription =====

  watch(
    path: string,
    handler: WsNotifyHandler = () => {},
    options: { immediate?: boolean } = {},
  ): () => void {
    const { immediate = false } = options;

    if (!this.subscriptions.has(path)) {
      this.subscriptions.set(path, new Set());
      if (this.connected) {
        this.call('watch', { path }).catch(() => {});
      }
    }
    this.subscriptions.get(path)!.add(handler);

    if (immediate) {
      this.get(path)
        .then((value) => {
          try { handler(path, value); } catch { /* ignore */ }
        })
        .catch(() => {});
    }

    return () => {
      const handlers = this.subscriptions.get(path);
      if (!handlers) return;
      handlers.delete(handler);
      if (handlers.size === 0) {
        this.subscriptions.delete(path);
        if (this.connected) {
          this.call('unwatch', { path }).catch(() => {});
        }
      }
    };
  }

  unwatch(path: string): void {
    this.subscriptions.delete(path);
    if (this.connected) {
      this.call('unwatch', { path }).catch(() => {});
    }
  }

  // ===== User commands (cmd field) =====

  async run(name: string, args: Record<string, unknown> = {}): Promise<CommandRunResponse> {
    return this.send<VarValue>({ cmd: name, params: args });
  }

  async commands(): Promise<CommandListResponse> {
    return this.unwrap(await this.call<CommandListResponse>('commands'));
  }

  // ===== Batch =====

  async batch(items: Record<string, unknown>[]): Promise<VarValue> {
    return this.unwrap(await this.send<VarValue>({ batch: items }));
  }

  // ===== Connection management =====

  onConnectionChange(handler: WsStateHandler): () => void {
    this.stateHandlers.add(handler);
    return () => this.stateHandlers.delete(handler);
  }

  onMessage(handler: WsMessageHandler): () => void {
    this.messageHandlers.add(handler);
    return () => this.messageHandlers.delete(handler);
  }

  // ===== Backward compatibility =====
  subscribe = this.watch;
  unsubscribe = this.unwatch;

  private unwrap<T>(reply: VeReply<T>): T {
    if (reply.code < 0) {
      const err = reply as VeErrorReply;
      throw new Error(`${err.code}: ${err.message ?? 'unknown error'}`);
    }
    return (reply as { data: T }).data;
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
      for (const path of this.subscriptions.keys()) {
        this.call('watch', { path }).catch(() => {});
      }
    };

    this.ws.onclose = () => {
      this.notifyState(false);
      for (const [, pending] of this.pending) {
        clearTimeout(pending.timer);
        pending.reject(new Error('Connection closed'));
      }
      this.pending.clear();
      if (!this.intentionalClose) {
        this.scheduleReconnect();
      }
    };

    this.ws.onerror = () => {
      this.ws?.close();
    };

    this.ws.onmessage = (event) => {
      try {
        const msg = JSON.parse(event.data as string) as WsMessage;
        this.handleMessage(msg);
      } catch {
        // ignore malformed messages
      }
    };
  }

  private handleMessage(msg: WsMessage): void {
    if ('event' in msg) {
      if (msg.event === 'node.changed') {
        const evt = msg as NodeChangedEvent;
        const handlers = this.subscriptions.get(evt.path);
        if (handlers) {
          for (const handler of handlers) {
            try { handler(evt.path, evt.value); } catch { /* ignore */ }
          }
        }
      }
      this.notifyMessage(msg);
      return;
    }

    const id = typeof msg.id === 'number' ? msg.id : undefined;
    if (id !== undefined && this.pending.has(id)) {
      const pending = this.pending.get(id)!;
      this.pending.delete(id);
      clearTimeout(pending.timer);
      pending.resolve(msg);
      return;
    }

    this.notifyMessage(msg);
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
    for (const handler of this.stateHandlers) {
      try { handler(connected); } catch { /* ignore */ }
    }
  }

  private notifyMessage(message: WsMessage): void {
    for (const handler of this.messageHandlers) {
      try { handler(message); } catch { /* ignore */ }
    }
  }
}
