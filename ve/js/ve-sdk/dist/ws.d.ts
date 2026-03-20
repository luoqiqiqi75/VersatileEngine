import type { WsCommand, WsMessage, VarValue } from './types';
export type WsMessageHandler = (msg: WsMessage) => void;
export type WsStateHandler = (connected: boolean) => void;
export interface VeWsClientOptions {
    url?: string;
    reconnectInterval?: number;
    maxReconnectInterval?: number;
}
export declare class VeWsClient {
    private url;
    private ws;
    private reconnectInterval;
    private maxReconnectInterval;
    private currentInterval;
    private reconnectTimer;
    private intentionalClose;
    private messageHandlers;
    private stateHandlers;
    constructor(options?: VeWsClientOptions);
    get connected(): boolean;
    connect(): void;
    disconnect(): void;
    onMessage(handler: WsMessageHandler): () => void;
    onStateChange(handler: WsStateHandler): () => void;
    send(cmd: WsCommand): void;
    get(path?: string): void;
    set(path: string, value: VarValue): void;
    subscribe(path: string): void;
    unsubscribe(path: string): void;
    private createConnection;
    private scheduleReconnect;
    private notifyState;
}
//# sourceMappingURL=ws.d.ts.map