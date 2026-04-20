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

export interface VeOkReply<T = VarValue> {
  ok: true;
  id?: VarValue;
  data: T;
  accepted?: false;
}

export interface VeAcceptedReply {
  ok: true;
  id?: VarValue;
  accepted: true;
  task_id: string;
}

export interface VeErrorReply {
  ok: false;
  id?: VarValue;
  code: string;
  error: string;
}

export type VeReply<T = VarValue> = VeOkReply<T> | VeAcceptedReply | VeErrorReply;

export interface NodeMeta {
  type: number;
  child_count: number;
  has_shadow: boolean;
  subscribers: number;
  parent_path?: string;
}

export interface NodeResponse {
  path: string;
  value: VarValue;
  tree?: VarValue;
  meta?: NodeMeta;
}

export interface NodeSetResponse {
  path: string;
}

export interface NodeListEntry {
  name: string;
  path: string;
  has_value: boolean;
  child_count: number;
  type?: number;
  subscribers?: number;
}

export interface NodeListResponse {
  path: string;
  children: NodeListEntry[];
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

export type CommandRunOkResponse = VeOkReply<VarValue>;
export type CommandRunAcceptedResponse = VeAcceptedReply;
export type CommandRunErrorResponse = VeErrorReply;
export type CommandRunResponse = VeReply<VarValue>;

export interface NodeChangedEvent {
  event: 'node.changed';
  path: string;
  value: VarValue;
}

export interface TaskResultEvent {
  event: 'task.result';
  id?: VarValue;
  task_id: string;
  ok: boolean;
  data?: VarValue;
  code?: string;
  error?: string;
}

export type WsMessage = NodeChangedEvent | TaskResultEvent | VeReply<VarValue>;

export interface VeRequest {
  op: string;
  id?: VarValue;
  path?: string;
  value?: VarValue;
  tree?: VarValue;
  args?: VarValue;
  wait?: boolean;
  depth?: number;
  meta?: boolean;
  bubble?: boolean;
  items?: VeRequest[];
  name?: string;
}

export interface VeSdkConfig {
  httpBase?: string;
  wsUrl?: string;
}
