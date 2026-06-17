# VersatileEngine Quick Reference

Quick reference for common operations and recent features.

## Var Quick Operations

```cpp
// Construction
Var v1(42);                           // int
Var v2("hello");                      // string
Var v3(std::vector<int>{1, 2, 3});    // auto-converts to LIST
Var v4(std::map<std::string, int>{{"a", 1}});  // auto-converts to DICT

// Type checking
if (v.isInt()) { ... }
if (v.isList()) { ... }

// Extraction (type-safe, returns default on mismatch)
int i = v.toInt(0);           // default: -1
std::string s = v.toString(); // default: ""
const Var::ListV& list = v.toList();
const Var::DictV& dict = v.toDict();

// In-place conversion (chainable)
v.fromString("hello");        // Convert to STRING
v.fromList({1, 2, 3});        // Convert to LIST
v.fromDict({{"a", 1}});       // Convert to DICT

// Generic conversion
int value = v.as<int>();      // Throws on failure
auto opt = v.tryAs<int>();    // Returns std::optional<int>
```

## Node Operations

### Naming convention (2×2 read/write × path/value)

Node path/value access follows a **two-axis symmetric naming table**. Names are deliberately short — they are typed hundreds of times per day, so two-letter `at` beats six-letter `ensure`.

|              | **Path** (returns `Node*`)              | **Value** (returns `Var`)             |
|--------------|------------------------------------------|----------------------------------------|
| **mutating** | `at(path)` — ensure-or-create the node   | `set(path, v)` — write value, auto-create missing path |
| **read-only**| `find(path)` — `nullptr` if absent       | `get(path)` — empty `Var` if absent     |

Mental model:
- **Writing is creative**: `at` and `set` bring the path into existence if it doesn't yet exist — you said you want to write there, so VE makes the room.
- **Reading is observational**: `find` and `get` never mutate the tree — a missing node has no value to observe, so they simply report "absent".
- `get` is functionally `find` + read-value; the short alias exists because reading values is the most frequent operation.

> Common mis-reads to recheck: `at` is **not** `std::vector::at` (no bounds-throw); `find` is **not** a search/scan (it's an exact path lookup). Both operate on slash-separated paths like `robot/state/power`.

### Writing values: prefer the short form

`Node::set` accepts a `Var` via implicit construction. Var ctors are non-explicit and cover every common literal, so **write the literal directly** — let the compiler pick the right ctor.

```cpp
node->set(0);                  // INT   (Var(int) → int64 internally)
node->set(1.5);                // DOUBLE
node->set(true);               // BOOL
node->set("hello");            // STRING via Var(const char*)
node->set(std::string{"x"});   // STRING via Var(const std::string&)
node->set(Var::ListV{1,2,3});  // LIST — container types need the explicit tag
node->set(myMap);              // DICT — std::map / Dict pickup by Var template ctor
```

Wrap in `Var(...)` explicitly only when:
- **Disambiguating a raw data pointer** — `Var(static_cast<void*>(p))` is required by design (`var.h:64-67` SFINAE-deletes other raw pointers to prevent accidental pointer→bool).
- **Forcing a numeric type narrower than the literal** — e.g. you have an `int` but want it stored as DOUBLE: `node->set(Var(static_cast<double>(n)))`.
- **Custom user types** — `Var::custom(std::move(myObject))` for opaque payloads.

Patterns like `set(ve::Var(static_cast<int64_t>(0)))` are historical residue from an earlier API surface and add no safety today — the short form is equivalent at the byte level.

### Common patterns

```cpp
// Global root accessor (shortcut for the process-wide tree root)
auto* node = ve::n("robot/state/power");
node->set(1);                              // value write + path ensure in one call

// Read-side
Node* found = root->find("config");        // nullptr if missing — pair with explicit null-check
Var value   = root->get("config/level");   // empty Var if missing — safe to chain .toInt(default)

// Write-side
Node* slot  = root->at("config/db/host");  // creates "config", "db", "host" as needed, returns leaf
slot->set(std::string{"localhost"});

// Child / structural
Node* child   = parent->append("name");    // append a named child
bool changed  = node->update(Var(43));     // write-if-different, suppresses no-op signals
target->copy(source);                      // deep-merge subtree
target->clear();                           // drop all children, keep node itself
```

### Node as transient aggregator → schema serialization

When building a structured blob to serialize (YAML / JSON / Bin / Markdown), prefer **a temporary unowned `Node`** as the aggregator and serialize it via the schema layer. The schema system is Node-centric — every format implements `Format::fromNode(const Node*)`.

```cpp
Node payload("payload");
payload.set("kp", Var::ListV{1.0, 2.0, 3.0});
payload.set("mode", std::string{"position"});
payload.at("limits")->set("max", 10.0);

// Pick a format tag — JsonS / BinS / XmlS / VarS / MdS / YamlS (yaml lives in ve::ros)
std::string yaml = schema::fromNode<schema::YamlS>(&payload);
std::string json = schema::fromNode<schema::JsonS>(&payload);

// Convenience wrappers also exist where they read more naturally:
std::string yaml2 = ve::ros::yaml::encode(&payload);  // same path, shorter name
```

A temporary `Node` with **no parent and no subscribers** carries near-zero reactive overhead (mutex uncontended, signals fire into the void) — use it freely as a build-up container. Reserve `Var::DictV` / `Var::ListV` direct manipulation for the boundary case where you already hold a `Var` and just need a one-line serialization.

## File I/O Commands

### save - Export node to file

```bash
# Terminal
save json /config                    # Print to screen
save json /config -f config.json     # Save to ./data/config.json
save bin /data -f data.bin           # Binary format
save var /config                     # Var format (JSON-stringified)

# HTTP
curl -X POST http://localhost:12000/ve \
  -H "Content-Type: application/json" \
  -d '{"op":"command.run","name":"save","args":["json","/config","-f","config.json"],"wait":true}'

# WebSocket (JavaScript)
veService.command("save", {
    format: "json",
    path: "/config",
    file: "config.json"
});
```

### load - Import node from file

```bash
# Terminal
load json /config -f config.json           # From file
load json /config -i '{"key":"value"}'     # Inline data

# HTTP
curl -X POST http://localhost:12000/ve \
  -H "Content-Type: application/json" \
  -d '{"op":"command.run","name":"load","args":["json","/config","-f","config.json"],"wait":true}'

# WebSocket (JavaScript)
veService.command("load", {
    format: "json",
    path: "/config",
    file: "config.json"
});
```

**Supported formats**: `json`, `xml`, `md`, `bin`, `var`, or custom (via schema registry)

**File paths**: Relative to `./data/` by default (configurable via `ve/server/file_io/data_root`)

### Markdown Format (md)

Markdown format provides AI-friendly document storage with hierarchical retrieval.

```bash
# Import documentation
load md /docs/plan -f plan.md

# Export as Markdown
save md /docs/plan -f plan.md

# Search by heading
search "Feature" /docs/plan --key

# Get specific section
curl -X POST http://localhost:12000/ve \
  -d '{"op":"node.get","path":"docs/plan/Section/Subsection","depth":1}'
```

**Mapping**:
- MD heading → Node (name=cleaned title, value=content after heading)
- Original title → `_title` child (only if name was cleaned)
- Heading level → `_level` child (only if level jumped)

**Example**:
```markdown
# Database
Config for database

## MySQL
Production settings

# Title1
### Title3
Deep content
```

Becomes:
```
/Database (value: "Config for database")
  /MySQL (value: "Production settings")
/Title1 (value: null)
  /Title3 (value: "Deep content")
    /_level: 3  # Jumped from 1 to 3
```

## Setup Configuration

### Single JSON file
```bash
./ve.exe config.json
```

### Directory (recursive)
```bash
./ve.exe config_dir/
```

Directory structure maps to node tree:
- `config_dir/robot.json` → `/robot` node
- `config_dir/sensors/camera.json` → `/sensors/camera` node

## Logging Configuration

Default log directory: `./log/` (falls back to platform-specific if creation fails)

```json
{
  "ve": {
    "core": {
      "config": {
        "log": {
          "level": "info",           // debug/info/warning/error
          "app": "myapp",             // App name (default: from argv[0])
          "dir": ""                   // Override (empty = use default)
        }
      }
    }
  }
}
```

## Command System

### Registration

Register commands via `command::reg(key, callable, help)`. The callable is adapted to `Proc = std::function<Result(Node* ctx, Node* in, Node* out)>` automatically:

```cpp
// Full three-parameter form (ctx, in, out)
command::reg("node.get", [](Node* ctx, Node* in, Node* out) -> Result {
    Node* target = root(ctx)->find(in->get("path").toString());
    if (!target) return Result::fail("not found");
    out->at("value")->set(target->get());
    return Result::ok();
}, "read a node value");

// Two-parameter form (in, out) — ctx ignored
command::reg("ros.topic.list", [](Node* in, Node* out) -> Result {
    auto filter = in->get("filter").toString();
    schema::VarS::toNode(out->at("topics"), Var(ve::ros::listTopics(filter)));
    return Result::ok();
}, "list ROS topics");

// Single-parameter form (in only)
command::reg("robot.stop", [](Node* in) -> Result {
    auto group = in->get("group").toString();
    return stopGroup(group);
}, "stop robot group");

// Generic callable — args unpacked from in via schema layer
command::reg("math.add", [](int a, int b) { return a + b; }, "add two numbers");
```

### Dispatch

```cpp
// Create and run a single command
Command cmd = command::create("node.get");
cmd.input<schema::JsonS>(R"({"path":"robot/state"})");  // populate in node
cmd.run();
Result r = cmd.result();          // check outcome
Node* out = cmd.outputNode();     // read output

// Pipeline — chain multiple commands with shared context
Pipeline pipe;
Command* c1 = pipe.add(command::create("node.get"));
c1->setContextNodes(pipe.contextNode(), paramsNode, outputNode);
pipe.sync();
```

### Introspection

```cpp
auto keys = command::factory().keys();        // all registered command names
auto help = command::factory().help("save");  // help text for a command
```

### HTTP endpoints

- **`POST /cmd/<name>`** — single command, body → `in` node (JSON), response = result + output
- **`POST /ve`** — envelope protocol with `op`/`cmd`/`batch` dispatch through `Pipeline`

## Service Ports

| Service | Port | Protocol | Use Case |
|---------|------|----------|----------|
| Terminal REPL (AI/script) | 10100 | Text commands | **Default** for scripts and AI agents (bare output) |
| Terminal REPL (human) | 10000 | Text commands | Human interactive debugging (banner/title/color) |
| HTTP | 12000 | `/at` + `/ve` + `/jsonrpc` | Browser, curl |
| WebSocket | 12100 | JSON envelope | Real-time web apps |
| TCP | 12200 | JSON envelope + newline | Scripts, IoT |
| UDP | 12300 | JSON envelope datagram | Telemetry |
| Binary TCP | 11000 | MessagePack envelope | High-performance IPC |

## Module Registration

```cpp
class MyModule : public Module {
public:
    using Module::Module;  // Inherit constructor
    
private:
    void init() override {
        // Initialize resources
        // Register commands
    }
    
    void ready() override {
        // Start services
    }
    
    void deinit() override {
        // Cleanup
    }
};

VE_REGISTER_MODULE(my.module, MyModule)
// or with priority:
VE_REGISTER_PRIORITY_MODULE(my.module, MyModule, 50, 1)
```

Module workspace: `my.module` → `/my/module` in node tree
