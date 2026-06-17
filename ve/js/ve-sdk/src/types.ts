export type VarType =
  | 'null' | 'bool' | 'int' | 'double' | 'string'
  | 'bin' | 'list' | 'dict' | 'pointer' | 'custom';

export type VarValue =
  | null
  | boolean
  | number
  | string
  | Uint8Array
  | VarValue[]
  | { [key: string]: VarValue };

export interface HealthResponse {
  status: string;
  uptime_s: number;
}

// Envelope v2.1 reply: {id?, code, data?, message?}
export interface VeOkReply<T = VarValue> {
  code: number;
  id?: number;
  data: T;
}

export interface VeErrorReply {
  code: number;
  id?: number;
  message?: string;
}

export type VeReply<T = VarValue> = VeOkReply<T> | VeErrorReply;

export interface ChildEntry {
  name: string;
  path: string;
  value?: VarValue;
  child_count: number;
}

export interface ChildrenResponse {
  children: ChildEntry[];
}

export interface ExportResponse {
  tree: VarValue;
}

export interface GetResponse {
  value: VarValue;
}

export interface PathResponse {
  path: string;
}

export interface TreeImportResponse {
  path: string;
}

export type TreeNode = VarValue;

export interface CommandInfo {
  name: string;
  help: string;
}

export interface CommandListResponse {
  commands: CommandInfo[];
}

export type CommandRunResponse = VeReply<VarValue>;

export interface NodeChangedEvent {
  event: 'node.changed';
  path: string;
  data: VarValue;
}

export type WsMessage = NodeChangedEvent | VeReply<VarValue>;

export interface VeSdkConfig {
  httpBase?: string;
  wsUrl?: string;
}
