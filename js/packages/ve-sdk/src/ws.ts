import type { WsCommand, WsMessage, VarValue } from './types';

export type WsMessageHandler = (msg: WsMessage) => void;
export type WsStateHandler = (connected: boolean) => void;

export interface VeWsClientOptions {
  url?: string;
  reconnectInterval?: number;
  maxReconnectInterval?: number;
}

export class VeWsClient {
  private url: string;
  private ws: WebSocket | null = null;
  private reconnectInterval: number;
  private maxReconnectInterval: number;
  private currentInterval: number;
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  private intentionalClose = false;

  private messageHandlers = new Set<WsMessageHandler>();
  private stateHandlers = new Set<WsStateHandler>();

  constructor(options: VeWsClientOptions = {}) {
    this.url = options.url ?? 'ws://localhost:8081';
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
    this.ws?.close();
    this.ws = null;
  }

  onMessage(handler: WsMessageHandler): () => void {
    this.messageHandlers.add(handler);
    return () => this.messageHandlers.delete(handler);
  }

  onStateChange(handler: WsStateHandler): () => void {
    this.stateHandlers.add(handler);
    return () => this.stateHandlers.delete(handler);
  }

  send(cmd: WsCommand): void {
    if (!this.ws || this.ws.readyState !== WebSocket.OPEN) return;
    this.ws.send(JSON.stringify(cmd));
  }

  get(path = ''): void {
    this.send({ cmd: 'get', path });
  }

  set(path: string, value: VarValue): void {
    this.send({ cmd: 'set', path, value });
  }

  subscribe(path: string): void {
    this.send({ cmd: 'subscribe', path });
  }

  unsubscribe(path: string): void {
    this.send({ cmd: 'unsubscribe', path });
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
    };

    this.ws.onclose = () => {
      this.notifyState(false);
      if (!this.intentionalClose) this.scheduleReconnect();
    };

    this.ws.onerror = () => {
      this.ws?.close();
    };

    this.ws.onmessage = (event) => {
      try {
        const msg: WsMessage = JSON.parse(event.data as string);
        for (const handler of this.messageHandlers) handler(msg);
      } catch { /* ignore malformed messages */ }
    };
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
    for (const handler of this.stateHandlers) handler(connected);
  }
}
