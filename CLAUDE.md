# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Darwin Chronicles is a 2D evolution simulation in C++20 using an Entity Component System (EnTT). Organisms with genome-encoded neural networks sense, think, act, metabolize, learn, and reproduce in a procedurally generated world driven by planetary physics. All behavior is emergent — no hard-coded AI.

## Build Commands

```bash
make build                          # Debug build (cmake -B build + build)
make build-release                  # Release build
make test                           # Build + run all Catch2 tests
make bench                          # Build + run Google Benchmark suite
make run                            # Build + run main executable
make run-example EXAMPLE=earth_like # Run a specific example
make run-sandbox SANDBOX=worldgen   # Run a sandbox executable
make format                         # clang-format all source files (includes sandboxes/)
make clean                          # Remove build directories
```

CMake options: `DARWIN_BUILD_TESTS` (ON), `DARWIN_BUILD_BENCHES` (OFF), `DARWIN_BUILD_EXAMPLES` (ON), `DARWIN_BUILD_SANDBOXES` (ON).

Run a single test by name:
```bash
./build/tests/darwin_tests "[environment]"   # Run tests tagged [environment]
```

## Architecture

### Module Dependency Graph (acyclic)

```
environment  ← lowest level (planetary physics, climate)
    ↑
   world     ← terrain, biomes, nutrient fields (uses FastNoiseLite)
    ↑
organisms    ← genome, neural brain, metabolism, sensors
    ↑
evolution    ← reproduction, mutation, selection
    ↑
simulation   ← tick loop, system scheduling (depends on all above)
    ↑
rendering    ← sprite gen, camera, debug viz, UI (depends on simulation + SDL2)
```

Each module is a static library (`darwin_<module>`) with a CMake alias (`darwin::<module>`). The main executable links `darwin::simulation` and `darwin::rendering`.

### Key Patterns

- **ECS**: All organism state lives in EnTT components (Genome, Brain, Energy, GridPosition). Systems process components in order: Sense → Think → Act → Metabolize → Learn → Reproduce.
- **Three-layer cascade**: Planetary physics → climate constraints → evolutionary pressure. Environment parameters drive everything downstream.
- **Energy tradeoffs**: Brain complexity, body size, speed all have metabolic costs. Organisms that over-invest in one trait die from energy depletion.
- **Genome structure**: 152 floats encoding architecture genes, morphology, physiology, and neural weights. Sprites are procedurally generated from genome data.

### Sandboxes

`sandboxes/<name>/` are standalone executables for prototyping subsystems in isolation. They do **not** link any `darwin::*` modules — only SDL2, FastNoiseLite, and `darwin_warnings`. Code proven in a sandbox can later be promoted into a `darwin::*` module.

The `add_darwin_sandbox(name)` CMake helper (defined in `sandboxes/CMakeLists.txt`) wires up the standard sandbox dependencies. Each sandbox lives in its own subdirectory and produces a `sandbox_<name>` binary at `build/sandboxes/<name>/sandbox_<name>`.

Current sandboxes:
- `sandboxes/worldgen/` — Interactive procedural world generation (SDL2 rendering, overlays, camera, live parameter tweaking, screenshot/log export)

### File Layout Convention

- Public headers: `include/darwin/<module>/<file>.h` (use `#include <darwin/world/grid.h>`)
- Source files: `src/<module>/<file>.cpp`
- Each module's `src/<module>/CMakeLists.txt` defines the library target
- Module design docs: `src/<module>/README.md` and `docs/systems/*.md`
- Sandbox sources: `sandboxes/<name>/*.{h,cpp}` (no `include/` subdirectory; headers are local)

## Code Style

Google C++ style with modifications (see `.clang-format`):
- 4-space indent, 100-char line limit
- `#pragma once` for header guards
- Namespace: `darwin::<module>` (e.g., `darwin::world`, `darwin::organisms`)
- Pointer alignment: left (`int* ptr`)
- Aggressive warnings enabled (see `darwin_warnings` target in root CMakeLists.txt)

## Dependencies (via FetchContent)

EnTT v3.13.2, FastNoiseLite v1.1.1, SDL2 (system or v2.30.10 fallback), Catch2 v3.7.1, Google Benchmark v1.9.1.
