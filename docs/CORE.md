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
int value = v.as<int>();      // Throws on failure
auto opt = v.tryAs<int>();    // Returns std::optional<int>
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

- loading config into the node tree
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
