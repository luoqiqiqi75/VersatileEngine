# VersatileEngine Design Guide

## Summary

VersatileEngine is a layered runtime built around one shared `ve::Node` tree.
The tree is the system state model, the observation surface, and the control surface.

The core does not try to replace Qt, ROS, DDS, RTT, or web tooling.
It gives those systems one stable place to exchange state.

## Design Principles

### 1. One tree, many clients

Every durable runtime concern should be representable as node state:

- configuration
- module status
- process health
- command inputs and outputs
- adapter-facing state

Different transports and adapters should expose the same tree, not parallel models.

### 2. Glue layer, not business framework

VE is intentionally non-intrusive.
Domain code should keep its own classes, loops, and APIs when that is the best fit.
VE should sit at the boundary:

- publish state into the node tree
- subscribe to state from the node tree
- translate between native framework types and `ve::Var`

If a feature can be implemented as an adapter instead of a core dependency, prefer the adapter.

### 3. Pure C++ core

The `ve/` core is the long-term stable center of the project.
It must remain:

- pure C++17
- independent from Qt
- usable in headless processes
- small enough to embed into other runtimes

Qt, DDS, ROS, RTT, and JS support belong to adapter or service layers.

### 4. Runtime-first observability

Every serious VE process should be debuggable while it is running.
That is why the service layer exists.

Default surfaces:

- Terminal for operator and developer inspection
- HTTP for scripts, curl, and automation
- WebSocket for live UI and external dashboards
- Binary IPC for efficient process-to-process traffic

The service layer is not separate from the model.
It is a projection of the same node tree.

### 5. Dogfood the framework, or do not ship it

If a piece of framework is not used by the code in this repository, it does
not exist. Unused APIs never get validated, and their design rots because no
one has the pressure of being a user.

The rule is simple and non-negotiable:

- **Write it, use it, refine it.** The same PR that adds a framework capability
  is the PR that puts a first real consumer on top of it. No "we'll wire it up
  later." Later never comes, and the design ossifies around a use case no one
  ever tried.
- **When you use a framework API, act as a critical user.** If the shape is
  awkward, if you find yourself parsing around it, if you need a second
  parameter that is not there — the framework is the thing that must change,
  not your workaround. Propose the fix, or open an issue with the pain point.
  Silent workarounds are how "the logic loops to the sky" (逻辑绕上天) starts.
- **Real recurring needs strengthen the framework; one-offs stay in the
  module.** If two consumers want the same helper, promote it. If exactly one
  wants it and it is domain-specific, keep it local. Do not preemptively
  generalize, and do not perpetually specialize.

A framework abstraction with no real caller has no ground truth. Its shape is
whatever felt tidy in isolation, which almost always drifts from what a real
consumer would need. The failure mode is predictable: the API grows to cover
imagined cases, gets rewritten several times chasing an imagined "right"
shape, and when a real consumer finally arrives, integration is a painful
overhaul that reshapes both the abstraction and every consumer bolted on
around it. Layers that go through this cycle carry those scars for a long
time, and downstream code tends not to invest in maintaining them, because
they were never really "theirs".

Every module in this repo — service, terminal, ROS, RTT, adapters — is
expected to be a first-class user of core APIs. If the framework's own authors
will not use it as they build it, no one else will maintain it later.

## Layer Model

### Core

The core owns semantics.

Main types:

- `ve::Var`: compact runtime value container
- `ve::Object`: signal-capable base object
- `ve::Node`: ordered reactive tree node
- `ve::Command`, `ve::Pipeline`: command registration, dispatch, and execution pipeline
- `ve::Module`: runtime module lifecycle
- `ve::Entry`: process setup, module loading, and shutdown
- `ve::Loop`: event-loop and cross-thread dispatch support

Rules:

- core code does not know about Qt classes
- core code does not assume one specific frontend
- core APIs describe behavior, not migration history

### Service

The service layer exposes the node tree to operators, tools, and remote clients.

It should provide:

- transport handling
- request parsing
- subscription fan-out
- command execution entry points

It should not own business logic.
If logic belongs to the product, it should stay in modules or adapters and publish state through nodes.

### Adapters

Adapters map foreign ecosystems into VE.

Examples:

- `qt/` for Qt, QWidget, QML, and IMOL interop
- `ros/` for DDS and ROS integration
- `rtt/` for retained xcore and RTT-oriented pieces
- `ve/js/` for web-facing clients and tools

Adapter code may depend on foreign frameworks.
Core code may not.

### Programs

Programs are concrete entry processes and examples.

They decide:

- which modules are linked or loaded
- which services are enabled
- which config file is loaded
- which main loop is used

## Node Model

### Naming and addressing

`ve::Node` uses three related concepts:

- **name**: stored child name, may be empty
- **key**: `name`, `name#N`, or `#N`
- **path**: slash-separated keys

Examples:

- `robot/state/power`
- `items/item#1`
- `list/#0`

Rules:

- same-name siblings are allowed
- anonymous children are allowed
- child iteration order is significant

### Structural operations

Core structural operations must keep these semantics stable:

- insertion order is preserved
- named overlap access stays deterministic
- anonymous children remain list-like
- `copy()` synchronizes value and subtree into an existing node

`Node::copy` is a sync operation, not a constructor.
It updates the destination node from a source node and can optionally insert missing children and remove unmatched children.

### Signals

The node tree is reactive by default.

Current direct node signals:

- `NODE_CHANGED`
- `NODE_ADDED`
- `NODE_REMOVED`
- `NODE_ACTIVATED`

`NODE_ACTIVATED` is the subtree-level signal.
It is the key reason VE can expose one tree to many tools without hard wiring module-to-module references.

## Command Model

Commands are invoked from three surfaces that must all resolve to the same shape:
REPL, envelope `cmd` (Python / HTTP / MsgPack), and human curl to `/cmd/<name>`.
The rule below keeps a single implementation valid for all three.

### Signature

Every registered command has exactly this signature:

```cpp
Result cmd::foo(Node* ctx, Node* in, Node* out);
```

- `ctx` holds session state (`_session`, and REPL `argv` when applicable). Read
  what you need (usually `Session*`); do not use `ctx` as an alternate parameter
  source.
- `in` is the parameter node. Read **named fields only** (`in->get("path")`,
  `in->get("top")`, etc.). This mirrors the command's `input_schema`.
- `out` is where you write the result payload. Structured, matching
  `output_schema`.

Return `Result::ok()` / `Result::fail(code, msg)` / `Result::accept()`.

### Instruction schema is the contract

Every command ships an `instruction` subtree in a JSON resource (e.g.
`ve/res/service/cmd.json`) with `description`, `usage`, `input_schema`,
`output_schema`. The schema is not documentation — it is executable:

- **`command::bind` maps CLI tokens to `in` fields using the schema.** Property
  name in the schema == CLI flag name (`--<name>` or `--<name> value`) == field
  read on `in`. These three MUST be identical.
- Boolean properties accept bare `--<name>` (bind fills `true`).
- Positional tokens fill schema properties in declaration order, skipping those
  already provided by flags.
- Unknown flags are ignored (schema is the whitelist).

The REPL, `/cmd/<name>`, and network positional (`params.args`) all delegate to
`command::bind` — no command should re-parse tokens itself.

### One parser, three surfaces

The only place a command consults CLI-style tokens is here, at the top of the
command, when `params.args` is present (network positional shape):

```cpp
if (Node* args_n = in->find("args")) {
    Strings tokens;
    for (auto* c : args_n->children()) tokens.push_back(c->getString());
    command::bind(command::factory(), "foo", tokens, in);
}
```

After that, read named fields from `in` uniformly. `search` in
`ve/src/service/cmd_commands.cpp` is the canonical reference.

### If the CLI shape does not fit the schema, fix the schema — not the parser

Domain-alias flags (`--value` meaning `--target value`, `--glob` meaning
`--mode glob`) can not be expressed in `input_schema` and MUST NOT be
smuggled in via a hand-written parser reading `ctx.argv`. That path silently
diverges from network callers, hides the contract from `describe`, and adds an
ad-hoc parser per command.

The right response is one of:

- rename the schema property so the CLI form is natural (`--mode glob` instead
  of `--glob`),
- use standard short flags via schema single-letter properties (`-i` maps to a
  property named `i`),
- accept that the CLI form the docs assumed is non-standard and revise the docs.

Ad-hoc positional / flag parsing inside a command body is a design smell. If
`in.args` is not enough, revisit the schema.

### Output shape

Return a structured object under `out`, not a bare list or scalar:

```cpp
Node* matches = out->at("matches");
for (auto& p : paths) matches->append()->set(p);
out->set("count", static_cast<int64_t>(paths.size()));
```

Clients can then rely on `output_schema` for shape. Bare lists mean every new
consumer invents its own unwrapping heuristic.

## Module and Process Model

Configuration is loaded directly into the node tree.
Modules read the parts they own and publish the parts they expose.

Recommended layout pattern:

- `ve/...` for framework-owned runtime state
- `<module>/config/...` for persistent settings
- `<module>/state/...` for status
- `<module>/value/...` for live outputs
- `<module>/command/...` for command-facing data when needed

Process startup should remain simple:

1. Load config into the tree.
2. Load plugins and create modules.
3. Initialize modules.
4. Enter the main loop.
5. Deinitialize in reverse order.

## What To Avoid

- Do not create parallel hidden state models when a node subtree would do.
- Do not put Qt-only assumptions into the core.
- Do not describe stable APIs in terms of migration phases or temporary compatibility.
- Do not make services own product logic.
- Do not add framework-specific glue directly into unrelated modules when an adapter boundary is available.
- Do not hand-roll positional or flag parsing inside a command body — the
  instruction schema plus `command::bind` is the single parser. See the Command
  Model section.
- Do not add framework code that no in-repo module consumes. See principle 5.

## Related Documents

- [CORE.md](CORE.md)
- [CODING_STYLE.md](CODING_STYLE.md)
- [HISTORY.md](HISTORY.md)
- [local-http-curl-debug.md](local-http-curl-debug.md)
