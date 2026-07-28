# VersatileEngine Core Guide

## Scope

The core layer lives in `ve/`.
It is the portable, framework-independent part of the project.

The core is responsible for:

- data representation
- reactive state trees
- signal delivery
- commands and pipelines
- module lifecycle
- process entry and loop integration

The core is not responsible for Qt widget logic, DDS transport details, or web UI behavior.

## Main Public Types

### `ve::Var`

`ve::Var` is the runtime value container used by nodes, signals, commands, and services.

Primary categories:

- none
- bool
- int (int64)
- double
- string
- binary (Bytes)
- list (Vector<Var>)
- dictionary (Dict<Var>)
- pointer
- callable (std::function<Var(const Var&)>)
- custom (std::any)

Use `Var` at boundaries.
Use native domain types inside your own code, then convert at the edge.

#### Construction & Conversion

**Direct construction**:
```cpp
Var v1(42);                    // int
Var v2(3.14);                  // double
Var v3("hello");               // string
Var v4(Var::ListV{1, 2, 3});   // list
Var v5(Var::DictV{{"a", 1}});  // dict
```

**From containers** (automatic conversion):
```cpp
std::vector<int> vec = {1, 2, 3};
Var v = vec;  // Converts to LIST

std::map<std::string, int> map = {{"a", 1}};
Var v = map;  // Converts to DICT
```

**Quick conversion methods**:
```cpp
// Extract values (type-safe, returns default on mismatch)
int i = v.toInt(0);           // default: -1
double d = v.toDouble(0.0);   // default: 0.0
std::string s = v.toString(); // default: ""
const Var::ListV& list = v.toList();
const Var::DictV& dict = v.toDict();

// In-place conversion (chainable)
v.fromString("hello");        // Convert to STRING
v.fromList({1, 2, 3});        // Convert to LIST
v.fromDict({{"a", 1}});       // Convert to DICT

// Generic conversion (uses Convert<T>)
int value = v.as<int>();      // returns T{} on type mismatch (does not throw)
int safe  = v.to<int>(-1);    // returns the given default on mismatch
```

**Type checking**:
```cpp
if (v.isInt()) { ... }
if (v.isList()) { ... }
if (v.isDict()) { ... }
```

**Custom types**:
```cpp
struct MyData { int x; };
Var v = Var::custom(MyData{42});
if (auto* p = v.customPtr<MyData>()) {
    // Use p->x
}
```

**Callable**:

`Var` can hold a function (`CALLABLE`, stored as `std::function<Var(const Var&)>`).
`Var::callable` adapts any compatible callable; `invoke` calls it.

```cpp
Var fn = Var::callable([](int a, int b) { return a + b; });  // args unpacked from input
Var sum = fn.invoke(2, 3);   // -> Var(5); multiple args are packed as a List
if (fn.isCallable()) { ... }
```

Callables back the command registry and internal name-based dispatch — store a
`Var::callable` handler on a `Node` and resolve it via `find(key)`.

### `ve::Object`

`ve::Object` is the signal-capable base class.
Use it when you need:

- thread-safe signal connections
- lightweight lifecycle ownership
- observer registration

Use `Object` for runtime actors.
Use `Node` for shared state.

### `ve::Node`

`ve::Node` is the central runtime data structure.

Key capabilities:

- ordered children
- named and anonymous child access
- value storage
- subtree bubbling
- path lookup
- subtree synchronization with `copy()`

Typical use:

```cpp
auto* power = ve::n("robot/state/power");
power->set(1);

auto* state = ve::n("robot/state");
state->watch(true);
```

Useful operations:

- `find(path)` for read-only lookup
- `at(path)` for create-on-demand lookup
- `append(name)` for child creation
- `copy(other, auto_insert, auto_remove)` for subtree sync
- `clear(auto_delete)` for structural reset

### `ve::Command`, `ve::Pipeline`

The command system is the runtime execution layer.

- `Command` is a named callable registered in a `Factory`, created via `command::create(key)` and executed via `cmd.run()`. Its `Proc` signature is `Result(Node* ctx, Node* in, Node* out)`.
- `Pipeline` chains one or more `Command` instances into an execution graph with shared context, input, and output nodes. Used by the envelope protocol (`/ve`, WebSocket, BinTCP) for batch and multi-step dispatch.

Registration uses `command::reg(key, callable, help)`, where callable can be any of:
- `Result(Node* ctx, Node* in, Node* out)` — full three-parameter form
- `Result(Node* in, Node* out)` — input/output only (ctx ignored)
- `Result(Node* in)` — input only
- `Result()` — no parameters
- Any generic callable — arguments are unpacked from `in` via the schema layer

The terminal, binary IPC service, and other runtime tools should rely on this layer instead of duplicating business logic.

### `ve::schema`

The schema layer is the format-facing import and export surface for node trees.

Use:

- `schema::fromNode<schema::JsonS>(node)`
- `schema::fromNode<schema::JsonS>(node, schema::JsonS::ExportOptions{...})`
- `schema::toNode<schema::JsonS>(node, text)`
- `schema::toNode<schema::JsonS>(node, text, copy_flags)`

Important behavior:

- no-flags import keeps the direct fast path
- flags-based import performs merge-style synchronization through `Node::copy()`
- export options are per format (nested in each `SchemaTraits` specialization);
  `auto_ignore` (JsonS/BinS/VarS) hides `_`-prefixed internal children and
  defaults to true — pass `auto_ignore = false` to export the full tree
- `copy_flags` are `Node::CopyFlag` bits: `COPY_INSERT`, `COPY_REMOVE`,
  `COPY_UPDATE`, `COPY_REPLACE`, plus the presets `COPY_DEFAULT`
  (insert + replace) and `COPY_STRICT` (default + remove)

### `ve::Module`

`ve::Module` is the lifecycle unit for application features.

Use modules for:

- subsystem startup and shutdown
- config loading from known subtrees
- service registration
- publication of runtime state

Modules should be small, explicit, and tree-oriented.

### `ve::entry`

`ve::entry` is the process bootstrap API.

It is responsible for:

- loading the startup config into the node tree
- loading plugins
- creating and ordering modules
- starting the main loop
- shutting down in reverse order

Minimal form:

```cpp
#include <ve/entry.h>

int main(int argc, char* argv[]) {
    return ve::entry::exec(argc, argv);
}
```

Or step by step, when the application needs to act between phases:

```cpp
if (!ve::entry::setup(argc, argv)) return 2;   // config file + CLI
// ... read /ve/entry, write nodes this app owns ...
ve::entry::init();                             // plugins + modules + READY
int code = ve::entry::run();
ve::entry::deinit();
```

#### Startup config file

`setup()` loads **one** file, `ve.json` in the working directory by default.
Override it with `-c <path>` or a bare `*.json` positional. A missing file is
not an error: every setting has a built-in default. The file name carries no
meaning — `leo.json` and `ve.json` behave identically.

```json
{
  "app":       "leo",
  "log":       { "level": "info", "dir": "" },
  "version":   2,
  "blacklist": [ "ve.service.x" ],
  "plugins":   [ { "path": "veqt.dll", "enabled": true, "min_api": 0 } ],

  "modules": {
    "ve":  { "server": { "node": { "http": { "config": { "port": 12000 } } } } },
    "leo": { "robot":  { "controller": { "backend": "external" } } }
  }
}
```

The reserved top-level keys are the only ones VE interprets:

| Key | Meaning |
| --- | --- |
| `app` | Log app name |
| `log` | `level` (`d`/`i`/`w`/`e`) and `dir`; applied during `setup()` |
| `version` | Minimum VE version this config needs; `setup()` fails if it exceeds `VE_ENTRY_VERSION` |
| `blacklist` | Module keys to skip. `"a.b"` also skips `a.b.*` |
| `plugins` | Loaded in order at the top of `init()` |
| `modules` | Per-module config, keyed by node path |

Everything a module reads lives under `modules`, keyed by node path:
`modules/ve/server` is copied to `/ve/server`. Module keys use `.`
(`ve.server`), node paths use `/` (`ve/server`); they name the same thing.
Subtrees matching no loaded module are dropped. Because module config is nested
under `modules`, a module can be named anything without colliding with a
reserved key.

#### CLI flags

Recognized only by `setup(int, char**)`. Each writes straight into the node
tree, so there is no second representation to keep in sync.

| Flag | Effect |
| --- | --- |
| `-c <path>` / `--config <path>` | Startup config file |
| `<path>.json` | Same, as a bare positional (first wins) |
| `<path>.dll` / `.so` / `.dylib` | Appended to `plugins` |
| `-v` / `--verbose` | `verbose = true` |
| `-t` / `--terminal` | `modules/ve/client/terminal/stdio/enabled` |
| `-r [host:port]` / `--remote` | `modules/ve/client/terminal/tcp/*` |

`-t` and `-r` are mutually exclusive. Precedence is built-in defaults, then the
config file, then the CLI.

**Unrecognized flags are ignored, not rejected.** They stay in `/ve/entry/argv`
(also reachable via `entry::args()`) for the application to parse. An app with
its own switches reads them there and writes the nodes it owns before calling
`init()` — so adding a flag to VE can never break a downstream launcher, and an
app never has to pre-register anything with VE.

There is deliberately no generic "override any path" flag. Settings belong in
the config file, where they are reviewable and diffable.

#### `/ve/entry` is staging only

`setup()` builds `/ve/entry`; `init()` copies `modules/*` onto the real module
subtrees and then **erases `/ve/entry`** once `READY` is reached. Anything you
need from it must be read before `READY`, or through `entry::verbose()` /
`entry::args()`.

## Core Usage Patterns

### Publish state

Use stable subtrees instead of loose globals.

```cpp
ve::n("sensor/state/online")->set(true);
ve::n("sensor/value/temperature")->set(23.5);
```

### Observe state

Use node signals when the tree itself is the contract.

```cpp
auto* sensor = ve::n("sensor");
sensor->watch(true);
sensor->connect<ve::Node::NODE_ACTIVATED>(sensor, [](int64_t signal, void* source) {
    (void)signal;
    (void)source;
});
```

### Synchronize subtrees

Use `copy()` when one tree should drive another.

```cpp
ve::Node snapshot;
snapshot.copy(ve::n("robot"), ve::Node::COPY_STRICT);
```

Use `COPY_STRICT` when the destination should mirror the source.
Use `COPY_DEFAULT` when the destination may carry local extra state.
Drop `COPY_REPLACE` to only fill in destination values that are still null.

## Service Defaults

The default VE runtime services are designed around the core tree:

- Terminal: inspect and mutate nodes interactively
- HTTP: scriptable access to node state
- WebSocket: live subscriptions for tools and UIs
- Binary TCP: efficient machine-facing IPC

Those services should remain thin layers over `Node`, `Var`, and `Command`.

## Core Boundaries

When writing core code:

- prefer clear semantics over adapter convenience
- avoid product-specific naming
- avoid transitional compatibility comments in stable headers
- add tests for behavior changes
- document new public behavior in `README.md`, `DESIGN.md`, and this guide when needed

## Related Documents

- [DESIGN.md](DESIGN.md)
- [CODING_STYLE.md](CODING_STYLE.md)
- [HISTORY.md](HISTORY.md)
