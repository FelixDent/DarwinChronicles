# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Darwin Chronicles is a 2D evolution simulation in C++20 using an Entity Component System (EnTT). Organisms with genome-encoded neural networks sense, think, act, metabolize, learn, and reproduce in a procedurally generated world driven by planetary physics. All behavior is emergent — no hard-coded AI.

## Core Philosophy: Geological & Physical Realism

**Every mechanism in the simulation must have a real-world physical or geological basis.** This is the project's constitution — it applies to terrain generation, weather, hydrology, ecology, and all future systems.

- **No cosmetic post-processing**: Never add noise, amplification, or correction terms to make output "look right." If mountains look flat, fix the tectonic uplift or erosion physics — don't paint ridges on afterwards.
- **Process-driven emergence**: Complexity must emerge from the interaction of physically-grounded processes (tectonic stress → uplift → differential erosion → ridge-valley relief), not from layered noise functions.
- **Internal consistency**: Because every system has a physical basis, outputs are automatically consistent with each other (geology constrains hydrology constrains ecology). Cosmetic patches break this consistency.
- **Fix root causes**: When output doesn't match reality, trace backwards to find which physical process is missing, miscalibrated, or oversimplified. The fix is always in the process, never in the output.

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

**Sandbox layering — each layer builds on the one below:**

```
Layer 1: Terrain   (worldgen)   ← static geology, elevation, coastlines
    ↓ consumed by
Layer 2: Weather   (weather)    ← atmosphere, precipitation, rivers, lakes
    ↓ consumed by
Layer 3: Vegetation (veggen)    ← plant growth, biomes, canopy
    ↓ consumed by
Layer 4: Creatures  (future)    ← organisms, neural nets, evolution
```

**Code that serves the same purpose MUST be shared, not duplicated.** When a higher-layer sandbox needs lower-layer functionality (terrain generation, tile rendering, camera, UI primitives), it must `#include` and link the lower-layer source files — never copy-paste them into a separate file. Use the sandbox's `CMakeLists.txt` to add source files from sibling sandbox directories (see `sandboxes/spritetest/CMakeLists.txt` for the pattern). The canonical version of each system lives in the sandbox where it is most actively developed:
- **Terrain generation** (`terrain_gen.h/cpp`): canonical in `weather/`
- **Tile texture rendering** (`tile_texture.h/cpp`): canonical in `weather/`
- **Camera, UI primitives** (`ui.h`): canonical in `weather/`
- **Vegetation** (`vegetation.h/cpp`): canonical in `veggen/`

When adding a feature to a shared system (e.g., clipmap terrain rendering), it must work in ALL sandboxes that use that system. If you add zoom-aware rendering to the weather sandbox's terrain renderer, the worldgen sandbox must get it too. **Do not declare a shared-system feature "done" until all consuming sandboxes are updated.**

**Each layer only owns its own attributes.** Terrain generation (Layer 1) produces static geology: elevation, slope, aspect, rock type, soil texture, bedrock hardness, etc. It does NOT produce any attributes that depend on weather — no moisture, no rivers, no hydrology, no vegetation. Those attributes are created and updated only when the weather system (Layer 2) is active. Similarly, vegetation attributes only exist when the vegetation system (Layer 3) is active. A lower-layer sandbox must compile and run without any higher-layer code.

Current sandboxes:
- `sandboxes/worldgen/` — Interactive procedural world generation (SDL2 rendering, overlays, camera, live parameter tweaking, screenshot/log export). Two generation modes selected by `EnvParams::plate_count`: **tectonic mode** (`plate_count > 0`, default) and **legacy noise mode** (`plate_count == 0`). **Tectonic mode** pipeline (12 stages): (1) plate generation — `plate_count` plates with seeded centers, velocity vectors, and continental/oceanic type; continental plates receive an inward velocity bias (simulating supercontinent assembly), oceanic plates receive an outward spreading bias; each continental plate is assigned a `spine_dx/spine_dy` direction (perpendicular to velocity) and a random `spine_offset`; (2) domain-warped Voronoi — two-layer warp (WARP1: 65-tile amplitude at 0.004 freq, WARP2: 25-tile at 0.010 freq); **boundary distance noise**: FBm noise perturbation on raw Voronoi distance to make boundaries wavy rather than linear; **boundary normal rotation**: per-tile noise-based rotation up to ~35° breaks straight plate boundary alignment; boundary normal/tangent, and signed convergence field computed per tile; (3) continuous tectonic fields — `continentality = blended_cont × 0.55 + crust_noise × 0.25 + cont_warp_noise × 0.20` drives land/ocean; coastlines emerge organically from this field; **province/craton mask**: per-tile tectonic activity score gates meso_ridge_noise, terrain_var_noise, and detail noise — active zones (plate boundaries + young collision belts) receive full detail, stable cratons receive reduced variation; continental interior structure: shield uplift (σ=90 Gaussian), **spine** (directional ridge perpendicular to plate drift, wider Gaussian σ=40 with along-strike fragmentation noise for peaks/saddles, uplift up to 0.15×ridge_strength), **meso_ridge_noise** (ridged, 3 octaves, 0.012 freq, seed+8000 — interior escarpments, province-gated), `terrain_var_noise` (FBm, 0.004 freq, land scale 0.12, province-gated), `basin_noise` (FBm, 0.012 freq, max depression 0.14); **curl noise ridge warp** (seed+26000/26001, ~22-tile displacement) curves ridge geometry; **lithology noise** (seed+27000, 0.005 freq) spatially varies ridge frequency; **piedmont roughness** (seed+28000, 0.025 freq) adds foothill roughness at mountain margins; **multi-direction ridge blending** (40% primary + 25% rotated 30° + 35% isotropic) prevents ridges from all aligning parallel to plate boundaries; boundary stress: continent-continent → plateau + **along-strike segmented peaks** (belt-var noise modulates peak height with ±40% amplitude along range strike, breaking ranges into massifs and saddles) + foothill envelope (σ=90) + foreland basin; ocean-continent → coastal arc (`MOUNTAIN_PEAK × 1.1`) + foothill envelope (σ=80) + back-arc basin + offshore trench; ocean-ocean → island arc; rift → depression + shoulders; mid-ocean ridge → central uplift; transform → scarp; secondary ridge spurs; 6-octave micro-detail FBm (province-gated); edge falloff; **(3b) three-scale relief domain warp** — 12+5+2 tile displacement at continental/regional/local scales breaks any remaining linear tectonic artifacts before erosion; (4) material-conserving thermal erosion — 6 iterations, `THERMAL_RATE=0.12`, `MAX_STABLE_SLOPE=0.055` (gentler than before to complement stream-power erosion); **(5) stream-power erosion** — 40-iteration stream-power law erosion with D8 flow accumulation, stream-power carving proportional to flow×slope, rock hardness modulation, **mountain canyon boost** (2× carving for high-elevation high-flow tiles), and sediment capacity model (deposit when stream loses power rather than fixed fraction); **(5b) valley-floor smoothing** — targeted low-pass filter, smoothing strength scaled by elevation so mountain terrain is unchanged; **(5d) valley drowning** — rias, estuaries, and fjords carved by lowering high-flow valley floors near the coast below water level using flow accumulation + sea-level anomaly noise + fjord detection (high relief + high latitude); (6) distance-from-coast inland elevation bias — BFS + exponential saturation (0.12 uplift, half-life 15 tiles); **(6b) noise-warped coastal plain enforcement** — tiles within a noise-warped nearshore band (1.5–7 tile width, slope-gated: steep coasts skipped) are lowered to form flat coastal lowlands; **(7) exposure-based SDF coastal remodeling** — replaces previous uniform fractalization: (7a) BFS signed distance field from coastline; (7b) 16-direction fetch raymarching (max 60 tiles) for wave exposure; (7c) coastal slope proxy + 3-pass Gaussian exposure blur; (7d) exposure-driven anisotropic displacement with spectral control (sheltered coasts suppress high-freq 90%, mid 70%, broad 30%) + gated coastal profile ramp (1–3 tile width, 50% blend, respects cliffs); **(7b) depositional features** — river-mouth deltas (fan-shaped lobes with distributary channels) and barrier islands (offshore ridges with tidal inlet gaps) on moderate-exposure coasts; **(7c) clustered skerry generation** — Poisson cluster centers (8-tile min spacing) on exposed headlands spawning islet fields in 3–6 tile radius; **(7d) sub-bay inlet carving** — steepest-ascent tracing up to 8 tiles inland in sheltered concavities; (8) basin flood-fill lakes — priority-queue expansion from local minima (LAKE_DEPTH_MAX=0.08, min 4 tiles, max 200 basins); (9) water speckle removal — connected-component analysis removes water bodies smaller than 3 tiles; **(9b) debris islet removal** — land bodies smaller than 8 tiles removed; **(9c) morphological narrow feature cleanup** — neck removal, corner bridge removal, peninsula tip pruning; **(10) SDF curvature flow coastline smoothing** — 4 iterations mean-curvature flow (dt=0.18) restricted to 3-tile coastal band, producing anti-aliased organic coastlines (replaces previous Laplacian smoothing). **Geology stage** (shared post-ocean): `compute_geology()` assigns `RockType` (Granite/Basalt/Limestone/Sandstone/Shale/Metamorphic) and soil properties (`SoilTexture`, `bedrock_hardness`, `bedrock_permeability`, `soil_depth`, `porosity`, `ksat`, `field_capacity`, `wilting_point`, `erodibility`) based on tectonic context — convergent boundaries → Metamorphic/Granite, volcanic arcs → Basalt, stable cratons → Granite/Sandstone, coastal lowlands → Shale/Silt/Clay, etc. **Legacy noise mode**: (1) height field with domain warping (two Perlin layers, up to 20-tile displacement), (2) ridged multifractal noise, (3) unify height (ridge merged at scale ~0.18). Both modes share stages after height generation: ocean flood-fill (BFS, edge-connected + ≥200 tiles = ocean — no lake classification at terrain-gen time), slope + aspect (atan2 of gradient) + **relief-based elevation bands** (R=4 neighborhood: Mountains = relief > 0.22 AND slope > 0.18; Hills = relief > 0.12 OR (slope > 0.12 AND elev > water_level+0.12)), Euclidean Distance Transform via Felzenszwalb separable parabola-envelope (`dist_ocean` only), roughness (3×3 height stddev), geology assignment. **Hydrology is no longer computed at terrain-gen time** — no D8 downhill routing, no flow accumulation, no lakes. Rivers and lakes emerge dynamically from precipitation interacting with the geological substrate in the weather sandbox's dynamic simulation (WSE multi-neighbor routing + groundwater baseflow). **Headless mode**: `--headless` generates terrain, renders a BMP (NW directional lighting 315° azimuth / 40° altitude, slope exaggeration 6×), prints stats to stdout, and exits. Output filename: `terrain_{preset}_{seed}.bmp`. Headless BMP uses: **gamma-compressed ocean** (`depth^0.6` gamma before quadratic color gradient, no coastal halo ring); **tectonics-aware bathymetry** (margin-dependent shelf width, convergent margin trenches); **climate-driven biome land coloring** (7-biome logistic blend from latitude+lapse temperature + composite moisture model); **aspect-based snow** (slope sheds snow, pole-facing preference, moisture modulation); **aspect-based mountain albedo** (sun/shadow face warm/cool shift). `EnvParams` fields: `water_level`, `noise_scale`, `continent_threshold`, `ridge_strength`, `plate_count` (0=legacy, >0=tectonic, default 12), `continental_ratio` (default 0.40). `TerrainTile` geometry fields: `elev01`, `band`, `is_ocean`, `slope01`, `aspect`, `dist_ocean`, `roughness`, `elevation`. `TerrainTile` geology fields: `rock` (RockType enum), `bedrock_hardness`, `bedrock_permeability`, `soil` (SoilTexture enum), `soil_depth`, `porosity`, `ksat`, `field_capacity`, `wilting_point`, `erodibility`. Removed fields: `is_lake`, `dist_water`, `river_flow`, `soil_fertility`, `soil_hold`, `downhill_x/y`. 9 overlay modes (keys 1-9): None, Continental, Slope, ElevBand, DistOcean, Roughness, Aspect, Geology, SoilTexture. `GenerationTimings`: `height_ms`, `ridge_ms`, `unify_ms`, `ocean_ms`, `slope_band_ms`, `dist_fields_ms`, `geology_ms`, `roughness_ms`, `total_ms`. Key constants: `MOUNTAIN_PEAK=0.75`, `WARP1_STRENGTH=65`, `WARP2_STRENGTH=25`, `THERMAL_RATE=0.12`, `STREAM_POWER_ITERS=40`. 6 terrain presets (earth, venus, mars, snowball, tropical, toxic), each with tuned `plate_count` and `continental_ratio`.
- `sandboxes/weather/` — Atmospheric weather system layered on top of worldgen terrain. Three-phase architecture:
  - **Static bake** (`weather.h/cpp`, `bake_weather`): latitude-based temperature with lapse rate, 3-cell wind model (trade/westerly/polar), moisture-flux precipitation sweeps (zonal + meridional) with orographic uplift, evaporation demand, storminess (5-factor model), rain shadow detection, and initial aridity index seeded from `precip / (evap + 0.01) * 0.8` (proper ratio-based formula replacing the previous arbitrary `precip - evap * 0.5`).
  - **Atmosphere simulation** (`atmosphere.h/cpp`): coarse-grid atmospheric state running at 4× terrain tile scale. `AtmosphereCell` carries: climate baseline fields (`T_base`, `q_base`, `p_base` — slow, seasonal), weather anomaly fields (`T_anom`, `q_anom`, `p_anom` — fast, set by QG thermal wind + advected), composite fields (`T`, `q`, `p`, `u`, `v`, `cloud`, `precip_rate`, `storminess`, `ground_temp`), terrain coupling fields (`avg_slope`, `avg_aspect`, `avg_dist_ocean`), moisture budget (`precip_budget`), upper troposphere (`T_upper`, `q_upper`, `u_upper`, `v_upper`, `p_upper`, `stability`), and **QG dynamics fields** (`psi1`, `psi2` — upper/lower streamfunctions; `q1_pv`, `q2_pv` — upper/lower potential vorticity). `AtmosphereState` additionally tracks `initial_energy` and `total_energy` for energy conservation. Bilinear interpolation to terrain resolution via `AtmosphereState::sample()`; nearest-neighbour via `sample_nearest()` for Precipitation and Storminess overlays. Tick order (critical — must not be reordered): (1) terrain coupling→(1b) seasonal baseline update→**(2) QG dynamical core**→(3) moisture tracer advection→(4) moisture diffusion→(5) solar forcing→(6) moisture/condensation/precipitation. **QG core** (ADR-0014, replaces old pressure+PGF+Coriolis steps): (2a) compute background thermal wind shear `U_s(y)` from `dT_base/dy`; (2b) stochastic baroclinic eddy parameterization — smooth multi-scale noise (4-cell primary + 12-cell secondary + 5-cell fine) modulated by local baroclinicity (`|U_shear|`) and latitude envelope (sin²), upper/lower layers opposite-sign (baroclinic tilt), evolved by persistence `EDDY_PERSIST=0.95` with slow eastward drift `EDDY_DRIFT_SPEED=0.3` — replaces semi-Lagrangian PV advection which was killed by numerical diffusion on a 64×32 grid; (2c) Ekman drag on lower-layer PV (`QG_EKMAN_R=0.02`, terrain roughness enhancement); (2e) SOR PV inversion for streamfunctions (`QG_SOR_ITERS=200`, `QG_SOR_OMEGA=1.6`); (2f) wind diagnosis — upper-layer winds from `psi1` gradients; **surface winds from PGF model** (p_anom pressure gradient + Coriolis + cross-isobar friction, K_PGF=0.6, K_CROSS=0.25) blended 90% with streamfunction winds; (2g) `T_anom += QG_C_T*(psi1−psi2)` thermal wind coupling. QG constants: `QG_BETA=0.873`, `QG_Ld=5.0`, `QG_invLd2=0.04`, `QG_SHEAR_GAIN=3.0`, `QG_C_T=2.0`, `QG_EKMAN_R=0.02`. Eddy constants: `EDDY_AMPLITUDE=4.0`, `EDDY_PERSIST=0.95`, `EDDY_DRIFT_SPEED=0.3`, `EDDY_CELL_SIZE=4`. Removed constants: `K_PRESSURE`, `K_DAMP`, `K_CONVERGENCE`, `K_PRESSURE_DIFFUSE`, `K_HEAT_DIFFUSE`, `K_UPPER_DAMP`, `QG_NU4` (del-4 hyperviscosity, removed with PV advection). Key physics: solar forcing (K_SOLAR=8.0), `T_ref = -15 + solar_seasonal * 48`, T prognostics discipline (all fast physics writes `T_anom` only; `cell.T = T_base + T_anom` recomputed at start of step 6), ADVECT_SPEED=4.0, ocean evaporation (OCEAN_EVAP_RATE=0.10, reduced by cloud shading: `1.0 - cloud*0.8` clamped [0.2,1.0]), land evaporation (LAND_EVAP_RATE=0.04, quadratic soil moisture dependence: `wet*wet*RATE` for desert shutoff), latent heat (K_LATENT=8.0). Precipitation: CLOUD_PRECIP_THRESHOLD=0.06, CLOUD_PRECIP_RATE=1.5. **Cloud dissipation**: dual pathways — evaporative (returns to vapor, capped by q_sat) + radiative (`RADIATIVE_DISSIP_FRAC=0.7` of total, destroys cloud without adding moisture). CLOUD_EVAP_BASE=0.30, CLOUD_EVAP_DRY=0.50, CLOUD_SUBSIDE_RATE=0.80. Upper layer: K_RAD_UPPER=0.025, EXPECTED_LAPSE=-18, K_VERT_HEAT=0.02. Budget: BUDGET_OCEAN_RECHARGE=0.08, BUDGET_RAIN_COST=3.0. Ten physical invariants enforced each tick:
    - Stefan-Boltzmann radiative cooling (T^4 law, lower layer: K_RAD=0.09; upper layer: K_RAD_UPPER=0.025)
    - Water mass conservation: total atmospheric water (q + cloud) monitored; advection drift > 0.1% triggers a gentle proportional correction
    - Clausius-Clapeyron hard clamp: q clamped to q_sat after advection/diffusion (catches supersaturation from numerical transport)
    - Cloud evaporation limited: cloud dissipation cannot push q above q_sat
    - Wind speed hard cap: MAX_WIND_SPEED = 6 cells/day prevents numerical blowup from QG-derived winds
    - Moisture diffusion: explicit Laplacian diffusion on q_anom field (K_MOIST_DIFFUSE=0.03)
    - Convective instability release: when ground-air temp diff exceeds threshold, forced condensation + enhanced precipitation dumps latent energy and moisture
    - Evaporation limits: land evaporation hard-capped by available soil moisture
    - CFL/Courant limiter: moisture advection trace distance clamped to ≤ 1.5 cells (PV is no longer advected — stochastic generation replaces semi-Lagrangian transport)
    - Surface water overflow (dynamics): excess surface_water above smooth cap infiltrates soil instead of being discarded
    - `AtmosphereState` tracks `total_water`, `last_water_correction`, `initial_energy`, `total_energy` for conservation bookkeeping
  - **Dynamic simulation** (`dynamics.h/cpp`): per-tick terrain interaction reading from atmosphere when available:
    - Precipitation → surface_water (rain) or snow_depth (below freezing); tracked in `WaterBudget`
    - Snowmelt → surface_water when temperature > 0°C; tracked in `WaterBudget`
    - Infiltration: surface → soil_moisture using geology-based `ksat` (normalized by `soil_depth`) and `field_capacity`; slope penalty `pow(slope01, 0.7)` (superlinear, not linear cap); deep drainage to `groundwater` (capped at `soil_depth * porosity`); groundwater baseflow discharges to surface_water
    - Evaporation: EVAP_SURFACE_RATE=0.01, EVAP_SOIL_RATE=0.007; **AET capped to PET** (actual evaporation cannot exceed atmospheric demand per tick); soil evaporation blocked below wilting_point
    - Runoff: WSE multi-neighbor flow with minimum gradient 0.005 (enables micro-pooling); ocean tiles excluded (tracked as `total_coastal_drain`); CFL cap (`effective_k = min(K_OUT * dt_days, 0.5)`); slope-dependent Horton fraction (+0-15% on steep terrain); headwater minimum for elevation > 0.55; flash runoff from surface cap overflow routed to D8 downstream neighbor (not to ocean; only falls back to ocean if no downstream exists); accumulates in pre-allocated `DynamicState::runoff_buf`
    - **Basin spillway storage** (step 5b-ii): init-time BFS identifies D8 sink basins; per-basin stores sink elevation, spill elevation, area, volume, spill tile; micro-relief noise (8-tile + 3-tile wavelengths, slope-scaled amplitude) deepens basins; basins with depression_depth >= 0.001 aggregate surface_water and spill only when WSE >= spill elevation — produces persistent lakes
    - **D8 flow accumulation** (step 5c): split into two reservoirs — `accum_quick` (surface runoff, coastal drain, overflow; EMA tau=0.3 days) and `accum_base` (groundwater baseflow; EMA tau=10 days); propagated separately along `downhill`; SINK_TOLERANCE=0.001 (was 0.002); `DynamicTile::discharge` = EMA-smoothed quickflow + baseflow_d
    - Surface water cap: smooth `1.0 + max(0, 1 − slope/0.1)` (replaces step function); overflow transfers to soil
    - EMA aridity smoothing: 70/30 weight (was 80/20)
    - `DynamicTile` fields: `surface_water`, `soil_moisture`, `snow_depth`, `local_humidity`, `effective_precip/moisture/evap/storm`, `groundwater`, `discharge`, `quickflow`, `baseflow_d`, `pet`, `aridity`
    - `DynamicState` carries: `runoff_buf`, `downhill`, `topo_order`, `accum_discharge`, `accum_quick`, `accum_base`, `basin_id`, `basin_spill_elev`, `basin_sink_elev`, `basin_area`, `basin_volume`, `basin_spill_tile`, `num_basins`, `budget` (`WaterBudget`)
    - `WaterBudget` fields: `total_precip`, `total_evap_surface`, `total_evap_soil`, `total_ocean_drain`, `total_coastal_drain`, `total_overflow`, `total_gw_recharge`, `total_snowmelt`, `total_headwater`, `total_accum_raw`, `initial_storage`
    - `DynamicStats` tracks `groundwater_mean/max`, `discharge_mean/max`, `aridity_mean/min/max`, `pet_mean`, `arid_tiles`, `humid_tiles`
    - `print_hydrology_diagnostics()`: end-of-run report (water budget, storage percentiles, discharge network, log-histogram, lake components, discharge by elevation/aridity band, D8 sink count); called from `main.cpp` at end of headless runs
  - **Telemetry**: `AtmosphereStats` struct: T/q/cloud/precip/wind/pressure/storm min/max/mean + stddev + extreme event counts; raining_cells/cloudy_cells; invariant diagnostics (total_atmo_water, max_courant, water_correction); latitude-band arrays T_band[3]/wind_band[3]/q_band[3]; moisture budget stats (budget_min/max/mean); anomaly stats (T_anom_min/max/stddev, q_anom_stddev); upper layer stats (T_upper_mean, wind_upper_mean/max, stability_min/max/mean, inversion_cells); energy budget (total_energy, solar_in, radiative_out, latent_release, energy_drift_pct). `DynamicStats` also includes aridity stats row and groundwater/discharge stats row in the live legend panel.
  - **Overlays** (keys 1-9, 0, -, =, `, F1, F2, F3, F4, F5, F6, F7): None, Temperature, Precipitation, Moisture, Wind, Evaporation, Storminess, MoistureBars, RainShadow, SurfaceWater, SoilMoisture, SnowDepth, PrecipBudget (backtick), UpperWind (F1), Stability (F2), Aridity (F3 — red desert → yellow semi-arid → green sub-humid → blue humid; legend shows ARID/HUMID direction labels), Groundwater (F4 — tan/cyan/blue subsurface reservoir), Discharge (F5 — log-scaled emergent river channels, transparent for no-flow; legend shows "SQRT SCALE" note), Geology (F6 — categorical RockType swatches), SoilTextureOverlay (F7 — categorical SoilTexture swatches). Overlay NONE state shows a hint chip with key bindings for all 20 modes.
  - **Color ramps** (`renderer.cpp`): Temperature now blue→white→red (perceptually uniform); Precipitation now blue-only; Discharge now teal→white; Stability now diverging blue→white→red with gamma correction; Snow now blue-tinted white.
  - **Wind arrows**: rendered in neutral gray; density reduced at low zoom (fewer arrows drawn when camera is zoomed far out).
  - **Clipmap terrain rendering** (`renderer.h/cpp`, `tile_texture.h/cpp`): two-level procedural texture cache replaces per-tile flat-color rectangles. L0 macro (whole world, 4 px/tile, baked once via `bake_terrain_cache()`) and L1 meso (camera region, 16 px/tile, regenerated on camera pan via `update_meso_cache()`). `TerrainCacheLevel` struct stores the SDL texture, dimensions, world-space origin, pixels-per-tile, seed, and valid flag. At zoom ≤ ~1 tile/6 screen pixels the macro level is used; at higher zoom the meso level is blitted. When no cache is available a flat-color per-tile fallback draws. Dynamic wet/snow/standing-water tinting is composited on top as semi-transparent SDL rectangles. `bake_terrain_cache()` must be called once after terrain generation and again after any preset change.
  - **Procedural terrain texture system** (`tile_texture.h/cpp`): `eval_terrain_pixel(terrain, world_x, world_y, pixels_per_tile, seed, water_level)` evaluates a single RGBA32 pixel at arbitrary world-space coordinates using LOD-gated noise (`TerrainLOD`: Macro/Meso/Micro). `render_terrain_region()` fills a caller-owned pixel buffer for a rectangular world patch. Coloring layers: hypsometric tint from elevation band, geology-based rock color ramp (RockType drives hue/value), roughness/slope-based material texture, multi-directional hillshade, coastal transition gradients, scree on steep slopes. High-freq noise octaves are gated by Nyquist (only enabled when the octave wavelength exceeds 2-3 pixels at current zoom).
  - Mountain triangle and hill bump glyphs removed — procedural texture provides visual terrain detail at all zoom levels. The `dim_glyphs` parameter has been removed from `render_terrain()`.
  - **Telemetry panel**: drop shadow/border added around the legend panel; section headers use desaturated gold; separator spacing widened for readability.
  - **UI component library** (`ui.h`, `sandbox::ui` namespace): CSS-like layout primitives used by `main.cpp` (hint box, tooltip dispatch) and `telemetry.cpp` (both legend panels). `Theme` — centralized SDL_Color palette (singleton). `Panel` — box-model struct with `draw_bg()` (fill + border + shadow) and `push_clip()`/`pop_clip()` for `overflow:hidden`; panel backgrounds are fully opaque (alpha 255) to eliminate stale-pixel artifacts. `ColumnLayout` — flexbox-column cursor with `text()`, `separator()`, `spacer()`. `anchor_rect()` — positions a box by `TopLeft/TopRight/BottomLeft/BottomRight` within a parent area. Spacing tokens `S0`–`S5` (multiples of 4 px), safe-area insets `SAFE_L/T/R/B` (8 px). `auto_width()` and `row_height()` helpers. `render_tooltip(renderer, mouse_x, mouse_y, text, win_w, win_h)` — draws a floating info box near the cursor; `text` is split on `|` delimiters into up to 8 lines; box repositions to stay on-screen near edges; border uses accent gold at 70% opacity. The right-hand dynamics/metrics panel uses a fixed pixel width (not shrink-wrap) to prevent layout jitter as values change each tick. No texture allocation — all drawing uses SDL rect/line primitives.
  - **Overlay info panel** (`telemetry.cpp`): when an overlay is active, a secondary info box appears immediately to the right of the color legend panel, bottom-aligned with it. The box renders 4–6 lines of plain-English text explaining what causes changes in that overlay metric and how it interacts with other simulation systems. Implemented as `OverlayInfo get_overlay_info(OverlayMode)` returning an `OverlayInfo` struct (`const char* lines[8]`, `int count`). All 19 active overlay modes have descriptions (Temperature, Precipitation, Moisture, Wind, Evaporation, Storminess, MoistureBars, RainShadow, SurfaceWater, SoilMoisture, SnowDepth, PrecipBudget, UpperWind, Stability, Aridity, Groundwater, Discharge, Geology, SoilTexture). Panel width is fixed at the widest text across all overlays (`INFO_PANEL_TEXT_W`, computed once at startup via a static initializer) to prevent layout shift when cycling overlays.
  - **Controls**: T=play/pause dynamics, R=reset dynamics, `,`/`.`=halve/double speed (replaces Numpad +/-), V=wind arrows, G=grid, N/P=preset cycle, Space=rebake weather; **Overlay Cycle button** advances through all 20 overlays in order; camera fits world on startup and clamps to world bounds every frame; controls hint panel at Y=40 with labelled key groups; rain shadow count in live stats and screenshots mode computed dynamically (not from stale bake data)
  - **Headless mode**: `sandbox_weather --headless N [preset]` runs N sim-days without a window; preset arg accepts name string (e.g., "Pangaea") or numeric index. Prints per-2-day diagnostics (latitude-band temperatures, wind by band, q/cloud/precip means, raining cell count, cloudy cell count, total atmospheric water, aridity mean/min/max). Final summary shows cumulative peak extremes including peak aridity and PET. Prints warnings if temperature spread collapses, latitude bands equalize, or wind dies. After the atmosphere summary, `print_hydrology_diagnostics()` prints the hydrology report (water budget, storage percentiles, discharge network, lake components, D8 sinks).
  - **Screenshots mode**: `sandbox_weather --screenshots [preset] [days]` simulates for the given number of days, then uses an SDL software renderer (offscreen, no window) to capture 19 screenshots covering 16 overlay modes at default zoom, 2 zoomed-in views (temperature and discharge at 2.5×), and 1 wide-angle view (temperature at 0.5×). Files saved as `weather_ui_{preset}_{suffix}.bmp`. Full UI is rendered into each frame (terrain, overlay, wind arrows where applicable, legend panel, buttons, controls hint). Designed for use with the `gpt-ui-review` skill.
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

## UX Performance Review (MANDATORY)

After completing any change that could affect user-facing performance, **invoke the `perf-ux-reviewer` agent** to audit the changed code before moving on. This includes:

- Rendering changes (overlays, sprites, camera, SDL2 draw calls)
- Simulation loop modifications (atmosphere tick, dynamics tick, vegetation tick)
- Data structure changes in hot paths (per-tile, per-entity, per-frame code)
- New overlays or UI elements
- Grid traversal or algorithmic changes that run per-tick
- Procedural generation changes that affect load times

**Process:**
1. Complete your code changes and verify correctness.
2. Launch the `perf-ux-reviewer` agent, pointing it at the modified files.
3. Address findings using the same priority rules as Critical Change Review: critical and significant findings fixed immediately, minor findings fixed if trivial or flagged to the user.

**When NOT to invoke:** Pure documentation changes, build system tweaks that don't touch runtime code, test-only changes, or trivial constant adjustments (e.g., changing a color value).

## Commit and Push After Major Changes (MANDATORY)

After completing every major change — new feature, significant refactor, new system, or multi-file modification — **you MUST commit and push to the remote repository**. This happens AFTER running the mandatory review agents (critical-change-reviewer, perf-ux-reviewer if applicable, and docs-maintainer) and addressing their findings.

**Process:**
1. Complete the code change and verify it works (build, run, analyze output).
2. Run critical-change-reviewer → fix all findings.
3. Run perf-ux-reviewer (if the change touches runtime code) → fix findings.
4. Run docs-maintainer → update documentation.
5. Stage all relevant files, commit with a descriptive message, and push to remote.

**What counts as "major":** Any change that modifies more than ~3 files, adds a new system or feature, refactors existing architecture, or would be painful to reconstruct if lost. When in doubt, commit — small frequent commits are better than rare large ones.

**Do not accumulate uncommitted work across multiple major changes.** Each logical unit of work should be its own commit. If a session involves multiple features, commit after each one.

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

**No artificial equilibrium forcing:** Never add ad hoc correction terms, nudgers, or thermostats to force a system into equilibrium. If a quantity drifts (energy, water mass, population), diagnose and fix the **root cause** — a missing sink, an unbalanced source, a bookkeeping mismatch, or a misaligned variable. Correction terms mask real bugs, create artifacts, and prevent the system from reaching natural equilibrium. If a quantity is not expected to be conserved (e.g., atmospheric energy when ground is an external reservoir), track it as a diagnostic only — don't "correct" it.

## Reality Check (MANDATORY)

When modifying or creating **any** system that produces emergent or procedural output — weather, terrain, creatures, sprites, vegetation, evolution, neural networks — **you MUST follow the `reality-check` skill** (`.claude/skills/reality-check/SKILL.md`). This applies to ALL procedural/simulation systems, not just weather.

The process:
1. **Define** what makes the output look real (observable characteristics, not code metrics)
2. **Design** metrics that can distinguish real from fake (not aggregate stats — spatial structure, temporal evolution, distributions)
3. **Implement** those metrics as telemetry/diagnostics/rendered images
4. **Verify** each characteristic passes before declaring done
5. **Iterate** — fix failures, re-verify, check for regressions

**Aggregate statistics (mean, stddev, min/max) are NOT sufficient.** They hide the spatial and temporal structure that makes output look real or fake. Always verify with metrics that would actually fail if the output were unrealistic.

## Sandbox UI Changes

When making UI changes to any SDL2 sandbox (overlays, legends, panels, buttons, camera):

1. **Follow the `sandbox-ui` skill** (`.claude/skills/sandbox-ui/SKILL.md`) — layout constants, spacing rules, color conventions, performance thresholds.
2. **When the user corrects a UI mistake**, update the "Lessons Learned" section at the bottom of the skill file with the date and what went wrong. This is how the skill improves over time.
3. **For GPT-assisted UI review**, use the `gpt-ui-review` skill (`.claude/skills/gpt-ui-review/SKILL.md`): describe the UI target feel, capture screenshots (manually or via `--screenshots` mode), send to GPT with `gpt_ui_review.py`, implement ALL suggestions, verify at 7+/10. Iterate until done.

## Procedural Sprite Generation

When modifying any procedural sprite generation code (e.g., `sandboxes/veggen/plant_sprite.cpp`):

1. **Follow the `realistic-sprite-gen` skill** (`.claude/skills/realistic-sprite-gen/SKILL.md`) — structure first, then layers, environment drives traits, enforce constraints.
2. **Always use the visual review loop** (`.claude/skills/sprite-visual-review/SKILL.md`) — build, render, view PNG, assess, fix, repeat. Never commit sprite generation changes without visually verifying the output.
3. **Physical consistency**: health effects (stressed/brown/dead) are post-processing on the same skeleton. An organism that loses surface detail must show the same underlying structure. Never regenerate structure per health state.

## Dependencies (via FetchContent)

EnTT v3.13.2, FastNoiseLite v1.1.1, SDL2 (system or v2.30.10 fallback), Catch2 v3.7.1, Google Benchmark v1.9.1.
