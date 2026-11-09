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

/** Response of `meta`: structure without the value. */
export interface MetaResponse {
  type: string;   // Var::typeName
  count: number;  // direct children
}

export interface ChildrenOptions {
  offset?: number;  // negative counts from the end
  limit?: number;   // -1 = all remaining
  value?: boolean;  // add value, as get
  meta?: boolean;   // add type/count, as meta
}

/** key always; value with `value`, type/count with `meta`. */
export interface ChildEntry extends Partial<MetaResponse> {
  key: string;
  value?: VarValue;
}

export interface ChildrenResponse {
  children: ChildEntry[] | null;  // null when the range is empty
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
