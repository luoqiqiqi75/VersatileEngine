import { Server } from "@modelcontextprotocol/sdk/server/index.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import {
  CallToolRequestSchema,
  ListToolsRequestSchema,
  McpError,
  ErrorCode,
} from "@modelcontextprotocol/sdk/types.js";

type CommandInfo = {
  name: string;
  help?: string;
};

type CommandListResponse = {
  commands?: CommandInfo[];
};

type CommandCallResponse = {
  ok?: boolean;
  result?: unknown;
  error?: unknown;
  id?: number;
};

const VE_HTTP_BASE = (process.env.VE_HTTP_BASE ?? "http://127.0.0.1:12000").replace(/\/$/, "");

const VE_PING_TOOL = "ve_ping";

const builtinPingToolDef = {
  name: VE_PING_TOOL,
  description:
    "Probe VE HTTP at GET /api/cmd (no VE command). Returns ok + command count if up; use to see whether the service is running.",
  inputSchema: {
    type: "object" as const,
    additionalProperties: true,
  },
};

function text(value: unknown): string {
  if (typeof value === "string") {
    return value;
  }
  try {
    const s = JSON.stringify(value, null, 2);
    if (s !== undefined) {
      return s;
    }
  } catch {
    // fall through
  }
  return String(value);
}

function errorText(err: unknown): string {
  if (typeof err === "string") {
    return err;
  }
  if (err instanceof Error) {
    const parts: string[] = [];
    const head = err.message || err.name || "Error";
    parts.push(head);
    const agg = err as Error & { errors?: unknown[] };
    if (Array.isArray(agg.errors) && agg.errors.length > 0) {
      for (const e of agg.errors) {
        parts.push(errorText(e));
      }
    }
    return parts.join("\n");
  }
  return text(err);
}

async function httpJson<T>(path: string, init?: RequestInit): Promise<T> {
  const res = await fetch(`${VE_HTTP_BASE}${path}`, init);
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

async function listCommands(): Promise<CommandInfo[]> {
  const payload = await httpJson<CommandListResponse>("/api/cmd");
  if (!Array.isArray(payload.commands)) {
    return [];
  }
  return payload.commands.filter((c) => !!c?.name);
}

async function pingVe(): Promise<{ ok: boolean; text: string }> {
  const path = "/api/cmd";
  try {
    const res = await fetch(`${VE_HTTP_BASE}${path}`);
    const body = await res.text();
    if (!res.ok) {
      return {
        ok: false,
        text: `down: HTTP ${res.status} ${path} at ${VE_HTTP_BASE} — ${body.slice(0, 400)}`,
      };
    }
    let count = 0;
    try {
      const j = JSON.parse(body) as CommandListResponse;
      if (Array.isArray(j.commands)) {
        count = j.commands.length;
      }
    } catch {
      // ignore parse errors; still reachable
    }
    return {
      ok: true,
      text: `ok: ${VE_HTTP_BASE} reachable, GET ${path} -> HTTP ${res.status}, ${count} commands`,
    };
  } catch (err) {
    return {
      ok: false,
      text: `down: ${VE_HTTP_BASE} — ${errorText(err)}`,
    };
  }
}

function commandBodyForHttp(args: unknown): unknown {
  if (args == null) {
    return [];
  }
  if (Array.isArray(args)) {
    return args;
  }
  if (typeof args === "object") {
    const o = args as Record<string, unknown>;
    if (Array.isArray(o.args)) {
      return o.args;
    }
    if (Array.isArray(o.argv)) {
      return o.argv;
    }
    if (Object.keys(o).length === 0) {
      return [];
    }
  }
  return args;
}

async function callCommand(name: string, args: unknown): Promise<CommandCallResponse> {
  return httpJson<CommandCallResponse>(`/api/cmd/${encodeURIComponent(name)}`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ cmd: name, body: commandBodyForHttp(args) }),
  });
}

async function main(): Promise<void> {
  const server = new Server(
    {
      name: "ve-mcp",
      version: "0.1.0",
    },
    {
      capabilities: {
        tools: {},
      },
    },
  );

  server.setRequestHandler(ListToolsRequestSchema, async () => {
    let commands: CommandInfo[] = [];
    try {
      commands = await listCommands();
    } catch {
      return { tools: [builtinPingToolDef] };
    }

    const remoteTools = commands
      .filter((c) => c.name !== VE_PING_TOOL)
      .map((c) => ({
        name: c.name,
        description: c.help && c.help.trim().length > 0 ? c.help : `VE command: ${c.name}`,
        inputSchema: {
          type: "object",
          additionalProperties: true,
        },
      }));

    return {
      tools: [builtinPingToolDef, ...remoteTools],
    };
  });

  server.setRequestHandler(CallToolRequestSchema, async (req) => {
    const name = req.params.name;
    if (!name || typeof name !== "string") {
      throw new McpError(ErrorCode.InvalidParams, "Tool name is required");
    }

    const args = req.params.arguments ?? {};

    if (name === VE_PING_TOOL) {
      const r = await pingVe();
      if (r.ok) {
        return { content: [{ type: "text", text: r.text }] };
      }
      return {
        isError: true,
        content: [{ type: "text", text: r.text }],
      };
    }

    let payload: CommandCallResponse;
    try {
      payload = await callCommand(name, args);
    } catch (err) {
      return {
        isError: true,
        content: [
          {
            type: "text",
            text: `HTTP call failed (${VE_HTTP_BASE}/api/cmd/${encodeURIComponent(name)}): ${errorText(err)}`,
          },
        ],
      };
    }

    if (payload.ok) {
      return {
        content: [{ type: "text", text: text(payload.result) }],
      };
    }

    return {
      isError: true,
      content: [{ type: "text", text: text(payload.error ?? "unknown error") }],
    };
  });

  const transport = new StdioServerTransport();
  await server.connect(transport);
}

main().catch((err) => {
  process.stderr.write(`[ve-mcp] fatal: ${errorText(err)}\n`);
  process.exit(1);
});
