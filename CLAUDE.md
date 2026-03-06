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
- `sandboxes/worldgen/` — Interactive procedural world generation (SDL2 rendering, overlays, camera, live parameter tweaking, screenshot/log export). Eleven-stage pipeline: (1) height field with domain warping (two Perlin layers displace coordinates up to 20 tiles for organic landforms), (2) ridge field (ridged multifractal noise for mountain ranges, kept for renderer decoration in `TerrainTile::elevation`), (3) unify height (ridge merged into continent height at scale ~0.18 so mountains affect hydrology), (4) ocean/lake flood-fill (BFS, edge-connected + ≥200 tiles = ocean), (5) slope + aspect (atan2 of gradient) + elevation bands from unified height, (6) Euclidean Distance Transform via Felzenszwalb separable parabola-envelope (replaces BFS Manhattan distances), (7) soil fertility/hold, (8) roughness as 3×3 neighborhood height stddev (replaces ridge+slope blend), (9) 8-directional downhill routing (D8) + lake outflow spill points, (10) flow accumulation → `river_flow` (log₂-scaled). `TerrainTile` fields: `elev01`, `band`, `is_ocean/lake`, `slope01`, `aspect`, `dist_ocean/water`, `river_flow`, `soil_fertility/hold`, `roughness`, `elevation`, `downhill_x/y`. 11 overlay modes (keys 1-9, 0, -): None, Continental, Slope, ElevBand, DistOcean, DistWater, SoilFertility, SoilHold, Roughness, Aspect, RiverFlow. `GenerationTimings`: `height_ms`, `ridge_ms`, `unify_ms`, `ocean_lake_ms`, `slope_band_ms`, `dist_fields_ms`, `soil_ms`, `roughness_ms`, `downhill_ms`, `river_ms`, `total_ms`. 6 terrain presets (earth, venus, mars, snowball, tropical, toxic).
- `sandboxes/weather/` — Atmospheric weather system layered on top of worldgen terrain. Three-phase architecture:
  - **Static bake** (`weather.h/cpp`, `bake_weather`): latitude-based temperature with lapse rate, 3-cell wind model (trade/westerly/polar), moisture-flux precipitation sweeps (zonal + meridional) with orographic uplift, evaporation demand, storminess (5-factor model), and rain shadow detection.
  - **Atmosphere simulation** (`atmosphere.h/cpp`): coarse-grid atmospheric state (temperature T, specific humidity q, pressure p, wind u/v, cloud, precip_rate, storminess) running at 4× terrain tile scale. Bilinear interpolation to terrain resolution via `AtmosphereState::sample()`; nearest-neighbour sampling via `sample_nearest()` used for Precipitation and Storminess overlays to show sharp shower boundaries rather than blurred gradients. Tick order (critical — must not be reordered): pressure→wind→advection→diffusion→solar forcing→moisture. Solar forcing runs AFTER advection so transported air masses are forced in-place rather than having forced temperatures overwritten by upwind stale values. Key physics: solar forcing (K_SOLAR=8.0), temperature reference `T_ref = -15 + solar_seasonal * 48` with `season_offset * latitude * 12` modulation (equator peak ~30°C in summer, poles ~ -15°C baseline; allows heat waves > 30°C and deep polar cold), temperature relaxation toward T_ref (K_RELAX_T=0.05, nonlinear — strengthens for large drifts), east-west periodic boundary advection (ADVECT_SPEED=0.5) with partial temperature advection (15% local retention to preserve latitude gradient against numerical diffusion), minimum equatorial Coriolis deflection (0.04 floor prevents purely meridional flow), wind damping (K_DAMP=0.05), latent heat from condensation (K_LATENT=8.0), ocean evaporation (OCEAN_EVAP_RATE=0.15), land evaporation (LAND_EVAP_RATE=0.05), multi-scale thermal turbulence (fast ~1-hour gusts + slow ~6-hour mesoscale shifts, daytime-boosted, weaker over water). Storminess uses a multiplicative formula `humidity_ratio * instability^3 * (1 + cloud*0.5)` where instability requires convergence > 0.2 threshold — produces realistic ~15-19% severe storm coverage (old additive formula saturated most cells at 1.0). **Precipitation**: physics-based cloud lifecycle — noisy condensation thresholds cause cloud thickening; when cloud exceeds CLOUD_PRECIP_THRESHOLD (0.12) precipitation falls at CLOUD_PRECIP_RATE (1.2) and cloud dissipates (CLOUD_EVAP_BASE=0.15, CLOUD_EVAP_DRY=0.40) with pressure-driven subsidence (CLOUD_SUBSIDE_RATE=0.50). Condensation noise uses temporal crossfade (cubic Hermite blending between timesteps) to prevent global pattern teleport; two decorrelated noise layers (+1000 spatial offset on second layer). `hash01()` takes `uint32_t` parameters to avoid signed integer overflow UB. Orographic precipitation remains continuous. Ten physical invariants enforced each tick:
    - Stefan-Boltzmann radiative cooling (T^4 law, normalized to match linear rates near equilibrium)
    - Water mass conservation: total atmospheric water (q + cloud) monitored; advection drift > 0.1% triggers a gentle proportional correction
    - Clausius-Clapeyron hard clamp: q clamped to q_sat (temperature-derived saturation) — not just 1.0
    - Wind speed hard cap: MAX_WIND_SPEED = 6 cells/day prevents numerical blowup
    - Heat diffusion: explicit Laplacian diffusion on T field with east-west periodic wrap
    - Moisture diffusion: explicit Laplacian diffusion on q field
    - Convective instability release: when ground-air temp diff exceeds threshold, forced condensation + enhanced precipitation dumps latent energy and moisture
    - Evaporation limits: land evaporation hard-capped by available soil moisture
    - CFL/Courant limiter: advection trace distance clamped to ≤ 1.5 cells to prevent overshooting
    - Surface water overflow (dynamics): excess surface_water above 1.0 infiltrates soil instead of being discarded
    - `AtmosphereState` tracks `total_water` and `last_water_correction` for conservation bookkeeping
  - **Dynamic simulation** (`dynamics.h/cpp`): per-tick terrain interaction reading from atmosphere when available:
    - Precipitation → surface_water (rain) or snow_depth (below freezing); capped at 1.0
    - Snowmelt → surface_water when temperature > 0°C (creates seasonal floods)
    - Infiltration: surface → soil_moisture, modulated by soil_hold and slope
    - Evaporation: temperature + wind driven, suppressed by local_humidity
    - Runoff: surface_water routes to precomputed downhill neighbors weighted by slope
    - Terrain feedback: wet tile evaporation contributes to local_humidity
  - **Telemetry**: `AtmosphereStats` struct (T/q/cloud/precip/wind/pressure/storm min/max/mean, raining_cells, cloudy_cells, total_atmo_water, max_courant, water_correction, plus latitude-band arrays T_band[3]/wind_band[3]/q_band[3] for north/equatorial/south diagnostics). Dynamic legend panel shows live atmosphere + dynamics stats including invariant diagnostics. Console diagnostics dump every 2 sim-days showing band temperatures and wind alongside global stats.
  - **Overlays** (keys 1-9, 0, -, =): None, Temperature, Precipitation, Moisture (atmospheric humidity), Wind, Evaporation, Storminess, MoistureBars, RainShadow (dynamic, traces current wind field), SurfaceWater, SoilMoisture, SnowDepth
  - **Controls**: T=play/pause dynamics, R=reset dynamics, Numpad +/-=speed, V=wind arrows, G=grid, N/P=preset cycle, Space=rebake weather
  - **Headless mode**: `sandbox_weather --headless N` runs N sim-days without a window and prints per-2-day diagnostics (latitude-band temperatures, wind by band, q/cloud/precip means, raining cell count, cloudy cell count, total atmospheric water). The raining/cloudy cell counts monitor spatial patchiness produced by precipitation intermittency. Final summary shows cumulative peak extremes across the entire run: peak T range, heat wave / deep freeze / severe storm / gale / heavy rain cell counts, and peak precipitation intensity. Prints warnings if temperature spread collapses, latitude bands equalize, or wind dies. Used for verifying atmosphere simulation stability without a display.
  - Terrain rendering tints dynamically: wet soil darkens/greens, snow whitens, standing water tints blue
- `sandboxes/veggen/` — Vegetation simulation across 8 biome presets with spatial terrain model. Interactive SDL2 mode with live overlays and speed control. Two headless CLI modes:
  - `sandbox_veggen --auto [seed]` — renders all biomes at four seasonal snapshots and saves BMP files
  - `sandbox_veggen --metrics [seed] [years]` — runs all biomes for N years (default 10), exports per-biome CSV and prints a population/mortality summary to stdout
  - **Terrain system** (implements `docs/ideation/improvements.md` Iteration A):
    - Per-tile spatial variation: elevation gradients + noise, slope from neighbors, temperature lapse rate + micro-noise offsets, orographic precipitation factors
    - Two-layer moisture model: `surface_water` (fills from rain, drains via runoff + infiltration) and `root_moisture` (what plants use). `TileState.soil_moisture()` returns `root_moisture`.
    - Hydrology: rain → surface_water → infiltration → root_moisture, evaporation (reduced by canopy shade up to 40%), runoff to downhill neighbors
    - Plant→environment feedback: `canopy_cover` computed from living plant biomass, reduces evaporation demand
  - **Simulation features**: density-dependent growth (15% penalty per neighbor), soft population cap, senescence with species-specific lifespans, mature-plant reproduction, five death causes (Drought, Cold, Heat, Competition, Senescence), continuous stress recovery
  - **Overlays** (keys 1-7): None, Root Moisture, Temperature, Health, Elevation, Surface Water, Canopy Cover
  - **UI panel**: grid-wide averages for temp, precip, root moisture, surface water, canopy %, elevation range, live plant count
  - Metrics snapshots every 30 sim-days: population by species, age distribution, mortality breakdown by cause, replacement ratio, growth-density correlation, environment averages
  - `sandboxes/spritetest/` shares `veggen/plant_sprite.cpp`, `veggen/vegetation.cpp`, and `veggen/metrics.cpp` directly via its CMakeLists.txt

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

## Documentation Maintenance (MANDATORY)

After every major change (new feature, new sandbox, refactored system, new overlays/controls/CLI modes), **you MUST run the docs-maintainer skill** (`.claude/skills/docs-maintainer/SKILL.md`) to update all affected documentation. Use a subagent to do this so it doesn't bloat the main conversation.

**What counts as a major change:**
- Adding or modifying a sandbox
- Adding or changing simulation systems, data structures, overlays, or controls
- Changing build system, dependencies, or CLI interfaces
- Adding new files or modules

**Files that must stay in sync:** `CLAUDE.md` (sandboxes, build commands), `docs/CHANGELOG.md`, `README.md` (structure tree, status), `docs/ARCHITECTURE.md` (sandboxes, module graph). See the skill for full details.

**Do not skip this.** Stale docs cause compounding confusion across sessions. If in doubt, run the skill anyway — it will no-op on files that are already current.

## Critical Change Review (MANDATORY)

When the `critical-change-reviewer` agent completes, **immediately address every finding** before moving on to other work. Do not defer, summarize-and-skip, or wait for the user to ask. Treat the review output as a punch list:

1. **Critical issues**: Fix immediately in the same response.
2. **Edge cases / medium severity**: Fix immediately unless the fix would be destabilizing — in that case, explain why you're deferring and get user confirmation.
3. **Minor notes / style**: Fix in the same pass if trivial (dead code, wrong comments, missing guards). Flag to the user if non-trivial.
4. **Performance concerns**: Note them but only fix if the user requests it or the cost is clearly unacceptable.

After addressing all findings, briefly list what was fixed and what (if anything) was intentionally deferred with reasoning.

## Simulation Changes: Verify Before Declaring Done (MANDATORY)

When modifying **any** simulation system — weather, atmosphere, dynamics, vegetation, evolution, organisms, energy, neural networks, or any future system — **you MUST verify the change works by running it and analyzing output** before telling the user it's done. Never declare a simulation change complete based solely on "the code compiles" or theoretical reasoning.

**Verification process:**
1. Build the relevant sandbox, test, or executable.
2. Run it and capture diagnostic output. Duration depends on the system's timescale — enough to see whether the system stabilizes, oscillates correctly, or drifts.
3. Analyze the output for stability: check that key quantities don't drift monotonically, collapse to uniform, or diverge.
4. Check for the specific behavior the change was meant to produce — don't just check that nothing crashes.
5. If output reveals a problem, diagnose and fix it before responding to the user.
6. For systems with multiple configurations or presets, test across several — not just the default.

**What to watch for (general):**
- Any quantity growing or shrinking monotonically without bound (runaway)
- Values collapsing toward uniformity across all cells/entities (dead system)
- Values clustering at clamp boundaries (0.0, 1.0) — suggests over-clamping or wrong rates
- Oscillations that amplify rather than dampen (numerical instability)
- Spatial variation disappearing (all regions behaving identically when they shouldn't)
- Populations crashing to zero or exploding to cap without environmental pressure

**If the system requires a display and you can't run it headlessly**, add a headless CLI mode that runs the simulation loop and prints diagnostics, or write a minimal test that exercises the system and prints stats.

## Sandbox UI Changes

When making UI changes to any SDL2 sandbox (overlays, legends, panels, buttons, camera):

1. **Follow the `sandbox-ui` skill** (`.claude/skills/sandbox-ui/SKILL.md`) — layout constants, spacing rules, color conventions, performance thresholds.
2. **When the user corrects a UI mistake**, update the "Lessons Learned" section at the bottom of the skill file with the date and what went wrong. This is how the skill improves over time.

## Procedural Sprite Generation

When modifying any procedural sprite generation code (e.g., `sandboxes/veggen/plant_sprite.cpp`):

1. **Follow the `realistic-sprite-gen` skill** (`.claude/skills/realistic-sprite-gen/SKILL.md`) — structure first, then layers, environment drives traits, enforce constraints.
2. **Always use the visual review loop** (`.claude/skills/sprite-visual-review/SKILL.md`) — build, render, view PNG, assess, fix, repeat. Never commit sprite generation changes without visually verifying the output.
3. **Physical consistency**: health effects (stressed/brown/dead) are post-processing on the same skeleton. An organism that loses surface detail must show the same underlying structure. Never regenerate structure per health state.

## Dependencies (via FetchContent)

EnTT v3.13.2, FastNoiseLite v1.1.1, SDL2 (system or v2.30.10 fallback), Catch2 v3.7.1, Google Benchmark v1.9.1.
