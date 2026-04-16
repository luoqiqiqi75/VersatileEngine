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

/** WebSocket outgoing command (subset; see veservice.js for full command.run + wait) */
export interface WsCommand {
  cmd: 'get' | 'set' | 'subscribe' | 'unsubscribe' | 'command.run';
  path?: string;
  value?: VarValue;
  id?: number;
  name?: string;
  args?: VarValue[];
  wait?: boolean;
}

/**
 * WebSocket JSON frames from ve::service::NodeWsServer (see node_ws_server.cpp).
 * VeWsClient in ws.ts only forwards raw objects; use veservice.js or a custom handler for Promise/command.run.
 */
export type WsMessage =
  | { type: 'data'; path?: string; value?: VarValue; id?: number }
  | { type: 'ok'; result?: VarValue; id?: number }
  | { type: 'error'; msg?: string; id?: number }
  | { type: 'subscribed' | 'unsubscribed'; path?: string; id?: number }
  | { type: 'event'; path?: string; value?: VarValue }
  | { type: 'accepted'; accepted?: boolean; id?: number }
  | { type: 'result'; ok: boolean; result?: VarValue; msg?: string; id?: number };

/** GET /api/cmd */
export interface CommandListResponse {
  commands: { name: string; help: string }[];
}

/** POST /api/cmd/{key} — sync result */
export interface CommandRunOkResponse {
  ok: true;
  result: VarValue;
  id?: VarValue;
}

/** POST /api/cmd/{key} — async accepted (wait false, command deferred to main loop) */
export interface CommandRunAcceptedResponse {
  ok: true;
  accepted: true;
  id?: VarValue;
}

export interface CommandRunErrorResponse {
  error: string;
}

export type CommandRunResponse = CommandRunOkResponse | CommandRunAcceptedResponse | CommandRunErrorResponse;

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
