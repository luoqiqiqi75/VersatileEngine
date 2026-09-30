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
- timers

Use `Object` for runtime actors.
Use `Node` for shared state.

A timer is just a signal the object emits on a schedule. `startTimer()` returns
the signal id; the tick carries a 1-based counter.

```cpp
auto tick = startTimer(100);                        // ms, repeating
connect(tick, this, [](int64_t n) { /* ... */ });
killTimer(tick);
```

The schedule lives on a `ve::Loop` (by default the loop running the call, else
`loop::main()`), so ticks arrive on that loop's thread. Timers die with the
object; `Loop::addTimer()` is the raw primitive underneath and is not meant for
application code.

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

For an `AsioLoop`, `start()` and `exec()` are two mutually exclusive ways to
drive the same loop. `start()` creates a background worker and returns;
`exec()` dispatches on and blocks its calling thread. The default headless
`loop::main()` uses the internal `AsioMainLoop` implementation and is driven by
`entry::run()` through `exec()`, so idle time blocks in Asio's native event
wait and main-loop handlers retain main-thread affinity.
For `AsioMainLoop` and `QtMainLoop`, `quit()` returns from native `exec()` while preserving
dispatch for shutdown. The caller then invokes `deinit()` on the same thread,
outside the native event loop. Services wait for accepted async commands while
processing remaining main-loop work. Modules deinitialize, then destruct, in
reverse order; the restored core main loop stops last. Each loop provider
restores the previous main pointer and deletes its own resources. Built-in core
loops are deleted by `CoreLoops` at static destruction.

For independent loops, `quit()` ends dispatch. A worker created by `start()` is
joined and reaped by `stop()` before it is started or used through `exec()` again.

#### Startup config file

`setup()` loads **one** file, `ve.json` in the working directory by default.
Override it with `-c <path>` or a bare `*.json` positional. A missing file is
not an error: every setting has a built-in default. The file name carries no
meaning — `leo.json` and `ve.json` behave identically.

```json
{
  "app":       "leo",
  "log":       { "level": "info", "dir": "", "async": false },
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
| `log` | Levels, sinks, directory, asynchronous queue and flush settings; applied during `setup()` |
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

#### Thread pool and logging

Keep only the settings you want to change in `ve.json`. The repository's
[ve_full.json](../ve/program/ve_full.json) lists the built-in defaults for log,
core, client and server settings, including connection timeouts, port retry
limits and terminal options. It is a reference file and is never merged into
the startup configuration automatically. Missing settings use code defaults;
copy individual settings from the reference when you need an override.
Building and installing VE does not copy either JSON file next to the
executable. With no `ve.json`, startup uses only code defaults.
For example, disabling an HTTP listener only needs `"http": {"enable": false}`;
there is no need to repeat that listener's port, root or retry defaults.
Empty mount/plugin lists are defaults; see the browser configuration and
service documentation for concrete static mount and plugin examples.

The task pool is configured at compile time. Without a macro, `loop::pool()`
and `AsioPoolLoop` with an omitted/zero thread count use twice
`std::thread::hardware_concurrency()` (2 workers if the CPU count is unknown).
Define the positive integer macro `VE_LOOP_POOL_THREADS` when compiling
libve to override that count. CMake can set it directly:

```bash
cmake -S . -B build -DVE_LOOP_POOL_THREADS=8
# Return to automatic sizing:
cmake -S . -B build -DVE_LOOP_POOL_THREADS=
```

This count is not a JSON setting. An explicit `AsioPoolLoop(name, count)` uses
that count, and user-owned pools continue to be supplied with `loop::setPool()`.
Logging has its own optional queue and worker count:

```json
{
  "log": {
    "level": "info",
    "dir": "./log",
    "async": true,
    "queue_size": 8192,
    "worker_threads": 1,
    "overflow_policy": "block",
    "flush_interval_seconds": 3,
    "flush_level": "error",
    "console": { "enabled": true, "level": "warn" },
    "file": { "enabled": true, "pattern": "%L[%Y/%m/%d %H:%M:%S.%e] %v" }
  }
}
```

| Log setting | Default | Behavior |
| --- | --- | --- |
| `level` | `info` | `debug`, `info`, `warn`/`warning`, `error`, `critical`/`sudo`, `off`/`ignore`; `d/i/w/e/s` also work |
| `dir` | `./log`, with a platform fallback | Applied before creating the log file |
| `async` | `false` | Move sink formatting and I/O to a dedicated bounded queue; message stream formatting stays on the caller |
| `queue_size` | `8192` | Positive number of queued messages |
| `worker_threads` | `1` | Logging workers (1..1000), independent of the task pool; more than one can reorder output |
| `overflow_policy` | `block` | Wait for queue space; `overrun_oldest` overwrites the oldest queued message and can lose logs |
| `flush_interval_seconds` | `3` | Periodic flushing; `0` disables it |
| `flush_level` | `error` | Flush on this severity or higher; `off` disables this trigger |
| `console.enabled`, `file.enabled` | `true` | Enable each output; disabled file output creates no log directory/file |
| `console.level`, `file.level` | Inherit `level` | Independent output thresholds |
| `console.pattern`, `file.pattern` | Existing console/file formats | spdlog format patterns |

`entry::setup()` applies these settings through `log::configure(Node*)`.
This is the only logging configuration API; pass a root Node containing
`app` and `log`, as in the JSON above:

```cpp
ve::Node config;
config.set("log/async", true);
config.set("log/dir", "./log");
bool ok = ve::log::configure(&config);
```

Each call replaces the complete snapshot: missing fields return to the defaults
above, rather than retaining earlier settings. VE reads the Node during the call
and does not retain it. Failed configuration leaves the current loggers intact.
Settings use `Node::get()` and the standard `Var` conversions with defaults.
Invalid numeric ranges or named levels/policies make `configure()` and `setup()` return `false`
with an error on stderr; configuration validation does not throw.
Configure before log producers start or after they stop. VE adds no
configuration mutex or shared-pointer synchronization to each log message;
spdlog still synchronizes shared output sinks and its asynchronous queue.
Console and file output share one dispatch (one enqueue in async mode); each
sink applies its own level and pattern.
VE owns its loggers directly. Its header-only spdlog registry is separate from
application registries, so application-side `spdlog::get()` cannot retrieve
VE's loggers by name. Configure them through JSON or `log::configure()`.

The early stream filter uses spdlog's `should_log()` without a separate VE level
cache. Filtered messages skip stream insertion and string allocation. Expressions
passed to `operator<<` are still evaluated by C++. With asynchronous logging,
`log::flush()` queues a flush request. The internal resource owner's destructor
stops periodic flushing, drains the worker pool and flushes sinks when the
configuration is replaced or the process exits normally. `entry::deinit()`
stops modules and leaves logging available. To release log resources earlier,
apply a configuration with both outputs disabled after producers stop.
Abrupt termination can still lose buffered logs.

For a local file-only comparison, build and run `ve_log_bench`. It reports both
producer time and time through final draining, plus dropped message counts:

```bash
cmake --build build --target ve_log_bench
./build/bin/ve_log_bench
```

Local Release measurements (three runs, median producer time; 256-byte payload,
file output under the temporary directory, console and periodic/severity flush
disabled):

| Mode | 1 producer, 50,000 messages | 4 producers, 200,000 messages |
| --- | --- | --- |
| Filtered | 3.45 ms | 3.58 ms |
| Synchronous | 19.22 ms | 133.15 ms |
| Async `block` | 29.57 ms | 255.20 ms |
| Async `overrun_oldest` | 24.71 ms, 7,621 messages dropped | 81.53 ms, 139,957 messages dropped |

The fast local file sink is cheaper synchronously in this workload. Async
logging moves output I/O off the caller but adds queue and scheduling costs;
`block` can still stall the caller under sustained load. The faster producer
time with `overrun_oldest` under contention comes with substantial data loss.
Keep the synchronous default and enable async for workloads that benefit from
moving slow output I/O to the background; measure with their actual sinks.

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

The remote terminal TCP client reads `host`, `port`, `connect_timeout_ms`, and
`disconnect_timeout_ms` from `modules.ve.client.terminal.tcp.config`. Both
timeouts default to 2000 ms in code and are listed in `ve_full.json`;
non-positive values fall back to that default.

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
