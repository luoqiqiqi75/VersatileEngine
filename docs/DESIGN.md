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

## Layer Model

### Core

The core owns semantics.

Main types:

- `ve::Var`: compact runtime value container
- `ve::Object`: signal-capable base object
- `ve::Node`: ordered reactive tree node
- `ve::Command`, `ve::Step`, `ve::Pipeline`: command and execution model
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

## Related Documents

- [CORE.md](CORE.md)
- [CODING_STYLE.md](CODING_STYLE.md)
- [HISTORY.md](HISTORY.md)
- [local-http-curl-debug.md](local-http-curl-debug.md)
