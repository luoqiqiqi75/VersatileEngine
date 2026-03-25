# VersatileEngine History

## Summary

VersatileEngine is the current form of a long-running line of projects that repeatedly explored the same core idea:

**represent system state as a reactive tree, then use that tree to connect modules, tools, and frontends.**

This document is a design history, not a release changelog.
It records which ideas survived and why.

## Timeline at a Glance

| Period | Project | Lasting contribution |
|--------|---------|----------------------|
| 2015-2017 | RobotAssist / IMOL | Original data-tree and module model |
| 2018-2019 | xcore | Pure C++ signal, command, and loop experiments |
| 2020-2021 | hemera | Nested value containers and tree-style lookup in pure C++ |
| 2021-2022 | feelinghand | VE used as a standalone integration layer |
| 2022-2023 | Bezier | High-performance pipeline and graph-style execution ideas |
| 2023 | PDS-HMI | Declarative HMI structure over the data tree |
| 2024 | moz | Web-facing protocol and frontend integration |
| 2025-present | mxhelper / MovaX 2.0 | Multi-protocol integration and adapter maturity |

## 1. RobotAssist and the IMOL Origin

The earliest generation established the architectural DNA that still matters:

- a hierarchical runtime data tree
- path-based access
- module lifecycle management
- signal-driven updates
- a strong emphasis on runtime tooling

The important lesson from this period was not a specific implementation detail.
It was the discovery that a tree can serve as both application state and integration surface.

Modern VE still inherits that idea directly.

## 2. xcore: Can the Core Leave Qt?

xcore was the first serious pure C++ extraction.
It proved that the basic runtime pieces did not fundamentally require Qt:

- object and signal dispatch
- manager-style ownership
- typed data holders
- command execution chains
- loop abstractions

xcore matters because it disproved the idea that VE had to stay Qt-only forever.
It also contributed the strongest early command and loop concepts that later VE work still draws from.

Its limitation was not capability.
It was ergonomics.
Compared with Qt, the amount of manual infrastructure was still high.

## 3. hemera: Tree Ambition in Pure C++

hemera pushed harder on the pure C++ direction by exploring nested data containers and path-driven access.
This period is important because it moved beyond flat typed objects and toward a real tree model.

Durable contributions from hemera:

- nested list and dictionary style values
- pure C++ tree lookup patterns
- stronger boundary thinking around ROS and DDS integration

What did not survive intact:

- the exact template-heavy data model
- the expectation that compile-time type safety alone would beat the convenience of a dynamic runtime value

The key lesson was that tree structure mattered, but developer usability mattered just as much.

## 4. feelinghand: VE as a Standalone Integration Layer

feelinghand is important less for new core abstractions and more for deployment proof.
It showed that VE could operate as a standalone layer rather than only as a hidden part of a larger in-house stack.

This period validated the integration posture that now defines VE:

- VE can sit between UI systems and domain systems
- VE can expose shared state without owning the entire product architecture

## 5. Bezier: Pipeline and Graph Pressure

Bezier was a stress test for execution flow, data dependencies, and performance.
Its pipeline work made one thing clear:

- linear command chains are useful
- graph-shaped execution becomes necessary in complex dataflow systems

Modern VE does not copy Bezier directly, but it inherits the pressure that Bezier exposed:

- runtime models must stay efficient
- orchestration needs to respect dependency structure
- the tree is not enough by itself when execution topology becomes complex

This history is one reason `Command`, `Step`, `Pipeline`, and `Loop` are treated as first-class concerns in VE.

## 6. PDS-HMI: Declarative UI Structure over the Tree

PDS-HMI showed that the tree model works well for application structure, not just transport.

What survived:

- configuration and runtime state under stable subtrees
- page and panel behavior driven from shared state
- practical use of the tree as the single source of truth for a real HMI

This period strengthened the current recommendation to separate module data into `config`, `state`, and `value` subtrees.

## 7. moz: Web Integration Becomes Native

moz pushed VE across the C++ and web boundary.
The important result was not simply that a browser could connect to the backend.
It was that the same state model remained useful once exposed through WebSocket and frontend tooling.

Durable contributions:

- live WebSocket state subscriptions
- browser-facing request and update patterns
- proof that the node tree can serve native and web clients at the same time

This is the historical basis for the current `ve/js/` area and for keeping HTTP and WebSocket close to the core model.

## 8. mxhelper and MovaX 2.0: Adapter Maturity

This period is where the adapter idea became fully concrete.
VE was no longer only a core plus one UI.
It became a practical convergence layer for multiple protocols and frontends.

Durable contributions:

- stronger Qt and QML bridging
- richer service boundaries
- more mature remote tooling expectations
- clearer separation between core semantics and transport-specific code

This is the closest direct ancestor of the current repository shape.

## 9. What Modern VE Keeps

Modern VE keeps the parts that repeatedly proved valuable across generations:

- the shared data tree
- path-driven state access
- reactive signals and subtree bubbling
- a thin but explicit module lifecycle
- runtime debugging surfaces
- adapters rather than hard rewrites
- a pure C++ core with optional ecosystem layers

## 10. What Modern VE Rejects

Modern VE intentionally avoids several patterns that appeared during earlier consolidation work:

- phase-based documentation as the primary public explanation
- public APIs described as temporary migration steps
- parallel competing descriptions of the same architecture
- framework-specific assumptions inside the core layer

The current repository should describe the design as it is now, while this history document explains how it got here.

## 11. Current Position

Today VE is best understood as:

- a pure C++17 runtime core
- a reactive node tree used as shared state and control surface
- a set of services that expose that tree to humans and tools
- a set of adapters that connect Qt, ROS, DDS, RTT, and web clients without forcing a rewrite

That is the shape that survived repeated use in robotics, HMI, medical, and web-facing systems.
