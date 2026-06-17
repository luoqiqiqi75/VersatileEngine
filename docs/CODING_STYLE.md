# VersatileEngine Coding Style

## Purpose

This document defines the stable coding rules for new VE work.
It exists to keep future code aligned with the current architecture rather than with older migration-era experiments.

## General Rules

- Prefer clear behavior over clever abstraction.
- Keep the `ve/` core pure C++17.
- Put framework-specific code in adapters, not in core headers.
- Keep public APIs small and explicit.
- Avoid temporary wrappers that only exist to smooth a migration step.

## Layer Boundaries

### Core

Core code may depend on:

- the C++ standard library
- bundled low-level libraries already adopted by the core

Core code may not depend on:

- Qt types
- ROS-specific types
- UI framework classes

### Service

Service code may depend on transport libraries.
It should not contain business logic.

### Adapter

Adapter code may depend on foreign frameworks.
It should translate between those frameworks and VE semantics.

## API Design

- Use nouns for stable data structures and verbs for operations.
- Keep tree semantics explicit: name, key, path, child order, overlap index.
- Prefer in-place sync APIs when identity matters, such as `Node::copy`.
- Do not expose undocumented side behavior as part of the contract.
- If a public behavior changes, update tests and public docs in the same change.

## Node and Path Conventions

- Use slash-separated paths in core APIs.
- Reserve `ve/...` for framework-owned runtime state.
- Prefer `config`, `state`, and `value` subtrees for module-owned data.
- Keep anonymous children for list-like structure only.
- Use named children for stable contracts.

## Comments

Comments are part of the public quality bar.

Rules:

- Write comments in English.
- Use ASCII only.
- Explain intent, invariants, or non-obvious behavior.
- Do not narrate trivial mechanics.
- Do not leave phase plans, migration reminders, or stale compatibility notes in stable code.
- If a comment describes protocol support, describe the protocol itself, not the historical reason it exists.

Preferred comment shapes:

- file header with scope
- short section headers
- brief rationale above tricky code

Avoid:

- banner noise without information
- duplicated comments on obvious assignments
- roadmap comments in implementation files

## Naming

- Use `snake_case` for variables and functions in existing C++ core style.
- Use `PascalCase` for types.
- Keep abbreviations rare and domain-specific.
- Favor names that match the node model and runtime behavior.

## Idiomatic VE Patterns

These are the short-form patterns to reach for first. See `docs/QUICK_REFERENCE.md` for worked examples.

### Node path/value access — 2×2 symmetric API

|              | Path (returns `Node*`)              | Value (returns `Var`)                  |
|--------------|--------------------------------------|----------------------------------------|
| **mutating** | `at(path)` — ensure-or-create        | `set(path, v)` — write, auto-create    |
| **read-only**| `find(path)` — `nullptr` if absent   | `get(path)` — empty `Var` if absent    |

Choose along two axes: are you reading or writing, and do you want the node or its value. Short names are an ergonomic choice for high-frequency operations; they are project-local and do **not** mirror `std::vector::at` (no bounds-throw) or `std::find` (no search/scan).

### Writing values — let `Var` ctors pick the type

Pass literals directly to `Node::set` and other Var-accepting APIs:

```cpp
node->set(0);             // not Var(static_cast<int64_t>(0))
node->set("hello");       // not Var(std::string{"hello"})
node->set(true);
```

Explicit `Var(...)` wrapping is only needed for raw `void*` pointers (`Var(static_cast<void*>(p))`), forced numeric widening, and custom types (`Var::custom`).

### Name-based dispatch — register through `command`

Whenever code routes by a string key (protocol `op`, RPC `method`, REPL verb, plugin action), use `ve::command::reg(key, fn, help)` for registration and `ve::command::call(key, ctx)` for dispatch. The command registry is hash-backed, introspectable (`command::keys`, `command::help`), and integrates with `Pipeline`. This replaces ad-hoc `if/else` string chains and per-module `std::map<std::string, fn>` tables.

For internal, non-exposed dispatch the same idea applies one level down: store `Var::callable` handlers on a dispatcher `Node` and resolve via `find(op)`. Reserve plain `switch` for fixed type enums (e.g. `Var::Type`) where the value space is closed.

### Building structured payloads — Node as transient aggregator

To assemble a structured blob for serialization (YAML / JSON / Bin / Markdown), prefer a **temporary unowned `Node`** as the build container, then serialize via `schema::fromNode<F>(&node)`. The schema layer is Node-centric — every format implements `Format::fromNode(const Node*)`. A `Node` with no parent and no subscribers carries near-zero reactive overhead, so this is a cheap and idiomatic build-up form. Direct `Var::DictV` / `Var::ListV` manipulation remains appropriate at boundaries where a `Var` is already in hand and one-shot serialization is the only goal.

## Memory and Ownership

- Preserve object identity when syncing existing runtime state.
- Prefer batch operations when the structure already supports them.
- Do not introduce hidden global ownership outside the existing core facilities.
- Be explicit when an operation deletes detached nodes.

## Signals and Runtime Behavior

- Treat emitted signals as part of observable behavior.
- Avoid adding silent behavior changes to structural APIs without tests.
- When batching, prefer fewer structural operations if semantics remain clear.

## Testing

- Every public behavior change needs tests.
- Add tests next to the subsystem that owns the behavior.
- Prefer behavior tests over implementation tests.
- Include edge cases for naming, ordering, and empty structures in node tests.

## Documentation

When a change affects public behavior or architectural guidance, update:

- `README.md` for top-level framing
- `docs/DESIGN.md` for architecture
- `docs/CORE.md` for core-facing behavior
- this file when the rule itself changes

## Repository Hygiene

- Do not keep generated artifacts in the repository unless they are intentionally versioned deliverables.
- Remove stale planning documents once they have been executed and summarized.
- Keep one authoritative document for each public topic.
