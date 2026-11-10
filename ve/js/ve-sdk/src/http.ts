import type {
  ChildrenOptions,
  ChildrenResponse,
  MetaResponse,
  CommandListResponse,
  CommandRunResponse,
  CommandDescription,
  ExportResponse,
  GetResponse,
  HealthResponse,
  PathResponse,
  VarValue,
  VeErrorReply,
  VeReply,
} from './types';

export interface VeHttpRequestOptions {
  signal?: AbortSignal;
  timeout?: number;
  maxResponseBytes?: number;
}

export interface VeHttpClientOptions {
  timeout?: number;
  maxResponseBytes?: number;
  fetchImpl?: typeof fetch;
}

export class VeResponseSizeError extends Error {
  readonly limit: number;
  readonly received: number;

  constructor(limit: number, received: number) {
    super(`VE response exceeds ${limit} bytes (received ${received})`);
    this.name = 'VeResponseSizeError';
    this.limit = limit;
    this.received = received;
  }
}

export class VeHttpClient {
  private base: string;
  private options: VeHttpClientOptions;

  constructor(base = '', options: VeHttpClientOptions = {}) {
    this.base = base.replace(/\/$/, '');
    this.options = { ...options };
    this.requestLimits(options);
  }

  private requestLimits(options: VeHttpRequestOptions) {
    const timeout = options.timeout ?? this.options.timeout ?? 0;
    const maxResponseBytes = options.maxResponseBytes ?? this.options.maxResponseBytes ?? 0;
    if (!Number.isFinite(timeout) || timeout < 0)
      throw new RangeError('timeout must be a non-negative finite number');
    if (!Number.isSafeInteger(maxResponseBytes) || maxResponseBytes < 0)
      throw new RangeError('maxResponseBytes must be a non-negative safe integer');
    return { timeout, maxResponseBytes };
  }

  private atUrl(path = ''): string {
    const normalized = path.replace(/^\/+/, '');
    return normalized ? `/at/${normalized}` : '/at';
  }

  private async requestJson<T>(
    path: string,
    init?: RequestInit,
    options: VeHttpRequestOptions = {},
  ): Promise<T> {
    const body = await this.requestText(path, init, options);
    try {
      return JSON.parse(body) as T;
    } catch {
      throw new Error(`Invalid JSON from ${path}: ${body}`);
    }
  }

  private async requestText(
    path: string,
    init?: RequestInit,
    options: VeHttpRequestOptions = {},
  ): Promise<string> {
    const { timeout, maxResponseBytes } = this.requestLimits(options);
    const { signal } = options;
    const controller = new AbortController();
    let reason: unknown;
    const cancel = (value: unknown) => {
      reason = value;
      controller.abort(value);
    };
    const checkAbort = () => {
      if (controller.signal.aborted) throw reason;
    };
    const abort = () =>
      cancel(signal?.reason ?? new DOMException('VE request aborted', 'AbortError'));
    if (signal?.aborted) abort();
    else signal?.addEventListener('abort', abort, { once: true });
    const timer =
      timeout > 0
        ? setTimeout(
            () =>
              cancel(new DOMException(`VE request timed out after ${timeout} ms`, 'TimeoutError')),
            timeout,
          )
        : undefined;
    let reader: ReadableStreamDefaultReader<Uint8Array> | undefined;
    try {
      checkAbort();
      const res = await (this.options.fetchImpl ?? fetch)(`${this.base}${path}`, {
        ...init,
        signal: controller.signal,
      });
      let body: string;
      if (maxResponseBytes === 0) body = await res.text();
      else {
        reader = res.body?.getReader();
        const chunks: string[] = [];
        const decoder = new TextDecoder();
        let received = 0;
        while (reader) {
          checkAbort();
          const { value, done } = await reader.read();
          if (done) break;
          received += value.byteLength;
          if (received > maxResponseBytes) {
            await reader.cancel();
            throw new VeResponseSizeError(maxResponseBytes, received);
          }
          chunks.push(decoder.decode(value, { stream: true }));
        }
        body = chunks.join('') + decoder.decode();
      }
      checkAbort();
      if (!res.ok) throw new Error(`HTTP ${res.status} ${path}: ${body}`);
      return body;
    } catch (error) {
      if (controller.signal.aborted) throw reason;
      throw error;
    } finally {
      if (timer !== undefined) clearTimeout(timer);
      signal?.removeEventListener('abort', abort);
      reader?.releaseLock();
    }
  }

  private unwrap<T>(reply: VeReply<T>): T {
    if (reply.code < 0) {
      const err = reply as VeErrorReply;
      throw new Error(`${err.code}: ${err.message ?? 'unknown error'}`);
    }
    return (reply as { data: T }).data;
  }

  // Low-level: send message to /ve
  async send<T = VarValue>(
    message: Record<string, unknown>,
    options?: VeHttpRequestOptions,
  ): Promise<VeReply<T>> {
    return this.requestJson<VeReply<T>>(
      '/ve',
      {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(message),
      },
      options,
    );
  }

  // Keep wire JSON intact when consumers need exact int64 or decimal tokens.
  async sendRaw(message: Record<string, unknown>, options?: VeHttpRequestOptions): Promise<string> {
    return this.requestText(
      '/ve',
      {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(message),
      },
      options,
    );
  }

  // Std operation: {op, params}
  async call<T = VarValue>(
    op: string,
    params: Record<string, unknown> = {},
    options?: VeHttpRequestOptions,
  ): Promise<VeReply<T>> {
    return this.send<T>({ op, params }, options);
  }

  async health(options?: VeHttpRequestOptions): Promise<HealthResponse> {
    return this.requestJson('/health', undefined, options);
  }

  // ===== Value operations =====

  async get(path: string, options?: VeHttpRequestOptions): Promise<VarValue> {
    const data = this.unwrap(await this.call<GetResponse>('get', { path }, options));
    return data.value;
  }

  async set(path: string, value: VarValue, options?: VeHttpRequestOptions): Promise<PathResponse> {
    return this.unwrap(await this.call<PathResponse>('set', { path, value }, options));
  }

  // ===== Tree operations =====

  async export(path = '', depth = -1, options?: VeHttpRequestOptions): Promise<VarValue> {
    const data = this.unwrap(await this.call<ExportResponse>('export', { path, depth }, options));
    return data.tree;
  }

  async import(
    path: string,
    tree: VarValue,
    flags?: number,
    options?: VeHttpRequestOptions,
  ): Promise<PathResponse> {
    const params: Record<string, unknown> = { path, tree };
    if (flags !== undefined) params.flags = flags;
    return this.unwrap(await this.call<PathResponse>('import', params, options));
  }

  // ===== Structure operations =====

  async meta(path = '', options?: VeHttpRequestOptions): Promise<MetaResponse> {
    return this.unwrap(await this.call<MetaResponse>('meta', { path }, options));
  }

  async children(
    path = '',
    opts: ChildrenOptions = {},
    options?: VeHttpRequestOptions,
  ): Promise<ChildrenResponse> {
    return this.unwrap(await this.call<ChildrenResponse>('children', { ...opts, path }, options));
  }

  async erase(path: string, options?: VeHttpRequestOptions): Promise<PathResponse> {
    return this.unwrap(await this.call<PathResponse>('erase', { path }, options));
  }

  async trigger(path: string, options?: VeHttpRequestOptions): Promise<PathResponse> {
    return this.unwrap(await this.call<PathResponse>('trigger', { path }, options));
  }

  // ===== User commands (cmd field) =====

  async run(
    name: string,
    args: Record<string, unknown> = {},
    options?: VeHttpRequestOptions,
  ): Promise<CommandRunResponse> {
    return this.send<VarValue>({ cmd: name, params: args }, options);
  }

  async commands(options?: VeHttpRequestOptions): Promise<CommandListResponse> {
    return this.unwrap(await this.call<CommandListResponse>('commands', {}, options));
  }

  async describe(name: string, options?: VeHttpRequestOptions): Promise<CommandDescription> {
    return this.unwrap(await this.call<CommandDescription>('describe', { name }, options));
  }

  // ===== Batch =====

  async batch(items: Record<string, unknown>[], options?: VeHttpRequestOptions): Promise<VarValue> {
    return this.unwrap(await this.send<VarValue>({ batch: items }, options));
  }

  // ===== Schema JSON convenience endpoints =====

  async exportRest(path = '', options?: VeHttpRequestOptions): Promise<string> {
    return this.requestText(this.atUrl(path), undefined, options);
  }

  // Merge into an existing node. PUT /at/* returns an empty body on success.
  async importRest(path: string, json: string, options?: VeHttpRequestOptions): Promise<void> {
    await this.requestText(
      this.atUrl(path),
      {
        method: 'PUT',
        headers: { 'Content-Type': 'application/json' },
        body: json,
      },
      options,
    );
  }
}
