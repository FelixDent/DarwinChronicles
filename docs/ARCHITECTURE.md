# Architecture

## Module Graph

Six static libraries with an acyclic dependency graph. Each is `darwin_<module>` with a `darwin::<module>` CMake alias.

```
environment       ← planetary physics, star/orbit/atmosphere, climate computation
    ↑
   world          ← procedural terrain (FastNoiseLite), 15 biome types, nutrient fields
    ↑
organisms         ← genome (152 floats), neural brain, metabolism, sensors
    ↑
evolution         ← reproduction, Gaussian mutation, natural selection
    ↑
simulation        ← tick loop, system scheduling, time control
    ↑
rendering         ← SDL2 sprite gen, camera, debug viz, UI
```

`environment` and `organisms` are independent leaf modules. `world` depends on `environment`. `evolution` depends on `organisms`. `simulation` depends on all four above. `rendering` depends on `simulation` + `world` + `organisms`.

## Three-Layer Cascade

The core design principle: physics drives biology, not the other way around.

1. **Planetary physics** — Star luminosity, orbital distance, atmosphere composition → equilibrium temperature, UV flux, habitability zone.
2. **Climate constraints** — Temperature ranges, moisture distribution, toxicity fields → biome classification, nutrient availability, movement costs.
3. **Evolutionary pressure** — Energy scarcity, metabolic costs, mutation rates → organism adaptation, population dynamics, speciation.

Changing a single parameter (e.g., atmospheric CO₂) cascades through all three layers.

## Entity Component System

All organism state lives in EnTT components, not in class hierarchies.

**Core components**: Genome, Brain, Energy, GridPosition, BodySize, Speed, MetabolismRate, ToxinResistance.

**System execution order** (per tick):
1. **Environment** — Regenerate nutrients across the grid
2. **Sense** — Sample local gradients (nutrients, toxins, temperature)
3. **Think** — Forward pass through genome-encoded neural network
4. **Act** — Apply motor outputs as movement
5. **Metabolize** — Deduct energy costs (basal + brain + movement)
6. **Learn** — Hebbian weight updates within lifetime
7. **Reproduce** — Clone + mutate if energy exceeds threshold
8. **Death** — Remove entities at zero energy

Systems are stateless functions over component views. No system holds mutable state between ticks.

## Energy as Universal Currency

Every action costs energy. This creates genuine tradeoffs without scripted balancing:

| Cost source | Formula | Effect |
|---|---|---|
| Basal metabolism | Kleiber's law: `0.01 × size^0.75` | Larger bodies have higher upkeep |
| Brain | `0.001 × synapse_count` per tick | Smarter organisms burn more |
| Movement | `0.002 × size × speed²` | Kinetic energy cost |
| Reproduction | Proportional to `body_size + brain_synapses` | Complex organisms need more energy to reproduce |
| Toxin damage | `toxicity × (1.0 - resistance)` per tick | Unresistant organisms die in toxic zones |

Organisms that over-invest in any single trait die from energy depletion. This alone produces diverse emergent strategies.

## Genome Structure

152 contiguous floats, divided into:

- **Architecture genes** (indices 0–3): Hidden layer count (0–2), neurons per layer (2–8)
- **Morphology genes** (indices 4–7): Body size, limb count, eye count, color traits
- **Physiology genes** (indices 8–15): Speed, metabolism efficiency, toxin resistance, reproduction threshold
- **Neural weights** (indices 16–151): Connection weights for the brain network

Architecture genes define the brain's topology. Morphology genes map directly to sprite appearance. Physiology genes set metabolic parameters. Neural weights are the actual decision-making machinery.

## World Grid

Flat array of tiles. Each tile stores:
- **Biome type** — One of 15 types (Ocean, Tundra, Boreal Forest, Temperate Forest, Rainforest, Grassland, Savanna, Hot Desert, Cold Desert, Swamp, Volcanic, Toxic Wasteland, etc.)
- **Nutrient level** — Current available food (0.0–1.0 of carrying capacity)
- **Toxicity** — Environmental poison level
- **Elevation** — Height value from noise generation
- **Temperature** — Derived from latitude + elevation + noise
- **Moisture** — BFS from water sources with exponential decay

Biomes are classified from the intersection of temperature, moisture, elevation, and toxicity fields — not hand-placed.

## Procedural Generation Pipeline

Four noise fields → biome classification → tile properties:

1. **Height** — Multi-octave Perlin noise. Sea level at 0.4.
2. **Temperature** — Latitude gradient + elevation lapse rate (-6.5°C/km) + local noise.
3. **Moisture** — BFS flood-fill from ocean tiles, exponential decay inland.
4. **Toxicity** — Sparse localized hotspots via high-threshold noise.

Each field is generated independently, then combined to classify biomes using threshold logic.

## Sandboxes

`sandboxes/<name>/` contains standalone executables for iterating on subsystems in complete isolation from the `darwin::*` module tree. A sandbox links only SDL2, FastNoiseLite, and `darwin_warnings` — no `darwin_*` static libraries. The intent is that a sandbox proves out an algorithm or data structure, after which the code can be promoted into the appropriate `darwin::*` module.

The `add_darwin_sandbox(name)` CMake helper in `sandboxes/CMakeLists.txt` sets up this dependency set uniformly. Each sandbox builds to `build/sandboxes/<name>/sandbox_<name>` and is launched with `make run-sandbox SANDBOX=<name>`.

### sandboxes/worldgen

Interactive terrain generation testbed. Supports two generation modes controlled by `EnvParams::plate_count`:

**Tectonic mode** (`plate_count > 0`, default for all presets): Voronoi-partitioned tectonic plate simulation with continuous fields, thermal and stream-power erosion, and post-erosion geomorphic refinement. 12 stages + step 3b:

1. **Plate generation** — `plate_count` plates (clamped to 4–30) with seeded random centers (jittered 10–90% of map extent), random velocity vectors (angle uniform on [0, 2π], speed 1–4), and continental/oceanic type sampled from `continental_ratio`. Continental plates receive a velocity bias toward the map center (inward drift, simulating supercontinent assembly); oceanic plates receive an outward spreading bias. Each continental plate is assigned a **spine direction** perpendicular to its velocity (`spine_dx`, `spine_dy`) plus a random `spine_offset` (±15 tiles), used in stage 3 to generate interior ridges.
2. **Domain-warped Voronoi** — two-layer domain warp applied to coordinates before Voronoi plate lookup. WARP1: 65-tile amplitude at 0.004 frequency. WARP2: 25-tile amplitude at 0.010 frequency. After warping, each tile finds its closest and second-closest plates. **Boundary distance noise**: moderate FBm perturbation on raw `dist_second − dist_closest` makes Voronoi edges wavy rather than linear. **Boundary normal rotation**: per-tile noise-based angle (up to ~35° from two noise layers) rotates boundary normal/tangent, breaking straight-line plate boundary alignment while preserving structural coherence. Signed convergence field stored per tile.
3. **Continuous tectonic fields** — four overlapping contributions:
   - **Macro (continuous continental field)**: plate type blended with `cont_warp_noise` + `crust_noise` so coastlines emerge organically. Province/craton mask gates noise amplitudes. Continental spine (σ=40) with along-strike fragmentation. Meso-scale ridges (province-gated). Terrain variation and basin depressions. **New in round 5**: **curl noise ridge warp** (seed+26000/26001, ~22-tile displacement) deforms ridge coordinate space for organic curved ridges; **lithology noise** (seed+27000, 0.005 freq) spatially varies ridge frequency by simulated rock type; **piedmont roughness** (seed+28000, 0.025 freq) adds coarse roughness at mountain-to-foothill margins; **multi-direction ridge blending** (40% primary + 25% rotated 30° + 35% isotropic) prevents ridges from all aligning parallel to plate boundaries.
   - **Meso (boundary stress)**: anisotropic ridged noise along boundary tangent with 5:1 stretch; belt segmentation ±40% along-strike. Boundary types: continent-continent (`MOUNTAIN_PEAK=0.75`) + plateau + foothill envelope (σ=90) + foreland basin; ocean-continent coastal arc (`×1.1`) + foothill envelope (σ=80) + back-arc basin + trench (`TRENCH_DEPTH=0.06`); ocean-ocean island arc; rift + shoulders; mid-ocean ridge central uplift; transform scarp. Secondary spur ridges in annular foothill zone.
   - **Micro (detail)**: 6-octave FBm, amplitude × province mask.
   - Edge falloff toward ocean at map borders.
3b. **Three-scale relief domain warp** — three FBm warp layers (12 + 5 + 2 tile displacement) applied to the full height field after stage 3 to eliminate any remaining linear tectonic artifacts at continental, regional, and local scales.
4. **Material-conserving thermal erosion** — 6 iterations with `THERMAL_RATE=0.12` and `MAX_STABLE_SLOPE=0.055` (gentler parameters complement the stream-power erosion in stage 5). Sediment deposited proportionally to lower neighbors. Each iteration followed by 15/85 Laplacian smoothing.
5. **Stream-power erosion** — 40 iterations of D8 flow accumulation + stream-power law carving (`flow × slope`, modulated by rock hardness). **Mountain canyon boost**: high-elevation high-flow tiles receive 2× carving for dramatic gorge formation. **Sediment capacity model**: sediment deposits when stream loses transport power (valley widens or slope decreases), producing realistic alluvial fans and graded longitudinal profiles. More geomorphically realistic than the previous fixed-fraction 6-iteration hydraulic erosion.
5b. **Valley-floor smoothing** — targeted low-pass filter over low-elevation land tiles; smoothing strength scaled by elevation so mountain terrain is unchanged. Only valley floors flatten.
5d. **Valley drowning** — rias, estuaries, and fjords produced by lowering high-flow valley floors near the coast below water level. Uses flow accumulation, sea-level anomaly noise, and fjord detection (high relief + high latitude) to spatially vary the drowning extent.
6. **Distance-from-coast inland elevation bias** — BFS + exponential saturation `0.12 × (1 − exp(−dist/15))`.
6b. **Noise-warped coastal plain enforcement** — nearshore band (1.5–7 tile width, modulated by FBm warp) lowered to flat coastal lowlands; steep coasts skipped.
7. **Exposure-based SDF coastal remodeling** — replaces previous uniform coastal fractalization. (7a) BFS signed distance field from coastline. (7b) 16-direction fetch raymarching (max 60 tiles) for wave exposure per coastal tile. (7c) Coastal slope proxy + 3-pass Gaussian exposure blur. (7d) Exposure-driven anisotropic displacement: sheltered coasts suppress high-frequency 90%, mid-frequency 70%, broad-frequency 30%; exposed coasts apply full amplitude. Gated coastal profile ramp (1–3 tile width, 50% blend) transitions coastal elevation to inland terrain; cliff geometry detected and respected.
7b. **Depositional coastal features** — river-mouth deltas (fan lobes + distributary channels) on high-flow coasts; barrier islands (offshore ridges with tidal inlet gaps) on moderate-exposure coasts.
7c. **Clustered skerry generation** — exposed headlands seed Poisson-distributed cluster centers (8-tile minimum spacing), each spawning a field of small rocky islets within a 3–6 tile radius.
7d. **Sub-bay inlet carving** — in sheltered low-exposure concavities, steepest-ascent tracing proceeds up to 8 tiles inland to carve narrow inlets or estuary recesses.
8. **Basin flood-fill lakes** — priority-queue basin detection and flood to spill point. Up to 200 basins, min 4 tiles, max depth 0.08.
9. **Water speckle removal** — components < 3 tiles raised back to land.
9b. **Debris islet removal** — land components < 8 tiles converted to water. Removes small fragments introduced by coastal displacement or skerry generation.
9c. **Morphological narrow feature cleanup** — neck removal (1-tile-wide land necks), corner bridge removal (diagonal-only connectivity), and peninsula tip pruning to eliminate degenerate coastal topology.
10. **SDF curvature flow coastline smoothing** — 4 iterations of mean-curvature flow (dt=0.18) restricted to a 3-tile coastal band. Rounds convex corners and fills concave bays slightly, producing anti-aliased organic coastline geometry. Replaces previous Laplacian smoothing.

Key constants: `CONT_BASE = max(0.58, water_level + 0.12)`, `OCEAN_BASE = 0.22`, `MOUNTAIN_WIDTH = 35`, `MOUNTAIN_PEAK = 0.75`, `RIFT_WIDTH = 20`, `PLATEAU_WIDTH = 65`, `TRENCH_DEPTH = 0.06`, `THERMAL_RATE = 0.12`, `STREAM_POWER_ITERS = 40`, `WARP1_STRENGTH = 65`, `WARP2_STRENGTH = 25`, `INLAND_BIAS_STRENGTH = 0.12`, `LAKE_DEPTH_MAX = 0.08`.

**Legacy noise mode** (`plate_count == 0`): three-stage noise pipeline before shared hydrology:

1. **Height field** — domain-warped continental noise (two Perlin layers offset input coordinates by up to 20 tiles, producing organic non-radial landforms)
2. **Ridge field** — ridged multifractal noise for mountain ranges; stored separately as `TerrainTile::elevation` for renderer decoration
3. **Unify height** — ridge field merged into the height field (scale ~0.18) so mountains affect downstream hydrology

Both modes share post-height stages:

4. **Ocean flood-fill** — BFS; edge-connected components ≥ 200 tiles become ocean. Lake classification is no longer performed at terrain-gen time — lakes emerge dynamically in the weather sandbox.
5. **Slope + aspect + elevation bands** — central-difference gradients on unified height; aspect = `atan2(dy, dx)` direction of steepest ascent; bands classified by local relief (max−min in R=4 tile neighborhood) and slope: Mountains = relief > 0.22 AND slope > 0.18; Hills = relief > 0.12 OR (slope > 0.12 AND elevation > water_level + 0.12); Lowland = everything else above water
6. **Euclidean Distance Transform** — Felzenszwalb separable parabola-envelope algorithm for `dist_ocean` only (dist_water removed)
7. **Roughness** — 3×3 neighborhood height standard deviation × 20
8. **Geology** — `compute_geology()` assigns rock type and soil properties per tile based on tectonic context (see Geology Stage below)

Removed from the shared pipeline: soil fertility/hold fields, D8 downhill routing, flow accumulation, `river_flow`, `is_lake`, `dist_water`, `downhill_x/y`. Hydrology is now fully dynamic (see weather sandbox dynamics).

**Geology Stage**: `compute_geology()` assigns `RockType` (Granite/Basalt/Limestone/Sandstone/Shale/Metamorphic) and `SoilTexture` (Sand/Loam/Silt/Clay/Peat) based on tectonic context and elevation: convergent mountain belts → Metamorphic; volcanic arcs → Basalt; stable continental interiors → Granite; sedimentary basins → Sandstone/Limestone; coastal lowlands → Shale with Clay/Silt soils; floodplains → Silt/Loam. Derived hydraulic parameters: `bedrock_hardness`, `bedrock_permeability`, `soil_depth`, `porosity`, `ksat` (saturated hydraulic conductivity, m/s), `field_capacity`, `wilting_point`, `erodibility`. These drive geology-aware infiltration and plant water availability in downstream systems.

`EnvParams` fields: `water_level`, `noise_scale`, `continent_threshold`, `ridge_strength`, `plate_count` (0=legacy, >0=tectonic, default 12), `continental_ratio` (fraction of plates that are continental, default 0.40). `TerrainTile` geometry fields: `elev01`, `band`, `is_ocean`, `slope01`, `aspect`, `dist_ocean`, `roughness`, `elevation` (ridge only). `TerrainTile` geology fields: `rock` (RockType), `bedrock_hardness`, `bedrock_permeability`, `soil` (SoilTexture), `soil_depth`, `porosity`, `ksat`, `field_capacity`, `wilting_point`, `erodibility`.

- Nine overlay modes (keys 1-9): None, Continental, Slope, ElevBand, DistOcean, Roughness, Aspect, Geology, SoilTexture; each overlay legend panel shows a short info description line
- Starts fully zoomed out via `Camera::fit_world` so the full map is visible at launch
- Pan (WASD / arrows), zoom (scroll wheel), grid toggle (G), FPS toggle (F)
- Live parameter tweaking: Tab cycles Seed/WaterLevel/NoiseScale/ContinentThreshold/RidgeStrength; `[`/`]` decrement/increment
- Regenerate (R) or random seed (Space); screenshot (F12) and log export (L)
- 6 built-in presets: earth, venus, mars, snowball, tropical, toxic (each with tuned `plate_count` and `continental_ratio`)
- Headless mode: `--headless` generates terrain, renders a 1-pixel-per-tile BMP (NW directional lighting 315° azimuth / 40° altitude, slope exaggeration 6×), prints generation stats to stdout, and exits. Output: `terrain_{preset}_{seed}.bmp`. BMP rendering uses: gamma-compressed ocean depth (`depth^0.6` before quadratic gradient, no coastal halo); tectonics-aware bathymetry (river shelf boost, margin-dependent shelf width, convergent margin trenches); lake depth gradient (lighter shores, darker centers); climate-driven biome land coloring (7-biome logistic blend from latitude+lapse temperature + composite moisture model); aspect-based snow with pole-facing preference; aspect-based mountain albedo variation; river discharge hierarchy with sediment tint in estuaries.

Source files: `config.h/cpp` (CLI parsing, presets, `EnvParams` with plate fields), `terrain_gen.h/cpp` (generation pipeline including `generate_tectonic_terrain` with curl noise, lithology, piedmont, and stream-power erosion), `renderer.h/cpp` (tile rendering, camera), `telemetry.h/cpp` (overlays, stats, FPS), `main.cpp` (event loop + headless biome coloring). Uses `sandbox` namespace.

### sandboxes/weather

Atmospheric weather simulation layered on top of worldgen terrain. Three-phase architecture:

**Terrain generation** (`terrain_gen.h/cpp`): Identical copy of the worldgen tectonic/legacy dual-mode pipeline (kept in sync with `sandboxes/worldgen/terrain_gen.cpp`). Includes all tectonic improvements through the current 12-stage pipeline: province/craton mask, continuous continental field, wider continental spines with along-strike fragmentation, along-strike mountain belt segmentation, meso-scale ridge noise (province-gated), basin depressions, terrain variation noise (province-gated), foreland/back-arc basins, stronger foothill envelopes, material-conserving thermal erosion (6 iterations), stream-power erosion (40 iterations), valley-floor smoothing (stage 5b), valley drowning (stage 5d), distance-from-coast inland elevation bias, noise-warped coastal plain enforcement (stage 6b), exposure-based SDF coastal remodeling (stage 7), depositional features (stage 7b), clustered skerries (stage 7c), sub-bay inlet carving (stage 7d), water speckle removal (stage 9), debris islet removal (stage 9b), morphological narrow feature cleanup (stage 9c), SDF curvature flow coastline smoothing (stage 10), **geology stage** (`compute_geology()` assigns RockType + SoilTexture + hydraulic parameters per tile). Hydrology (D8 routing, flow accumulation, lakes, soil fertility) removed from terrain-gen; all hydrology is now dynamic (see Dynamic simulation). Weather sandbox `EnvParams` includes `plate_count` and `continental_ratio`. Six weather presets (Archipelago, Pangaea, Island Chain, Continental, Highland, Waterworld) each carry tuned `plate_count` (range 8–18) and `continental_ratio` (range 0.20–0.55). Tectonic terrain produces structurally realistic rain shadows, drainage basins, and coastlines that improve weather realism.

**Static bake** (`weather.h/cpp`, `bake_weather`): latitude-based temperature with elevation lapse rate, 3-cell Hadley/Ferrel/polar wind model, moisture-flux precipitation sweeps (zonal + meridional) with orographic uplift, evaporation demand, storminess (5-factor composite), rain shadow detection, and initial aridity index seeded from `precip / (evap + 0.01) * 0.8` (ratio-based, correcting the previous arbitrary `precip - evap * 0.5` formula).

**Atmosphere simulation** (`atmosphere.h/cpp`, `tick_atmosphere`): coarse-grid (4× terrain tile scale) atmospheric state. `AtmosphereCell` carries: climate baseline fields (`T_base`, `q_base`, `p_base` — slow, seasonally updated), weather anomaly fields (`T_anom`, `q_anom`, `p_anom` — fast, advected, set by QG thermal wind constraint), composite fields (T, q, p, u, v, cloud, precip_rate, storminess, ground_temp), terrain coupling fields (`avg_slope`, `avg_aspect`, `avg_dist_ocean`), moisture budget (`precip_budget`), upper troposphere layer (`T_upper`, `q_upper`, `u_upper`, `v_upper`, `p_upper`, `stability`), and **QG dynamics fields** (`psi1`, `psi2` — upper/lower streamfunctions; `q1_pv`, `q2_pv` — upper/lower potential vorticity). `AtmosphereState` additionally tracks `initial_energy` and `total_energy` for Phase 4 energy conservation. Bilinear interpolation serves terrain-resolution queries via `AtmosphereState::sample()`; nearest-neighbour via `sample_nearest()` for sharp-boundary fields.

Tick order (must not be reordered): (1) terrain coupling — wind·slope uplift, aspect solar heating, maritime moderation, wet-soil evaporation feedback, storm persistence; (1b) seasonal baseline update — T_base/q_base track annual cycle; (2) **QG dynamical core** (ADR-0014) — (2a) background thermal wind shear U_s(y) from dT_base/dy, (2b) stochastic baroclinic eddy parameterization (PV perturbations generated from smooth multi-scale noise modulated by local baroclinicity, evolved by persistence, upper/lower layers opposite-sign for baroclinic tilt), (2c) Ekman drag on lower-layer PV, (2e) SOR PV inversion for psi1/psi2, (2f) wind diagnosis — upper-layer winds from psi1 gradients; surface winds from PGF model (K_PGF=0.6, K_CROSS=0.25) blended 90% with streamfunction winds, (2g) T_anom += QG_C_T*(psi1-psi2) thermal wind coupling; (3) semi-Lagrangian advection of moisture tracers (q_anom, cloud, precip_budget) + water drift correction; (4) moisture diffusion; (5) solar forcing + ground temp; (6) moisture sources + condensation + precipitation. QG replaces the previous pressure-from-T and PGF+Coriolis+damping steps. Del-4 hyperviscosity (previous step 2d) removed — stochastic generation of PV eliminates grid-scale noise without needing explicit dissipation. Temperature forcing (step 5) runs after QG and advection so transported air masses are forced in their new location.

Key physics: solar forcing (K_SOLAR=8.0), temperature reference `T_ref = -15 + solar_seasonal * 48`, T prognostics discipline (all fast physics writes to `T_anom` only; QG thermal wind sets the base anomaly; `cell.T = T_base + T_anom` recomputed at start of step 6), ADVECT_SPEED=4.0 (increased for visible moisture feature movement on coarse grid), ocean evaporation (OCEAN_EVAP_RATE=0.10, reduced by **cloud shading**: `1.0 - cloud*0.8` clamped [0.2,1.0] — breaks evap→condense→rain feedback loop over warm ocean), land evaporation (LAND_EVAP_RATE=0.04, quadratic soil moisture: `wet*wet*RATE`), latent heat (K_LATENT=8.0), storminess multiplicative formula. **QG constants**: QG_BETA=0.873, QG_Ld=5.0, QG_invLd2=0.04, QG_SHEAR_GAIN=3.0, QG_C_T=2.0, QG_EKMAN_R=0.02, QG_SOR_ITERS=200, QG_SOR_OMEGA=1.6. **Stochastic eddy constants**: EDDY_AMPLITUDE=4.0, EDDY_PERSIST=0.95, EDDY_DRIFT_SPEED=0.3, EDDY_CELL_SIZE=4. **PGF surface winds** (step 2f): surface winds computed from p_anom pressure gradient force + Coriolis + cross-isobar friction (K_PGF=0.6, K_CROSS=0.25), blended 90% with streamfunction winds — provides realistic spatial variation that streamfunction finite differences alone cannot produce at 64×32 resolution. **Cloud dissipation**: dual pathways — evaporative (returns to vapor, capped by q_sat) + radiative (`RADIATIVE_DISSIP_FRAC=0.7` of total, destroys cloud without adding moisture). **Precipitation**: CLOUD_PRECIP_THRESHOLD=0.06, CLOUD_PRECIP_RATE=1.5, CLOUD_EVAP_BASE=0.30, CLOUD_EVAP_DRY=0.50, CLOUD_SUBSIDE_RATE=0.80. Phase 3 upper layer: K_RAD_UPPER=0.025, EXPECTED_LAPSE=-18, K_VERT_HEAT=0.02. Budget constants: BUDGET_OCEAN_RECHARGE=0.08, BUDGET_RAIN_COST=3.0. Removed constants: K_PRESSURE, K_DAMP, K_CONVERGENCE, K_PRESSURE_DIFFUSE, K_HEAT_DIFFUSE, K_UPPER_DAMP, QG_NU4 (del-4 hyperviscosity, removed with PV advection). Auto-starts after weather bake.

Ten physical invariants are enforced each tick: Stefan-Boltzmann T^4 radiative cooling on both lower (K_RAD=0.09) and upper (K_RAD_UPPER=0.025) layers; water mass conservation with automatic proportional correction when advection drift exceeds 0.1% of total atmospheric water (q + cloud); Clausius-Clapeyron saturation hard clamp on q after advection/diffusion; cloud evaporation limited to not push q above q_sat; wind speed cap (MAX_WIND_SPEED = 6 cells/day); Laplacian moisture diffusion on q_anom; convective instability release when ground-air temperature difference exceeds threshold; soil-moisture-limited land evaporation; CFL/Courant limiter clamping moisture advection trace to ≤ 1.5 cells (PV is no longer advected); and surface water overflow-to-soil rerouting in the dynamics layer. `AtmosphereState` carries `total_water`, `last_water_correction`, `initial_energy`, `total_energy` for bookkeeping.

`AtmosphereStats` records: T/q/cloud/precip/wind/pressure/storm min+max+mean + stddev; raining_cells, cloudy_cells; invariant diagnostics (`total_atmo_water`, `max_courant`, `water_correction`); latitude-band arrays `T_band[3]`, `wind_band[3]`, `q_band[3]` (north polar / equatorial / south polar thirds); extreme event counts (severe_storm_cells, heavy_rain_cells, heat_wave_cells, deep_freeze_cells, gale_cells); Phase 1 moisture budget stats (budget_min/max/mean); Phase 2 anomaly stats (T_anom_min/max/stddev, q_anom_stddev); Phase 3 upper layer stats (T_upper_mean, wind_upper_mean/max, stability_min/max/mean, inversion_cells); Phase 4 energy budget (total_energy, solar_in, radiative_out, latent_release, energy_drift_pct). `compute_atmosphere_stats()` populates it each tick.

**Dynamic simulation** (`dynamics.h/cpp`, `tick_dynamics`): per-tick terrain interaction, reads T/precip/wind from atmosphere when `atmo` pointer is non-null, falls back to static climate otherwise:
- Precipitation lands as rain (surface_water) or snow (snow_depth) based on temperature; tracked in `WaterBudget`
- Snowmelt → surface_water when temperature > 0°C; tracked in `WaterBudget`
- Infiltration: surface_water → soil_moisture using geology-based `ksat` (normalized by `soil_depth`) and `field_capacity`; slope penalty is superlinear `pow(slope01, 0.7)` (not a linear cap); deep drainage to `groundwater` (capped at `soil_depth * porosity`); groundwater baseflow discharges back to surface_water creating perennial streams
- Evaporation: EVAP_SURFACE_RATE=0.01, EVAP_SOIL_RATE=0.007; AET capped to PET so actual evaporation never exceeds atmospheric demand; soil evaporation blocked below wilting_point
- Runoff: WSE multi-neighbor flow with minimum WSE gradient 0.005 (enables micro-pooling); ocean tiles excluded via `total_coastal_drain` path; CFL cap (`effective_k = min(K_OUT * dt_days, 0.5)`); slope-dependent Horton surface fraction (+0–15% on steep terrain); headwater minimum contribution for tiles with elevation > 0.55; flash runoff from surface-water cap overflow now routes to D8 downstream neighbor (falls back to ocean only if none exists; tracked as `total_overflow` in WaterBudget); accumulates in pre-allocated `DynamicState::runoff_buf`
- **Basin spillway storage** (step 5b-ii, init-time): BFS identifies D8 sink basins; per-basin stores `basin_sink_elev`, `basin_spill_elev`, `basin_area`, `basin_volume`, `basin_spill_tile`; two-octave micro-relief noise deepens basin floors; during tick, basins with `depression_depth >= 0.001` aggregate surface_water and retain it below spill elevation — overflow routes through spill tile; enables persistent lakes without terrain-gen lake classification
- **D8 flow accumulation** (step 5c): split into two EMA-smoothed reservoirs — `accum_quick` (quickflow: surface runoff + coastal drain + overflow; tau=0.3 days) and `accum_base` (baseflow: groundwater discharge; tau=10 days); propagated separately along `downhill`; SINK_TOLERANCE=0.001 (was 0.002); `DynamicTile::discharge` = EMA-smoothed `quickflow` + `baseflow_d`
- Surface water cap: smooth formula `1.0 + max(0, 1 − slope/0.1)` — 2.0 at slope=0, 1.0 at slope≥0.1, continuous in between; overflow transfers to soil
- EMA aridity smoothing weight: 70/30 (previously 80/20) for faster seasonal response
- `DynamicTile` fields: `surface_water`, `soil_moisture`, `snow_depth`, `local_humidity`, `effective_precip/moisture/evap/storm`, `groundwater`, `discharge`, `quickflow`, `baseflow_d`, `pet`, `aridity`
- `DynamicState` carries: `tiles`, `runoff_buf`, `downhill`, `topo_order`, `accum_discharge`, `accum_quick`, `accum_base`, `basin_id`, `basin_spill_elev`, `basin_sink_elev`, `basin_area`, `basin_volume`, `basin_spill_tile`, `num_basins`, `budget` (`WaterBudget`)
- `WaterBudget` fields: `total_precip`, `total_evap_surface`, `total_evap_soil`, `total_ocean_drain`, `total_coastal_drain`, `total_overflow`, `total_gw_recharge`, `total_snowmelt`, `total_headwater`, `total_accum_raw`, `initial_storage` — all `double`
- `DynamicStats` tracks surface_water_mean/max, soil_moisture_mean/max, snow_mean/max, snow_tiles, flooded_tiles, `groundwater_mean/max`, `discharge_mean/max`, `aridity_mean/min/max`, `pet_mean`, `arid_tiles`, `humid_tiles`
- `print_hydrology_diagnostics()` prints end-of-run water budget, storage percentiles, discharge network thresholds, log-histogram, lake connected-component analysis, discharge by elevation band and aridity zone, and D8 sink count (called from `main.cpp` at end of headless runs)

Overlay keys 1-9, 0, -, =, `, F1, F2, F3, F4, F5, F6, F7 for: None, Temperature, Precipitation, Moisture (atmospheric q), WindDirection, Evaporation, Storminess, MoistureBars, RainShadow, SurfaceWater, SoilMoisture, SnowDepth, PrecipBudget (backtick — moisture budget remaining), UpperWind (F1 — upper troposphere wind), Stability (F2 — temperature lapse between layers), Aridity (F3 — red desert → yellow semi-arid → green sub-humid → blue humid; legend shows ARID/HUMID direction labels), Groundwater (F4 — tan/cyan/blue subsurface reservoir depth), Discharge (F5 — log-scaled emergent river channels, transparent for no-flow; legend shows "SQRT SCALE" note), Geology (F6 — categorical RockType swatches), SoilTextureOverlay (F7 — categorical SoilTexture swatches). Overlay NONE state shows a hint chip with key bindings for all 20 modes. UI: Overlay Cycle button (clicks through all 20 overlays in order), Prev/Next/Rebake buttons; controls hint is three lines (T/R/speed, V/G/F/L/F12, N/P/overlay keys). Controls: T=play/pause dynamics, R=reset dynamics, `,`/`.`=halve/double speed (replaces Numpad +/-), V=wind arrows, G=grid, N/P=preset cycle, Space=rebake weather. Camera fits the full world on startup (`fit_world()`) and clamps to map bounds every frame (`clamp_to_world()`). Controls hint panel aligned at Y=40 with clearly labelled key groups. Rain shadow count in screenshots mode and live stats computed from dynamic state, not stale bake data. Headless preset arg accepts name strings ("Pangaea") or numeric index. Legend panel shows live atmosphere + dynamics stats including invariant diagnostics, aridity stats row, and groundwater/discharge stats row; console diagnostics dump every 2 sim-days with latitude-band temperature and wind breakdown.

**Color ramps** (`renderer.cpp`): Temperature uses blue→white→red (perceptually uniform); Precipitation uses blue-only; Discharge uses teal→white; Stability uses diverging blue→white→red with gamma correction; Snow uses blue-tinted white. Wind arrows rendered in neutral gray with density reduction at low zoom. Terrain glyphs alpha-blended at 40–70 alpha when any overlay is active (`dim_glyphs` path). Telemetry panel has drop shadow/border, desaturated gold section headers, wider separator spacing.

Headless mode: `sandbox_weather --headless N` runs N sim-days without a window, printing per-2-day rows with latitude-band temperatures (north/equatorial/south), global T min/mean/max, wind by band, q/cloud/precip means, raining cell count, cloudy cell count, total atmospheric water, and aridity mean/min/max. The final summary section shows cumulative peak extremes tracked across the entire run: peak T range, peak heat wave / deep freeze / severe storm / gale / heavy rain cell counts, peak precipitation intensity per cell, and peak aridity / PET. Prints warnings if T spread collapses below 5°C, latitude bands equalize below 3°C spread, or wind dies below 0.5 cells/day. After the summary, **four reality-check (RC) metrics** verify spatial/temporal structure: RC1 (wind direction temporal autocorrelation at 6 sample cells, lag 1/3/7/14 days), RC2 (adjacent-cell wind angle difference distribution), RC3 (precipitation intermittency — rain fraction per cell), RC4 (T_anom zonal autocorrelation at lag 1–32 cells). Followed by full hydrology diagnostics from `print_hydrology_diagnostics()`.

Screenshots mode: `sandbox_weather --screenshots [preset] [days]` simulates for the requested days then uses an SDL software renderer (offscreen, no window) to capture 19 screenshots: 16 overlay modes at default zoom, 2 zoomed-in views (temperature + discharge at 2.5×), 1 wide-angle view (temperature at 0.5×). Output: `weather_ui_{preset}_{suffix}.bmp`. Full UI rendered in each frame. Designed for use with the `gpt-ui-review` skill (`.claude/skills/gpt-ui-review/`).

Source files: `terrain_gen.h/cpp` (shared terrain — synced with worldgen, includes tectonic mode and `EnvParams` with `plate_count`/`continental_ratio`), `weather.h/cpp` (static bake), `atmosphere.h/cpp` (atmosphere simulation), `dynamics.h/cpp` (terrain-level tick), `renderer.h/cpp` (tile rendering + overlay dispatch), `telemetry.h/cpp` (FPS, legend, `render_dynamic_legend` takes `AtmosphereStats`), `ui.h` (UI component library — see below), `main.cpp` (event loop with `TerrainPreset` table using `plate_count`/`continental_ratio`). Uses `sandbox` namespace.

**UI component library** (`ui.h`, `sandbox::ui` namespace): CSS-like layout primitives used by `main.cpp` (hint box, tooltip dispatch) and `telemetry.cpp` (both legend panels). `Theme` — centralized `SDL_Color` palette (panel_bg, panel_border, panel_shadow, text, text_muted, text_bright, accent/gold, info/blue, danger/red, ok/green); singleton via `Theme::instance()`. `Panel` — box model struct: `draw_bg()` fills background (fully opaque, alpha 255 — eliminates stale-pixel artifacts), draws 1-px border and 2-px drop shadow; `push_clip()`/`pop_clip()` set/restore SDL clip rect for `overflow:hidden`. `ColumnLayout` — flexbox-column cursor with `text()`, `separator()`, `spacer()` methods. `anchor_rect()` — positions a box by `TopLeft/TopRight/BottomLeft/BottomRight` anchor within a parent area. Spacing tokens `S0`–`S5` (multiples of 4 px) and safe-area insets `SAFE_L/T/R/B` (8 px each). `auto_width()` shrink-wraps panel width to content; `row_height()` returns glyph height + gap. `render_tooltip(renderer, mouse_x, mouse_y, text, win_w, win_h)` — floating tooltip near the cursor: `text` split on `|` into up to 8 lines, box repositioned to stay on-screen at edges, border in accent gold at 70% opacity. The right-hand dynamics/metrics panel uses a fixed pixel width (not `auto_width`) to prevent layout jitter as values change each tick. All drawing uses `SDL_RenderFillRect`/`SDL_RenderDrawRect`/`SDL_RenderDrawLine` — no texture allocation.

### sandboxes/veggen

Vegetation simulation across a 3×3 tile grid with 8 biome presets (namespace `veggen`). Each tile has a full terrain model (elevation, slope, soil fertility/hold, downhill routing) and per-tick hydrology.

**Two-layer moisture model**: `surface_water` fills from precipitation, drains via infiltration into `root_moisture` and runoff to downhill neighbors; evaporation draws from surface then root and is reduced by canopy cover (up to 40% suppression at full canopy).

**Plant lifecycle**: archetype-specific traits (water need, drought tolerance, temperature range, fertility need, cluster affinity), density-dependent growth (15% penalty per neighbor), mature-plant reproduction, five death causes (Drought, Cold, Heat, Competition, Senescence), continuous stress recovery, senescence with species-specific lifespans.

Overlay keys 1-7: None, RootMoisture, Temperature, Health, Elevation, SurfaceWater, CanopyCover. UI panel shows grid-wide averages for all key environmental fields plus live plant count.

CLI modes (headless): `--auto [seed]` renders seasonal BMP snapshots for all biomes; `--metrics [seed] [years]` runs N-year simulations and exports per-biome CSV with population, mortality, and environment columns. Metrics snapshots every 30 sim-days.

Source files: `vegetation.h/cpp` (SimState, TileState, hydrology, plant update), `plant_sprite.h/cpp` (procedural sprite rendering), `renderer.h/cpp` (tile rendering, overlays, UI), `telemetry.h/cpp`, `metrics.h/cpp` (MetricsCollector, CSV export), `main.cpp`.

## File Conventions

- Public headers: `include/darwin/<module>/<file>.h`
- Implementation: `src/<module>/<file>.cpp`
- Module build: `src/<module>/CMakeLists.txt`
- Module docs: `src/<module>/README.md`
- System deep-dives: `docs/systems/<SYSTEM>.md` — see `TERRAIN_GENERATION.md`, `WEATHER_SYSTEM.md`, `PLANT_GENERATION.md`
- Decision records: `docs/decisions/<NNNN>-<slug>.md`
- Sandbox sources: `sandboxes/<name>/*.{h,cpp}` (headers are local; no `include/` layer)
