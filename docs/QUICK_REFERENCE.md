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

```cpp
// Global accessor
auto* node = ve::n("robot/state/power");
node->set(1);

// Path operations
Node* target = root->find("/config");     // Read-only lookup
Node* target = root->at("/config");       // Create-on-demand
Node* child = parent->append("child");    // Create child

// Value operations
node->set(Var(42));
Var value = node->get();
bool changed = node->update(Var(43));     // Only emits if changed

// Tree operations
target->copy(source);                     // Merge subtree
target->clear();                          // Remove all children
```

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

## Command Implementation

When implementing new commands via `command::reg()`:

```cpp
command::reg("mycommand", [](const Var& args) -> Result {
    // Args is always a List
    if (!args.isList()) {
        return Result(Result::FAIL, Var("Args must be a list"));
    }
    
    // Parse flags
    std::vector<std::string> tokens;
    for (auto& item : args.toList()) {
        tokens.push_back(item.toString());
    }
    auto f = detail::parseFlags(tokens, 0);
    
    // Get positional args
    std::string format = f.pos(0);
    std::string path = f.pos(1);
    
    // Get flags
    std::string file = f.get("file", 'f');
    bool compact = f.has("compact");
    
    // Do work...
    
    return Result(Result::SUCCESS, Var("Done"));
}, "mycommand <format> [path] [-f file]");
```

## Service Ports

| Service | Port | Protocol | Use Case |
|---------|------|----------|----------|
| HTTP | 12000 | `/at` + `/ve` + `/jsonrpc` | Browser, curl |
| WebSocket | 12100 | JSON envelope | Real-time web apps |
| TCP | 12200 | JSON envelope + newline | Scripts, IoT |
| UDP | 12300 | JSON envelope datagram | Telemetry |
| Binary TCP | 11000 | MessagePack envelope | High-performance IPC |
| Terminal REPL | 10000 | Text commands | Interactive debugging |

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
