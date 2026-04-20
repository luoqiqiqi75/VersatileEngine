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

type VeOkReply<T> = {
  ok: true;
  id?: unknown;
  data: T;
};

type VeAcceptedReply = {
  ok: true;
  id?: unknown;
  accepted: true;
  task_id: string;
};

type VeErrorReply = {
  ok: false;
  id?: unknown;
  code: string;
  error: string;
};

type VeReply<T> = VeOkReply<T> | VeAcceptedReply | VeErrorReply;

type CommandListResponse = {
  commands?: CommandInfo[];
};

const VE_HTTP_BASE = (process.env.VE_HTTP_BASE ?? "http://127.0.0.1:12000").replace(/\/$/, "");

const VE_PING_TOOL = "ve_ping";

const builtinPingToolDef = {
  name: VE_PING_TOOL,
  description:
    "Probe VE HTTP health and command listing. Returns whether the VE service is reachable and how many commands are exposed.",
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
  const payload = await httpJson<VeReply<CommandListResponse>>("/ve", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ op: "command.list" }),
  });
  if (!payload.ok || !("data" in payload) || !Array.isArray(payload.data.commands)) {
    return [];
  }
  return payload.data.commands.filter((c) => !!c?.name);
}

async function pingVe(): Promise<{ ok: boolean; text: string }> {
  try {
    const healthRes = await fetch(`${VE_HTTP_BASE}/health`);
    const healthBody = await healthRes.text();
    if (!healthRes.ok) {
      return {
        ok: false,
        text: `down: HTTP ${healthRes.status} /health at ${VE_HTTP_BASE} — ${healthBody.slice(0, 400)}`,
      };
    }
    let count = 0;
    try {
      const tools = await listCommands();
      count = tools.length;
    } catch {
      // ignore follow-up command listing errors
    }
    return {
      ok: true,
      text: `ok: ${VE_HTTP_BASE} reachable, /health up, ${count} commands`,
    };
  } catch (err) {
    return {
      ok: false,
      text: `down: ${VE_HTTP_BASE} — ${errorText(err)}`,
    };
  }
}

function commandArgsForVe(args: unknown): { args?: unknown; wait?: boolean; id?: unknown } {
  if (args == null) {
    return { args: [] };
  }
  if (Array.isArray(args)) {
    return { args };
  }
  if (typeof args === "object") {
    const obj = args as Record<string, unknown>;
    const out: { args?: unknown; wait?: boolean; id?: unknown } = {};
    if ("wait" in obj && typeof obj.wait === "boolean") {
      out.wait = obj.wait;
    }
    if ("id" in obj) {
      out.id = obj.id;
    }
    if (Array.isArray(obj.args)) {
      out.args = obj.args;
      return out;
    }
    if (Array.isArray(obj.argv)) {
      out.args = obj.argv;
      return out;
    }
    const clone = { ...obj };
    delete clone.wait;
    delete clone.id;
    if (Object.keys(clone).length === 0) {
      out.args = [];
    } else {
      out.args = clone;
    }
    return out;
  }
  return { args };
}

async function callCommand(name: string, args: unknown): Promise<VeReply<unknown>> {
  return httpJson<VeReply<unknown>>("/ve", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({
      op: "command.run",
      name,
      ...commandArgsForVe(args),
    }),
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
      }
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

    let payload: VeReply<unknown>;
    try {
      payload = await callCommand(name, args);
    } catch (err) {
      return {
        isError: true,
        content: [
          {
            type: "text",
            text: `HTTP call failed (${VE_HTTP_BASE}/ve op=command.run name=${name}): ${errorText(err)}`,
          },
        ],
      };
    }

    if (payload.ok) {
      if ("accepted" in payload && payload.accepted) {
        return {
          content: [{ type: "text", text: `accepted: task_id=${payload.task_id}` }],
        };
      }
      if (!("data" in payload)) {
        return {
          isError: true,
          content: [{ type: "text", text: "missing command result payload" }],
        };
      }
      return {
        content: [{ type: "text", text: text(payload.data) }],
      };
    }

    return {
      isError: true,
      content: [{ type: "text", text: text(`${payload.code}: ${payload.error}`) }],
    };
  });

  const transport = new StdioServerTransport();
  await server.connect(transport);
}

main().catch((err) => {
  process.stderr.write(`[ve-mcp] fatal: ${errorText(err)}\n`);
  process.exit(1);
});
