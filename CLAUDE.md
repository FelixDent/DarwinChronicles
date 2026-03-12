# CLAUDE.md

## Project

Darwin Chronicles is a 2D evolution simulation in C++20 using ECS (EnTT). Organisms with genome-encoded neural networks sense, think, act, metabolize, learn, and reproduce in a procedurally generated world driven by planetary physics. All behavior is emergent.

## Core Philosophy: Geological & Physical Realism

**Every mechanism must have a real-world physical or geological basis.**

- **No cosmetic post-processing**: Fix the physics, don't paint over output.
- **Process-driven emergence**: Complexity from interacting physical processes, not layered noise.
- **Fix root causes**: Trace backwards to the missing/miscalibrated process. Never fix output directly.

## Build Commands

```bash
make build                          # Debug build
make build-release                  # Release build
make test                           # Build + run all Catch2 tests
make bench                          # Build + run Google Benchmark suite
make run-sandbox SANDBOX=worldgen   # Run a sandbox executable
make format                         # clang-format all source files
```

## Architecture

### Module Dependency Graph

```
environment → world → organisms → evolution → simulation → rendering
```

Each module is `darwin_<module>` with alias `darwin::<module>`. Sandboxes don't link darwin modules — only SDL2, FastNoiseLite, `darwin_warnings`.

### Sandbox Layering

```
Layer 1: Terrain (worldgen) → Layer 2: Weather (weather) → Layer 3: Vegetation (veggen) → Layer 4: Creatures (future)
```

**Shared code rules:**
- Never duplicate — share via `#include` + CMakeLists source paths
- Canonical locations: `terrain_gen`, `tile_texture`, `ui.h` in `weather/`; `vegetation` in `veggen/`
- Features must work in ALL consuming sandboxes before declaring done
- Each layer only owns its own attributes (no weather data in terrain gen, etc.)

### Sandboxes

- **`worldgen/`** — Tectonic terrain generation (12-stage pipeline: plates → Voronoi → tectonic fields → erosion → geological depressions → coastal remodeling → geology). Stage 4b creates lake basins via three physically-justified processes: tectonic grabens (rift floor subsidence at divergent boundaries, en echelon segmented), glacial scouring (overdeepened finger-lake troughs + kettle fields on flat outwash, latitude+elevation gated), and volcanic calderas (circular rimmed basins near convergent arcs, up to 6 per world). Produces 10–60 lakes per preset after weather simulation fills depressions. Modes: `--headless`, `--tile-test`. 9 overlays, 6 presets. Shares `terrain_gen.h/cpp` and `tile_texture.h/cpp` from `weather/`.
- **`weather/`** — Atmosphere + hydrology on top of worldgen terrain. Static bake → QG atmosphere simulation (stochastic baroclinic eddies, two-layer streamfunction) → dynamic hydrology (WSE routing, D8 flow accumulation, basin spillway lakes). Autotile terrain rendering (discrete 16×16 templates, 8 hypsometric elevation bands, bezier coast/elevation transitions). 20 overlays, clipmap cache (macro 4px/tile + detail 16px/tile baked at startup). Modes: `--headless N [preset]`, `--screenshots [preset] [days]`.
- **`veggen/`** — Vegetation simulation, 8 biome presets, two-layer moisture model, density-dependent growth. Modes: `--auto [seed]`, `--metrics [seed] [years]`.

### Key Data Structures

- `TerrainTile`: `elev01`, `slope01`, `aspect`, `dist_ocean`, `roughness`, `rock` (RockType), `soil` (SoilTexture), geology fields (`bedrock_hardness`, `porosity`, `ksat`, `field_capacity`, `wilting_point`, `erodibility`). No hydrology fields (rivers/lakes emerge dynamically in weather layer).
- `EnvParams`: `water_level`, `plate_count` (0=legacy noise, >0=tectonic), `continental_ratio`, 6 presets.
- `AtmosphereCell`: baseline (`T_base`, `q_base`, `p_base`), anomaly (`T_anom`, `q_anom`, `p_anom`), QG fields (`psi1`, `psi2`, `q1_pv`, `q2_pv`), upper layer, composite fields.
- `DynamicTile`: `surface_water`, `soil_moisture`, `snow_depth`, `groundwater`, `discharge`, `aridity`.
- `TileClassification`: `TileFamily` (PureTerrain/Coast/Elevation), `EdgePair`, `TransitionGrade` (Shallow/Medium/Steep).
- `TemplateAtlas`: rotation LUT, pure terrain templates per `Mat` (12 materials), transition masks.

## Code Style

Google C++ style: 4-space indent, 100-char lines, `#pragma once`, namespace `darwin::<module>`, left pointer alignment.

## Mandatory Processes

### After Major Changes (in order):
1. **Critical change reviewer** → fix all findings immediately
2. **Perf/UX reviewer** (if runtime code touched) → fix findings
3. **Docs maintainer** → update CLAUDE.md, CHANGELOG, README, ARCHITECTURE
4. **Commit and push** — don't accumulate uncommitted work

### Simulation Changes:
- **Must verify by running** — never declare done based on "it compiles"
- **No artificial equilibrium forcing** — fix root causes, not symptoms
- **Reality check skill** for procedural/emergent output

### UI Changes:
- Follow `sandbox-ui` skill; record lessons when user corrects mistakes
- GPT-assisted review via `gpt-ui-review` skill for visual polish

### Procedural Sprites:
- Follow `realistic-sprite-gen` skill + visual review loop

## Dependencies

EnTT v3.13.2, FastNoiseLite v1.1.1, SDL2 (system or v2.30.10), Catch2 v3.7.1, Google Benchmark v1.9.1.
