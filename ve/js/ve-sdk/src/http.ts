import type {
  ChildrenResponse,
  CommandListResponse,
  CommandRunResponse,
  ExportResponse,
  GetResponse,
  HealthResponse,
  PathResponse,
  TreeImportResponse,
  VarValue,
  VeErrorReply,
  VeReply,
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
    if (reply.code < 0) {
      const err = reply as VeErrorReply;
      throw new Error(`${err.code}: ${err.message ?? 'unknown error'}`);
    }
    return (reply as { data: T }).data;
  }

  // Low-level: send message to /ve
  async send<T = VarValue>(message: Record<string, unknown>): Promise<VeReply<T>> {
    return this.requestJson<VeReply<T>>('/ve', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(message),
    });
  }

  // Std operation: {op, params}
  async call<T = VarValue>(op: string, params: Record<string, unknown> = {}): Promise<VeReply<T>> {
    return this.send<T>({ op, params });
  }

  async health(): Promise<HealthResponse> {
    return this.requestJson('/health');
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

  // ===== Legacy REST endpoints (unchanged, use /at/* directly) =====

  async exportRest(path = ''): Promise<string> {
    return this.requestText(this.atUrl(path));
  }

  async importRest(path: string, json: string): Promise<TreeImportResponse> {
    return this.requestJson<TreeImportResponse>(this.atUrl(path), {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: json,
    });
  }
}
