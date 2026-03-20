/** Var type tags matching C++ ve::Var::Type */
export type VarType =
  | 'null' | 'bool' | 'int' | 'double' | 'string'
  | 'bin' | 'list' | 'dict' | 'pointer' | 'custom';

/** JSON-serialized Var value (what the REST API returns) */
export type VarValue = null | boolean | number | string | VarValue[] | { [key: string]: VarValue };

/** Response from GET /api/node/{path} */
export interface NodeResponse {
  path: string;
  value: VarValue;
}

/** Response from PUT /api/node/{path} */
export interface NodeSetResponse {
  ok: boolean;
  path: string;
}

/** Response from GET /api/tree/{path} — recursive JSON structure */
export interface TreeNode {
  name?: string;
  value?: VarValue;
  children?: TreeNode[];
  [key: string]: unknown;
}

/** Response from GET /health */
export interface HealthResponse {
  status: string;
  uptime_s: number;
}

/** WebSocket outgoing command */
export interface WsCommand {
  cmd: 'get' | 'set' | 'subscribe' | 'unsubscribe';
  path?: string;
  value?: VarValue;
}

/** WebSocket incoming message */
export interface WsMessage {
  type: 'data' | 'ok' | 'subscribed' | 'unsubscribed' | 'event' | 'error';
  path?: string;
  value?: VarValue;
  error?: string;
}

/** Response from POST /api/tree/{path} */
export interface TreeImportResponse {
  ok: boolean;
  path: string;
}

/** SDK configuration */
export interface VeSdkConfig {
  httpBase?: string;
  wsUrl?: string;
}
