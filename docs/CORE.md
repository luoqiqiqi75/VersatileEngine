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

### `ve::Command`, `ve::Step`, `ve::Pipeline`

The command system is the runtime execution layer.

- `Step` is a unit of work
- `Pipeline` is an execution instance
- `Command` is a named recipe for one or more steps

The terminal, binary IPC service, and other runtime tools should rely on this layer instead of duplicating business logic.

### `ve::schema`

The schema layer is the format-facing import and export surface for node trees.

Use:

- `schema::exportAs<schema::Json>(node)`
- `schema::exportAs<schema::Json>(node, schema::ExportOptions{...})`
- `schema::importAs<schema::Json>(node, text)`
- `schema::importAs<schema::Json>(node, text, schema::ImportOptions{...})`

Important behavior:

- no-options import keeps the direct fast path
- options-based import performs merge-style synchronization
- `ExportOptions::auto_ignore` hides `_`-prefixed internal children
- `ImportOptions` controls `auto_insert`, `auto_remove`, and `auto_replace`

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
snapshot.copy(ve::n("robot"), true, true);
```

Use `auto_remove = true` when the destination should mirror the source.
Use `auto_remove = false` when the destination may carry local extra state.

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
