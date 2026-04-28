import type {
  CommandListResponse,
  NodeChangedEvent,
  NodeListResponse,
  NodeResponse,
  NodeSetResponse,
  TreeImportResponse,
  VarValue,
  VeErrorReply,
  VeReply,
  VeRequest,
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

  async call<T = VarValue>(op: string, payload: Omit<VeRequest, 'op'> = {}): Promise<VeReply<T>> {
    return new Promise((resolve, reject) => {
      if (!this.ws || !this.connected) {
        reject(new Error('Not connected'));
        return;
      }

      const id = ++this.nextId;
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new Error(`Request timeout (${this.timeout}ms)`));
      }, this.timeout);

      this.pending.set(id, {
        resolve: resolve as (value: VeReply<unknown>) => void,
        reject,
        timer,
      });
      this.ws.send(JSON.stringify({ op, id, ...payload }));
    });
  }

  // ===== Tree operations (default behavior) =====
  async get(path = '', depth = -1): Promise<VarValue> {
    const reply = await this.call<NodeResponse>('node.get', { path, depth });
    const data = this.unwrap(reply);
    return (data.tree ?? data.value) as VarValue;
  }

  async set(path: string, tree: VarValue): Promise<TreeImportResponse> {
    return this.unwrap(await this.call<TreeImportResponse>('node.put', { path, tree }));
  }

  // ===== Single value operations =====
  async val(path: string): Promise<VarValue>;
  async val(path: string, value: VarValue): Promise<NodeSetResponse>;
  async val(path: string, value?: VarValue): Promise<VarValue | NodeSetResponse> {
    if (value === undefined) {
      return this.unwrap(await this.call<NodeResponse>('node.get', { path })).value;
    } else {
      return this.unwrap(await this.call<NodeSetResponse>('node.set', { path, value }));
    }
  }

  // ===== Structure operations =====
  async list(path = ''): Promise<NodeListResponse> {
    return this.unwrap(await this.call<NodeListResponse>('node.list', { path }));
  }

  async rm(path: string): Promise<NodeSetResponse> {
    return this.unwrap(await this.call<NodeSetResponse>('node.remove', { path }));
  }

  async trigger(path: string): Promise<NodeSetResponse> {
    return this.unwrap(await this.call<NodeSetResponse>('node.trigger', { path }));
  }

  // ===== Subscription (tree mode by default) =====
  watch(
    path: string,
    handler: WsNotifyHandler = () => {},
    options: { immediate?: boolean; tree?: boolean; bubble?: boolean } = {},
  ): () => void {
    const { immediate = false, tree = true, bubble = false } = options;

    if (!this.subscriptions.has(path)) {
      this.subscriptions.set(path, new Set());
      if (this.connected) {
        this.sendWithoutReply({ op: 'subscribe', path, tree, bubble });
      }
    }
    this.subscriptions.get(path)!.add(handler);

    if (immediate) {
      this.get(path)
        .then((value) => {
          try {
            handler(path, value);
          } catch {
            // ignore callback errors
          }
        })
        .catch(() => {});
    }

    return () => {
      const handlers = this.subscriptions.get(path);
      if (!handlers) {
        return;
      }
      handlers.delete(handler);
      if (handlers.size === 0) {
        this.subscriptions.delete(path);
        if (this.connected) {
          this.sendWithoutReply({ op: 'unsubscribe', path });
        }
      }
    };
  }

  unwatch(path: string): void {
    this.subscriptions.delete(path);
    if (this.connected) {
      this.sendWithoutReply({ op: 'unsubscribe', path });
    }
  }

  // ===== Commands =====
  async run(name: string, args: VarValue = [], wait = true): Promise<VeReply<VarValue>> {
    return this.call<VarValue>('command.run', { name, args, wait });
  }

  async cmds(): Promise<CommandListResponse> {
    return this.unwrap(await this.call<CommandListResponse>('command.list'));
  }

  // ===== Batch operations =====
  async batch(items: Omit<VeRequest, 'id'>[]): Promise<VeReply<VarValue>[]> {
    const reply = await this.call<{ items: VeReply<VarValue>[] }>('batch', { items });
    return this.unwrap(reply).items;
  }

  // ===== Connection management =====
  onConnectionChange(handler: WsStateHandler): () => void {
    this.stateHandlers.add(handler);
    return () => this.stateHandlers.delete(handler);
  }

  onStateChange(handler: WsStateHandler): () => void {
    return this.onConnectionChange(handler);
  }

  onMessage(handler: WsMessageHandler): () => void {
    this.messageHandlers.add(handler);
    return () => this.messageHandlers.delete(handler);
  }

  // ===== Backward compatibility aliases =====
  getTree = this.get;
  remove = this.rm;
  subscribe = this.watch;
  unsubscribe = this.unwatch;
  command = this.run;
  listCommands = this.cmds;

  private unwrap<T>(reply: VeReply<T>): T {
    if (!reply.ok) {
      const err = reply as VeErrorReply;
      throw new Error(`${err.code}: ${err.error}`);
    }
    if ('accepted' in reply && reply.accepted) {
      throw new Error(`Request accepted asynchronously (task_id=${reply.task_id})`);
    }
    return reply.data;
  }

  private sendWithoutReply(payload: Record<string, unknown>): void {
    if (!this.ws || !this.connected) {
      return;
    }
    this.ws.send(JSON.stringify(payload));
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
        this.sendWithoutReply({ op: 'subscribe', path, tree: true });
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
            try {
              handler(evt.path, evt.value);
            } catch {
              // ignore callback errors
            }
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
    if (this.reconnectTimer) {
      return;
    }
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null;
      this.currentInterval = Math.min(this.currentInterval * 2, this.maxReconnectInterval);
      this.createConnection();
    }, this.currentInterval);
  }

  private notifyState(connected: boolean): void {
    for (const handler of this.stateHandlers) {
      try {
        handler(connected);
      } catch {
        // ignore callback errors
      }
    }
  }

  private notifyMessage(message: WsMessage): void {
    for (const handler of this.messageHandlers) {
      try {
        handler(message);
      } catch {
        // ignore callback errors
      }
    }
  }
}
