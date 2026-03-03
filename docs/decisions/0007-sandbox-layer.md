# ADR 0007: Sandbox Layer for Isolated Subsystem Prototyping

**Status**: Accepted
**Date**: 2026-03-03

## Context

The `darwin::*` module graph is intentionally acyclic and layered. That makes it an unreliable place to experiment: adding exploratory code risks breaking module boundaries, dragging in unwanted dependencies, or shipping half-baked algorithms into a public API. At the same time, visual feedback is essential for procedural generation work — you need to see the terrain to judge whether the noise parameters are right.

We needed a place to iterate rapidly on a new subsystem (procedural world generation) without touching any of the existing modules.

## Decision

Introduce a `sandboxes/` directory at the repository root. Each subdirectory is a self-contained executable that:

- Links only SDL2, FastNoiseLite, and `darwin_warnings` — **no** `darwin_*` static libraries.
- Keeps all headers local to its own directory (no `include/` layer).
- Uses its own `sandbox` namespace.
- Is built under `DARWIN_BUILD_SANDBOXES` (ON by default) and launched with `make run-sandbox SANDBOX=<name>`.

A CMake helper `add_darwin_sandbox(name)` in `sandboxes/CMakeLists.txt` enforces this dependency set uniformly so new sandboxes cannot accidentally link a `darwin::*` module.

The promotion path is explicit: once an algorithm is proven in a sandbox, the code is rewritten to fit the `darwin::*` module conventions (public header in `include/darwin/<module>/`, `darwin::<module>` namespace, CMake library target) and the sandbox becomes a regression reference or is removed.

The first sandbox, `sandboxes/worldgen/`, implements the full procedural world generation pipeline with an interactive SDL2 viewer, overlays, camera, and live parameter tweaking.

## Consequences

- **Good**: Exploratory code cannot corrupt the acyclic module graph — the isolation is enforced by CMake.
- **Good**: SDL2 rendering is available immediately for visual verification without adding it as a dependency to `darwin::world` or `darwin::environment`.
- **Good**: Sandboxes compile quickly because they carry no `darwin::*` baggage.
- **Good**: The `format` target and CI coverage include `sandboxes/` so code quality stays consistent.
- **Tradeoff**: Any code shared between a sandbox and a `darwin::*` module must be duplicated until the sandbox is promoted. There is no incremental linking between the two layers by design.
- **Tradeoff**: Sandboxes can diverge from the module they are meant to prototype; the promotion step requires a deliberate rewrite rather than a simple move.
