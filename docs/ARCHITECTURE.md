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

Interactive terrain generation testbed. Eleven-stage pipeline produces a tile map with full hydrology-aware terrain:

1. **Height field** — domain-warped continental noise (two Perlin layers offset input coordinates by up to 20 tiles, producing organic non-radial landforms instead of blobs)
2. **Ridge field** — ridged multifractal noise for mountain ranges; stored separately as `TerrainTile::elevation` for renderer decoration
3. **Unify height** — ridge field merged into the height field (scale ~0.18) so mountains affect all downstream hydrology, slope, and band classification
4. **Ocean/lake flood-fill** — BFS; edge-connected components ≥ 200 tiles become ocean, remainder become lakes
5. **Slope + aspect + elevation bands** — central-difference gradients on unified height; aspect = `atan2(dy, dx)` direction of steepest ascent; bands (Lowland/Hills/Mountains) from height offsets above water level
6. **Euclidean Distance Transform** — Felzenszwalb separable parabola-envelope algorithm, two passes: `dist_ocean` and `dist_water` (replacing previous BFS Manhattan distances)
7. **Soil fields** — fertility (noise + water proximity − slope) and hold (material noise + lowland bonus − steepness)
8. **Roughness** — 3×3 neighborhood height standard deviation × 20 (replaces previous ridged-noise + slope blend)
9. **Downhill routing** — 8-directional steepest descent (D8); lake outflow spill points: each lake component finds its lowest adjacent land tile and BFS-routes lake tiles toward it
10. **Flow accumulation** — height-sorted downhill walk accumulates per-tile flow counts; `river_flow` stored as log₂-scaled value

`TerrainTile` fields: `elev01`, `band`, `is_ocean/lake`, `slope01`, `aspect`, `dist_ocean/water`, `river_flow`, `soil_fertility/hold`, `roughness`, `elevation` (ridge only), `downhill_x/y`.

- Eleven overlay modes (keys 1-9, 0, -): None, Continental, Slope, ElevBand, DistOcean, DistWater, SoilFertility, SoilHold, Roughness, Aspect, RiverFlow; each overlay legend panel shows a short info description line
- Starts fully zoomed out via `Camera::fit_world` so the full map is visible at launch
- Pan (WASD / arrows), zoom (scroll wheel), grid toggle (G), FPS toggle (F)
- Live parameter tweaking: Tab cycles Seed/WaterLevel/NoiseScale/ContinentThreshold/RidgeStrength; `[`/`]` decrement/increment
- Regenerate (R) or random seed (Space); screenshot (F12) and log export (L)
- 6 built-in presets: earth, venus, mars, snowball, tropical, toxic

Source files: `config.h/cpp` (CLI parsing, presets), `terrain_gen.h/cpp` (generation pipeline), `renderer.h/cpp` (tile rendering, camera), `telemetry.h/cpp` (overlays, stats, FPS), `main.cpp` (event loop). Uses `sandbox` namespace.

### sandboxes/weather

Atmospheric weather simulation layered on top of worldgen terrain. Three-phase architecture:

**Static bake** (`weather.h/cpp`, `bake_weather`): latitude-based temperature with elevation lapse rate, 3-cell Hadley/Ferrel/polar wind model, moisture-flux precipitation sweeps (zonal + meridional) with orographic uplift, evaporation demand, storminess (5-factor composite), and rain shadow detection.

**Atmosphere simulation** (`atmosphere.h/cpp`, `tick_atmosphere`): coarse-grid (4× terrain tile scale) atmospheric state per cell — air temperature T, specific humidity q, pressure p, wind (u, v), cloud, precip_rate, storminess, ground coupling (ground_temp), and precomputed terrain averages (avg_elevation, avg_roughness, avg_snow, avg_soil_wet, is_water). Bilinear interpolation serves terrain-resolution queries via `AtmosphereState::sample()`.

Tick order (must not be reordered): (1) terrain coupling, (2) pressure from temperature, (3) wind from pressure gradient + Coriolis + damping + turbulence, (4) semi-Lagrangian advection of T/q/cloud + water drift correction + Laplacian diffusion, (5) solar forcing + ground temp + air temp, (6) moisture sources + condensation + precipitation. Temperature forcing (step 5) runs after advection (step 4) so transported air masses are forced in-place rather than having the forced values overwritten by stale upwind temperatures on the next advection step — this was a critical bug fix.

Key physics: solar forcing (K_SOLAR=8.0), temperature reference `T_ref = -15 + solar_seasonal * 48` with `season_offset * latitude * 12` seasonal modulation (equator peak ~30°C in summer, poles ~ -15°C baseline; allows heat waves > 30°C and deep polar cold), temperature relaxation toward T_ref (K_RELAX_T=0.05, nonlinear — `K_RELAX_T * (1 + 0.05 * delta_T²)` strengthens for large excursions), wind damping (K_DAMP=0.05), east-west periodic boundary advection (ADVECT_SPEED=0.5), partial temperature advection (15% local retention prevents latitude gradient erosion from numerical diffusion), minimum equatorial Coriolis (0.04 floor deflects meridional flow into zonal trade winds), ocean evaporation (OCEAN_EVAP_RATE=0.15), land evaporation (LAND_EVAP_RATE=0.05), latent heat (K_LATENT=8.0), multi-scale thermal turbulence (fast ~1-hour gusts + slow ~6-hour mesoscale shifts, daytime-boosted, weaker over water). Storminess uses a multiplicative formula `humidity_ratio * instability^3 * (1 + cloud*0.5)` where instability is nonzero only when convergence exceeds a 0.2 threshold — produces realistic ~15-19% severe storm coverage rather than the previous additive formula that saturated most cells at 1.0. **Precipitation**: physics-based cloud lifecycle — noisy condensation thresholds cause cloud thickening; when cloud exceeds CLOUD_PRECIP_THRESHOLD (0.12) precipitation falls at CLOUD_PRECIP_RATE (1.2) and cloud dissipates via CLOUD_EVAP_BASE (0.15), CLOUD_EVAP_DRY (0.40), and pressure-driven CLOUD_SUBSIDE_RATE (0.50). Condensation noise uses temporal crossfade (cubic Hermite blending between timesteps) to prevent global pattern teleport; two decorrelated noise layers (+1000 spatial offset on second layer). `hash01()` uses `uint32_t` parameters to eliminate signed integer overflow UB. Orographic precipitation remains continuous. `AtmosphereState::sample_nearest()` added alongside `sample()` for overlays (Precipitation, Storminess) that benefit from sharp nearest-neighbour boundaries rather than bilinear blending. Atmosphere q initialized at/below q_sat for local temperature to prevent condensation bursts. Auto-starts after weather bake.

Ten physical invariants are enforced each tick: Stefan-Boltzmann T^4 radiative cooling; water mass conservation with automatic proportional correction when advection drift exceeds 0.1% of total atmospheric water (q + cloud); Clausius-Clapeyron saturation hard clamp on q; wind speed cap (MAX_WIND_SPEED = 6 cells/day); explicit Laplacian heat diffusion with east-west periodic wrap; explicit Laplacian moisture diffusion; convective instability release when ground-air temperature difference exceeds threshold; soil-moisture-limited land evaporation; CFL/Courant limiter clamping advection trace to ≤ 1.5 cells; and surface water overflow-to-soil rerouting in the dynamics layer. `AtmosphereState` carries `total_water` and `last_water_correction` for bookkeeping.

`AtmosphereStats` records T/q/cloud/precip/wind/pressure/storm min+max+mean, raining_cells (precip_rate > 0.01), cloudy_cells (cloud > 0.05), three invariant diagnostics (`total_atmo_water`, `max_courant`, `water_correction`), and latitude-band arrays `T_band[3]`, `wind_band[3]`, `q_band[3]` (north polar / equatorial / south polar thirds). `compute_atmosphere_stats()` populates it each tick.

**Dynamic simulation** (`dynamics.h/cpp`, `tick_dynamics`): per-tick terrain interaction, reads T/precip/wind from atmosphere when `atmo` pointer is non-null, falls back to static climate otherwise:
- Precipitation lands as rain (surface_water, capped at 1.0) or snow (snow_depth) based on temperature
- Snowmelt → surface_water when temperature > 0°C
- Infiltration: surface_water → soil_moisture, modulated by soil_hold and slope
- Evaporation: temperature + wind driven, suppressed by local_humidity; draws surface then soil
- Runoff: surface_water routes to precomputed downhill neighbors weighted by slope
- Terrain feedback: wet tile evaporation contributes to local_humidity
- `DynamicStats` tracks surface_water_mean/max, soil_moisture_mean/max, snow_mean/max, snow_tiles, flooded_tiles (surface_water > 0.3)

Overlay keys 1-9, 0, -, = for: None, Temperature, Precipitation, Moisture (atmospheric q), WindDirection, Evaporation, Storminess, MoistureBars, RainShadow (dynamic — traces current wind field), SurfaceWater, SoilMoisture, SnowDepth. Controls: T=play/pause dynamics, R=reset dynamics, Numpad+/-=speed, V=wind arrows, G=grid, N/P=preset cycle, Space=rebake weather. Legend panel shows live atmosphere + dynamics stats including invariant diagnostics; console diagnostics dump every 2 sim-days with latitude-band temperature and wind breakdown.

Headless mode: `sandbox_weather --headless N` runs N sim-days without a window, printing per-2-day rows with latitude-band temperatures (north/equatorial/south), global T min/mean/max, wind by band, q/cloud/precip means, raining cell count, cloudy cell count, and total atmospheric water. The raining/cloudy cell counts monitor spatial patchiness produced by precipitation intermittency. The final summary section shows cumulative peak extremes tracked across the entire run: peak T range, peak heat wave / deep freeze / severe storm / gale / heavy rain cell counts, and peak precipitation intensity per cell. Prints warnings if T spread collapses below 5°C, latitude bands equalize below 3°C spread, or wind dies below 0.5 cells/day. Useful for CI-style atmosphere stability verification without a display.

Source files: `terrain_gen.h/cpp` (shared terrain), `weather.h/cpp` (static bake), `atmosphere.h/cpp` (atmosphere simulation), `dynamics.h/cpp` (terrain-level tick), `renderer.h/cpp` (tile rendering + overlay dispatch), `telemetry.h/cpp` (FPS, legend, `render_dynamic_legend` takes `AtmosphereStats`), `main.cpp` (event loop). Uses `sandbox` namespace.

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
