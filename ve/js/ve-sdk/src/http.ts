import type {
  CommandListResponse,
  CommandRunResponse,
  HealthResponse,
  NodeListResponse,
  NodeResponse,
  NodeSetResponse,
  TreeImportResponse,
  TreeNode,
  VarValue,
  VeErrorReply,
  VeReply,
  VeRequest,
} from './types';

export class VeHttpClient {
  private base: string;

  constructor(base = '') {
    this.base = base.replace(/\/$/, '');
  }

  private atUrl(path = ''): string {
    const normalized = path.replace(/^\/+/, '');
    return normalized ? `/at/${normalized}` : '/at';
  }

  private async requestJson<T>(path: string, init?: RequestInit): Promise<T> {
    const res = await fetch(`${this.base}${path}`, init);
    const body = await res.text();
    if (!res.ok) {
      throw new Error(`HTTP ${res.status} ${path}: ${body}`);
    }
    try {
      return JSON.parse(body) as T;
    } catch {
      throw new Error(`Invalid JSON from ${path}: ${body}`);
    }
  }

  private async requestText(path: string, init?: RequestInit): Promise<string> {
    const res = await fetch(`${this.base}${path}`, init);
    const body = await res.text();
    if (!res.ok) {
      throw new Error(`HTTP ${res.status} ${path}: ${body}`);
    }
    return body;
  }

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

  async call<T = VarValue>(op: string, payload: Omit<VeRequest, 'op'> = {}): Promise<VeReply<T>> {
    return this.requestJson<VeReply<T>>('/ve', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ op, ...payload }),
    });
  }

  async health(): Promise<HealthResponse> {
    return this.requestJson('/health');
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

  // ===== Commands =====
  async run(
    name: string,
    args: VarValue = [],
    wait = true,
  ): Promise<CommandRunResponse> {
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

  // ===== Legacy REST endpoints =====
  async getChildren(path = ''): Promise<string[]> {
    const data = await this.list(path);
    return data.children.map((child) => child.name);
  }

  async exportTree(path = '', depth = -1): Promise<string> {
    if (depth < 0) {
      return this.requestText(this.atUrl(path));
    }
    const tree = await this.get(path, depth);
    return JSON.stringify(tree);
  }

  async importTree(path: string, json: string): Promise<TreeImportResponse> {
    return this.requestJson<TreeImportResponse>(this.atUrl(path), {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: json,
    });
  }

  // ===== Backward compatibility aliases =====
  getNode = async (path = '', options: { meta?: boolean; depth?: number } = {}): Promise<NodeResponse> => {
    return this.unwrap(
      await this.call<NodeResponse>('node.get', {
        path,
        meta: options.meta,
        depth: options.depth,
      }),
    );
  };

  getTree = this.get;
  setNode = this.val;
  putNode = this.set;
  listNodes = this.list;
  removeNode = this.rm;
  triggerNode = this.trigger;
  runCommand = this.run;
  listCommands = this.cmds;
}
