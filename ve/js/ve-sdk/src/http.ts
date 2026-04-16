import type {
  HealthResponse,
  NodeResponse,
  NodeSetResponse,
  TreeNode,
  TreeImportResponse,
  VarValue,
  CommandListResponse,
  CommandRunResponse,
} from './types';

export class VeHttpClient {
  private base: string;

  constructor(base = '') {
    this.base = base.replace(/\/$/, '');
  }

  private async request<T>(path: string, init?: RequestInit): Promise<T> {
    const res = await fetch(`${this.base}${path}`, init);
    if (!res.ok) {
      const body = await res.text();
      throw new Error(`HTTP ${res.status}: ${body}`);
    }
    return res.json();
  }

  private async requestText(path: string, init?: RequestInit): Promise<string> {
    const res = await fetch(`${this.base}${path}`, init);
    if (!res.ok) {
      const body = await res.text();
      throw new Error(`HTTP ${res.status}: ${body}`);
    }
    return res.text();
  }

  async health(): Promise<HealthResponse> {
    return this.request('/health');
  }

  /** GET /api/cmd — registered command names and help text */
  async listCommands(): Promise<CommandListResponse> {
    return this.request('/api/cmd');
  }

  /**
   * POST /api/cmd/{cmdKey} — run a ve::command (e.g. save, load).
   * Default wait false → may return 202 { ok, accepted } when execution is async on main loop.
   * Pass wait: true to block until the pipeline finishes (same worker; avoid on HTTP from hot paths if possible).
   */
  async runCommand(
    cmdKey: string,
    body: { args?: VarValue[]; wait?: boolean; id?: VarValue } = {},
  ): Promise<CommandRunResponse> {
    const key = encodeURIComponent(cmdKey.replace(/^\/+/, ''));
    return this.request(`/api/cmd/${key}`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    });
  }

  async getNode(path = ''): Promise<NodeResponse> {
    const p = path ? `/${path}` : '';
    return this.request(`/api/node${p}`);
  }

  async setNode(path: string, value: VarValue): Promise<NodeSetResponse> {
    return this.request(`/api/node/${path}`, {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(value),
    });
  }

  /** POST /api/node/{path} — fire NODE_CHANGED on the node. */
  async triggerNode(path: string): Promise<NodeSetResponse> {
    return this.request(`/api/node/${path}`, {
      method: 'POST',
    });
  }

  async getTree(path = ''): Promise<TreeNode> {
    const p = path ? `/${path}` : '';
    return this.request(`/api/tree${p}`);
  }

  async getChildren(path = ''): Promise<string[]> {
    const p = path ? `/${path}` : '';
    return this.request(`/api/children${p}`);
  }

  /** Export subtree as raw JSON string (GET /api/tree) */
  async exportTree(path = ''): Promise<string> {
    const p = path ? `/${path}` : '';
    return this.requestText(`/api/tree${p}`);
  }

  /** Import JSON into subtree (POST /api/tree) */
  async importTree(path: string, json: string): Promise<TreeImportResponse> {
    const p = path ? `/${path}` : '';
    return this.request(`/api/tree${p}`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: json,
    });
  }
}
