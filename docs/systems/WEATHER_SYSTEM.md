# Weather System

**Implementation:** `sandboxes/weather/`

## Overview

The weather system generates and simulates atmospheric conditions across the procedurally generated world. It operates in three phases, each building on the previous:

1. **Static Weather Bake** -- A one-time pass that derives baseline climate fields (temperature, wind, precipitation, evaporation, storminess, rain shadows) from terrain and latitude. This provides the initial conditions and fallback values when the atmosphere simulation is not running.

2. **Atmosphere Simulation** -- A coarse-grid fluid dynamics model that evolves temperature, humidity, pressure, wind, cloud cover, and precipitation in real time. Runs on a grid 4x coarser than the terrain to keep the physics tractable at interactive framerates.

3. **Dynamic Terrain Interaction** -- A per-terrain-tile layer that converts atmospheric output into ground-level effects: rain accumulates as surface water or snow, snow melts, water infiltrates soil, evaporates, and flows downhill. Terrain feeds back into the atmosphere through local humidity.

The sandbox depends on the worldgen terrain system for elevation, slope, ocean masks, geological substrate properties (RockType, SoilTexture, ksat, field_capacity, etc.), roughness, and distance-to-ocean. Hydrology (rivers, lakes, D8 downhill routing, flow accumulation) is no longer precomputed — it emerges dynamically from precipitation interacting with the geological substrate. Its output (soil moisture, surface water, snow depth, temperature) provides the environmental foundation that the veggen sandbox consumes.

---

## Phase 1: Static Weather Bake

Source: `sandboxes/weather/weather.cpp` (function `bake_weather`)

The bake runs eight sequential steps over the full terrain grid and populates a `ClimateData` grid of `ClimateTile` values. Each step is independently timed for profiling.

### Step 1: Latitude Tables

Precomputes latitude in radians, sin, and cos for each row. Row y=0 maps to the north pole (+90 degrees), y=height-1 to the south pole (-90 degrees).

### Step 2: Temperature

Base temperature follows a cosine-of-latitude curve (warm equator, cold poles) plus a configurable greenhouse offset. Elevation reduces temperature via a lapse rate applied over an 8 km conceptual altitude range. Ocean and lake tiles are moderated toward a mild mean. Coastal land tiles within 10 tiles of the ocean receive additional moderation that warms cold coasts and slightly cools warm ones.

### Step 3: Wind Climatology (3-Cell Model)

Wind direction and strength are derived from a three-cell atmospheric circulation pattern:

| Latitude Band | Cell    | Zonal Wind     | Meridional Wind      |
|---------------|---------|----------------|----------------------|
| 0 - 30        | Hadley  | Trade easterlies | Toward equator (converging at ITCZ) |
| 30 - 60       | Ferrel  | Westerlies      | Poleward             |
| 60 - 90       | Polar   | Polar easterlies | Equatorward          |

Wind strength follows a sine curve within each band, scaled by configurable strength parameters. High-elevation terrain (above 0.5 normalized elevation) deflects and reduces wind magnitude.

### Step 4: Precipitation via Moisture-Flux Sweeps

Precipitation is computed through two independent moisture-transport sweeps:

- **Zonal (primary):** Sweeps each row in the direction of the prevailing zonal wind. Ocean and lake tiles inject moisture into the flux; land tiles recycle a small amount. At each step, a fraction of the flux rains out, amplified by the orographic factor when the next tile is higher (uplift). The flux decreases as moisture is extracted.

- **Meridional (secondary):** Sweeps each column north-to-south or south-to-north based on the midpoint meridional wind. Uses the same evaporation/rainout logic but at reduced weight (controlled by `meridional_weight`, default 0.3).

Raw precipitation values are normalized to the 0-1 range after both sweeps.

### Step 5: Evaporation Demand

Evaporation demand scales with temperature and insolation (cosine of latitude). Cold regions have near-zero evaporation; hot equatorial regions approach the maximum.

### Step 6: Effective Moisture and Aridity Seed

Effective moisture is computed as a proper aridity index ratio: `precip / (evap + 0.01) * 0.8`, clamped to 0-1. This represents the net water availability for vegetation and soil, and also seeds the initial `DynamicTile::aridity` values so the dynamic simulation starts with physically meaningful moisture balance rather than a uniform default.

### Step 7: Storminess (5-Factor Model)

Storminess is a weighted sum of five factors, computed for interior tiles using neighbor gradients:

| Factor                  | Weight | Description |
|-------------------------|--------|-------------|
| Moisture convergence    | 30%    | Surrounding tiles wetter than the center tile |
| Wind shear              | 20%    | Gradient magnitude of wind components across neighbors |
| Thermal instability     | 20%    | Hot + moist conditions favoring convection |
| Frontal zones           | 15%    | 2D temperature gradient magnitude |
| Cyclogenesis band       | 15%    | Latitude-dependent storm formation: midlatitude peak near 45 degrees, tropical cyclone peak near 18 degrees (ocean-only, requires SST above 26 C) |

### Step 8: Rain Shadow Detection

For each dry land tile (precipitation below 0.2), the algorithm traces upwind (opposite the local wind vector) up to 12 tiles. If it finds elevated terrain (above 0.55) with significantly more precipitation (at least 2x and 0.1 absolute more), the tile is marked as rain shadow. Shadow strength combines elevation difference, precipitation contrast, and proximity to the blocking terrain.

---

## Phase 2: Atmosphere Simulation

Source: `sandboxes/weather/atmosphere.h`, `sandboxes/weather/atmosphere.cpp`

### Coarse-Grid Model

The atmosphere operates on a grid where each cell covers a 4x4 block of terrain tiles (configurable via the `scale` parameter). This reduces the cell count by 16x, making fluid advection and diffusion affordable at interactive rates. Fields are sampled back to terrain resolution via bilinear interpolation for smooth gradients (temperature, humidity, wind) or nearest-neighbor for fields with sharp boundaries (precipitation, storminess).

### Tick Order

Each atmosphere tick executes the following steps in a fixed sequence. The ordering is physically motivated:

1. **Terrain coupling** -- Update each atmosphere cell's terrain averages from the dynamic state below it (snow, soil wetness) and apply terrain-driven physics: orographic uplift from wind hitting rising terrain, aspect-modulated solar heating, maritime moderation by distance-to-ocean, wet-soil evaporation feedback, and storm persistence attenuation over smooth terrain. Upper troposphere fields are refreshed from boundary-layer state.

1b. **Seasonal baseline update** -- Slowly shift `T_base` toward the current seasonal temperature reference so the climate baseline tracks the annual cycle without fast physics writing to it. `q_base` tracks the Clausius-Clapeyron saturation curve modulated by distance-to-ocean.

2. **Quasi-geostrophic (QG) dynamical core** -- Replaces the previous pressure-from-T and PGF+Coriolis+damping wind model with a two-layer QG system (ADR-0014). The QG core is executed in sub-steps:

   - **2a: Background thermal wind shear** -- Compute the zonal-mean meridional temperature gradient `dT_base/dy` and derive the background vertical wind shear `U_s(y) = -QG_SHEAR_GAIN * dT_base/dy`. This shear makes upper-layer winds faster than lower-layer winds in proportion to the pole-to-equator temperature gradient, as in the real atmosphere.

   - **2b: Stochastic baroclinic eddy parameterization** -- On a 64×32 grid, semi-Lagrangian numerical diffusion (~0.14/day at synoptic scale) exceeds the Eady growth rate (~0.13/day), killing perturbation growth. Instead of advecting PV, weather-scale PV perturbations are generated stochastically and evolved by persistence: `eddy_new = eddy_old * persist + target * (1 - persist)` where `target` is smooth multi-scale noise (primary 4-cell + secondary 12-cell + fine 5-cell wavelengths) modulated by local baroclinicity (`|U_shear|`). Amplitude is latitude-enveloped (sin² — strongest at mid-latitudes). The upper-layer perturbation is positive and the lower-layer perturbation is opposite-sign (×−0.6), producing a baroclinic tilt that drives temperature anomalies via step 2g. A slow eastward drift (`EDDY_DRIFT_SPEED=0.3 × day_of_year`) advects the noise coordinate to simulate weather system propagation. Constants: `EDDY_AMPLITUDE=4.0`, `EDDY_PERSIST=0.95`, `EDDY_DRIFT_SPEED=0.3`, `EDDY_CELL_SIZE=4`. Del-4 hyperviscosity (previously step 2d) is no longer needed because stochastic generation replaces advection — there is no grid-scale noise to suppress.

   - **2c: Ekman drag** -- Surface friction spins down lower-layer relative vorticity: `q2_pv -= QG_EKMAN_R * lap_psi2 * dt`. Terrain roughness (`avg_roughness * 0.5`) enhances drag over rough land.

   - **2e: PV inversion** -- SOR (200 iterations, ω=1.6) solves the coupled elliptic system for streamfunctions `psi1`, `psi2` given `q1_pv`, `q2_pv`:
     ```
     q1 = del2(psi1) + invLd2*(psi2 - psi1) + beta*y
     q2 = del2(psi2) + invLd2*(psi1 - psi2) + beta*y
     ```
     x-periodic; y-wall: `psi = 0` at pole rows. Gauss-Seidel ordering (updated `psi1` used immediately when computing `psi2`).

   - **2f: Wind diagnosis** -- Upper-layer winds `cell.u_upper`, `cell.v_upper` are diagnosed from `psi1` streamfunction gradients. Surface winds are computed via a **pressure gradient force (PGF) model** rather than directly from streamfunction finite differences (which are too smooth on a 64×32 grid). Surface winds = PGF from p_anom gradient + Coriolis + cross-isobar friction (K_PGF=0.6, K_CROSS=0.25), blended 90% with the lower-layer QG streamfunction wind. Only 10% of the background thermal wind shear U_s is mixed into the surface to avoid hemispheric-rotation artifacts. This produces realistic spatial surface wind variation where the direct streamfunction approach would produce overly smooth fields.

   - **2g: Thermal wind coupling** -- `T_anom += QG_C_T * (psi1 - psi2)` (QG_C_T=2.0). The inter-layer streamfunction difference is the QG thermal wind balance: a positive `psi1 - psi2` corresponds to warm anomalies (warm-core low), negative to cold anomalies. This replaces the previous T_anom relaxation step.

3. **Semi-Lagrangian advection** -- Transport humidity (`q_anom`), cloud cover, precip budget, and wind momentum using the QG-derived winds (ADVECT_SPEED=4.0). East-west boundaries wrap periodically; north-south boundaries clamp at the poles. After advection, a water conservation correction is applied to counteract mass drift from bilinear interpolation.

4. **Moisture diffusion** -- Laplacian diffusion (K_MOIST_DIFFUSE=0.03) smooths humidity gradients. Temperature anomaly diffusion is not a separate step — the stochastic eddy parameterization in step 2b controls synoptic-scale T variability via PV inversion and thermal wind coupling.

5. **Solar forcing and ground-air coupling** -- Solar intensity depends on latitude, season, and time of day. Ground temperature evolves under solar heating, Stefan-Boltzmann radiative cooling, and air-ground coupling. **T prognostics discipline**: all fast physics in this step modifies `T_anom` only (QG step 2g sets the base anomaly; solar and ground coupling add to it). `cell.T = T_base + T_anom` is recomputed at the start of step 6 so external code always reads a consistent composite. The reference temperature formula is `T_ref = -15 + solar_seasonal * 48`.

6. **Moisture, condensation, and precipitation** -- Evaporation from ocean (OCEAN_EVAP_RATE=0.10) and land (LAND_EVAP_RATE=0.04, quadratic soil moisture dependence: `wet*wet*RATE` for desert shutoff) adds humidity. **Ocean evaporation is reduced by cloud shading**: `cloud_shading = 1.0 - cell.cloud * 0.8f`, clamped to [0.2, 1.0] — cloud cover blocks solar heating of the ocean surface, breaking the tight evap→condense→rain feedback loop that causes perpetual overcast in ocean-heavy presets. Saturation follows Clausius-Clapeyron. Condensation uses noisy thresholds (two spatial scales with temporal crossfade) to create patchy cloud systems. CLOUD_PRECIP_THRESHOLD=0.06 (raised from 0.03 — requires thicker cloud before rain begins), CLOUD_PRECIP_RATE=1.5. **Cloud dissipation has dual pathways**: (a) evaporative dissipation returns cloud back to vapor, capped so q cannot exceed q_sat; (b) radiative dissipation (`RADIATIVE_DISSIP_FRAC=0.7` of total dissipation) destroys cloud without adding moisture, representing longwave emission to space — this prevents cloud from accumulating indefinitely over warm saturated ocean where the evaporative pathway is blocked by Clausius-Clapeyron. CLOUD_EVAP_BASE=0.30, CLOUD_EVAP_DRY=0.50, CLOUD_SUBSIDE_RATE=0.80.

7. **Convective instability** -- When ground-air temperature difference exceeds 6 C and humidity exceeds 50% of saturation, convective uplift forces condensation and enhanced precipitation.

8. **Storm formation** -- Storminess computed as multiplicative combination of wind convergence, temperature gradient, and humidity ratio. Cloud amplifies the result via cubic power law.

### Precipitation Intermittency

Rain in the atmosphere model is deliberately patchy and intermittent rather than uniform. Several mechanisms create this behavior:

- **Noisy condensation threshold:** Smooth noise varies the humidity level at which condensation begins, so some cells condense earlier than neighbors, producing cloud clusters.
- **Temporal crossfade:** The noise pattern evolves every ~3 hours with cubic Hermite blending between timesteps, preventing abrupt global pattern changes.
- **Threshold-based precipitation:** Clouds must thicken above a minimum before producing rain, creating a natural build-thicken-rain-dissipate lifecycle.
- **Pressure-driven subsidence:** High-pressure cells raise the effective saturation point and accelerate cloud clearing, creating clear gaps between rain systems.

### Physical Invariants

The simulation enforces ten physical invariants to maintain stability and realism:

| # | Invariant                | Description |
|---|--------------------------|-------------|
| 1 | Stefan-Boltzmann cooling (lower) | Ground and boundary-layer radiative emission follows T^4 law (K_RAD=0.09), preventing runaway surface heating |
| 2 | Stefan-Boltzmann cooling (upper) | Upper troposphere radiates to space via T^4 law (K_RAD_UPPER=0.025, weaker — lower optical depth). Root-cause fix for energy drift that previously required an artificial correction |
| 3 | Water conservation       | Total atmospheric water (humidity + cloud) is tracked; advection-induced drift from bilinear interpolation is corrected each tick |
| 4 | Clausius-Clapeyron       | Saturation humidity increases exponentially with temperature; q is clamped to q_sat after advection and diffusion to catch any supersaturation introduced by numerical transport |
| 5 | Cloud evaporation limit  | Cloud dissipation (evaporation back to vapor) is capped so the resulting q_anom cannot push q above q_sat |
| 6 | Wind speed cap           | Hard maximum of 6 cells/day prevents numerical blowup from QG-derived winds |
| 7 | Moisture diffusion       | Humidity anomaly (q_anom) spreads from wet to dry via Laplacian diffusion (K_MOIST_DIFFUSE=0.03) |
| 8 | Convective instability   | Large ground-air temperature differences trigger forced condensation and storm precipitation, releasing energy |
| 9 | Evaporation limits       | Land evaporation cannot exceed available soil moisture |
| 10 | CFL/Courant limiter     | Moisture tracer advection trace distance clamped to 1.5 cells per step (PV is no longer advected; stochastic generation eliminates the CFL constraint on PV) |

---

## Phase 3: Dynamic Terrain Interaction

Source: `sandboxes/weather/dynamics.h`, `sandboxes/weather/dynamics.cpp`

Each terrain tile maintains a `DynamicTile` with surface water, soil moisture, snow depth, local humidity, derived overlay values, and aridity tracking fields. The tick processes all land tiles in nine steps. After a headless run completes, `print_hydrology_diagnostics()` prints a comprehensive water-budget report (see Diagnostics section).

### 1. Precipitation

When the atmosphere simulation is active, precipitation rate is read directly from the atmosphere grid. Otherwise, a fallback path modulates the static baked precipitation with smooth hash-based noise that drifts with the wind, creating moving weather fronts with a spatial scale of 24 tiles.

If the local temperature is below freezing, precipitation accumulates as snow. Otherwise, rain is split between surface water and direct soil infiltration using **Variable Source Area (VSA) partitioning** — see step 1 detail below. Total precipitation is tracked in the `WaterBudget` for run-end diagnostics.

**VSA rainfall-runoff partitioning:** At 50 km/tile, most rainfall infiltrates soil. The surface runoff fraction is computed dynamically from three physical mechanisms, clamped to [5%, 70%]:

1. **Base impervious fraction (5%)**: bare rock, frozen ground, and sub-grid urban cover always generate some runoff regardless of soil state.
2. **Saturation-excess Dunne runoff (0–50%)**: when soil moisture approaches `field_capacity`, the storage deficit closes and water cannot infiltrate fast enough. The runoff fraction ramps from 0 at 70% saturation to 50% at full saturation. This is the dominant mechanism over gentle terrain.
3. **Hortonian overland flow (0–15%)**: steep slopes create shorter contact time between rainfall and soil, adding up to 15% additional surface runoff regardless of soil state.

Excess precipitation beyond the soil's `field_capacity` always routes as surface runoff (saturation-excess). Dry unsaturated soil infiltrates nearly all rainfall; saturated soil may route up to 70% directly to surface water.

### 2. Snowmelt

When temperature rises above freezing, snow melts at a rate proportional to temperature (0.08 per day per degree C). Meltwater becomes surface water, creating seasonal flood pulses. Snowmelt volume is tracked in the `WaterBudget`.

### 3. Infiltration

Surface water seeps into soil moisture at a rate governed by the tile's geological `ksat` (saturated hydraulic conductivity) normalized by `soil_depth` and `field_capacity` from `TerrainTile`. The `ksat / soil_depth` normalization gives the effective per-tick infiltration fraction, ensuring deep soils accept the same absolute volume as shallow soils at the same conductivity. Sandy soils with high `ksat` drain quickly; clay-rich soils drain slowly. The raw ksat rate is scaled by **0.25×** to allow surface water persistence for wetlands and river channels — at the natural ksat rate, standing water on flat loam drains within two days, preventing any transient pooling outside of basin depressions. Slope reduces infiltration via a superlinear penalty: `pow(slope01, 0.7)` (was a linear 70% cap), allowing moderate slopes to retain more infiltration than a hard linear cutoff while still strongly penalizing steep terrain. Soil moisture cannot exceed `field_capacity`. Excess water that percolates past the root zone drains into the `groundwater` reservoir (tracked as `total_gw_recharge` in `WaterBudget`).

### 4. Evaporation

Evaporation draws from surface water first (at EVAP_SURFACE_RATE=0.01, reduced from 0.02), then soil moisture (at EVAP_SOIL_RATE=0.007, reduced from 0.015). The rate scales with temperature, wind speed, and inverse local humidity. A `depth_boost` factor (0.5) modestly amplifies evaporation from deeper standing water. Soil evaporation is blocked below the `wilting_point` to prevent soil from drying beyond plant-accessible limits. **Actual evapotranspiration (AET) is capped to PET** — total evaporation from both surface and soil cannot exceed the atmosphere's evaporative demand in a given tick, preventing physically impossible hyperdrying in fast time steps. On land with active atmosphere, evaporation is physically limited by available moisture. Evaporation volumes are split into surface and soil components in the `WaterBudget`.

### 5. Sub-Grid Channel Drainage

At 50 km/tile resolution, each tile contains unresolved stream networks — first-order to third-order channels that route water to the tile outlet far faster than tile-to-tile WSE diffusion. These are modeled as a **slope-dependent linear reservoir** (the same formulation used by large-scale land models VIC and CLM):

```
drain_rate = K_CHANNEL * sqrt(slope)   [1/day, K_CHANNEL = 50]
channel_drain = surface_water * (1 - exp(-drain_rate * dt_days))
```

At slope = 0.01, drain_rate = 5/day — 99% of surface water exits within one day. At slope = 0.002 (near-flat), drain_rate = 2.2/day — 89% exits per day. The exponential form gives numerical stability for large time steps.

**D8 sinks bypass this step.** Tiles whose D8 downstream pointer points to themselves (depression centers) do not drain via the channel model — water accumulates and is managed by the basin spillway system (step 5b-ii). This separation is what enables lake formation at topographic depressions while keeping non-lake terrain well-drained.

Channel drain routes to the D8 downstream neighbor. If the downstream neighbor is ocean, volume is counted as `total_coastal_drain` in `WaterBudget`.

### 6. WSE Runoff

Surface water that remains after sub-grid channel drainage routes to neighboring tiles via WSE (water-surface-elevation) based multi-neighbor flow. Outflow is distributed across every neighbor with lower water surface elevation, weighted by the elevation gradient. A minimum WSE gradient of 0.005 is enforced to allow micro-pooling rather than forcing every wet tile to drain. Ocean tiles are treated as a separate drain path (volume tracked as `total_coastal_drain` in `WaterBudget`, which rolls into `total_ocean_drain`) and are excluded from the WSE neighbor search. A CFL cap limits the outflow fraction (`effective_k = min(K_OUT * dt_days, 0.5)`) to ensure numerical stability. **Flash runoff from surface-water cap overflow is routed to the D8 downstream neighbor** (not discarded to ocean); it is only counted as `total_overflow` in `WaterBudget` and falls back to ocean drain if no downstream neighbor exists. Runoff accumulates in a pre-allocated buffer (`DynamicState::runoff_buf`) and is applied after all tiles are processed to avoid order-dependent artifacts.

**Headwater minimum discharge:** Tiles with elevation above 0.55 receive a fixed minimum discharge contribution plus a fraction of local precipitation, preventing headwater streams from going silent between precipitation events.

### 5b. Groundwater

Deep drainage from infiltration feeds a `groundwater` reservoir per tile. The reservoir is capped at `soil_depth * porosity` (from TerrainTile geology) to prevent accumulation beyond the physical capacity of the substrate. Groundwater discharges back to `surface_water` as baseflow proportional to the reservoir depth, creating perennial flow even between precipitation events — springs, seeps, and baseflow-fed streams emerge from this mechanism.

### 5b-ii. Basin Spillway Storage

At initialization, `init_dynamics` identifies all **D8 sink basins** — sets of tiles draining into a single terminal sink tile — via BFS over the D8 graph. For each basin it stores:

- `basin_sink_elev`: elevation of the terminal sink tile
- `basin_spill_elev`: minimum elevation along the basin boundary (the lowest tile on the rim that connects to an adjacent basin or ocean; once WSE reaches this height, the basin spills)
- `basin_area`: number of tiles in the basin
- `basin_spill_tile`: the tile index where overflow exits

A two-octave **micro-relief noise** field (8-tile + 3-tile wavelengths, amplitude scaled by local slope) is added to the hydrology elevation used during init. This deepens topographic depressions in low-slope areas, creating realistic bowl-shaped basins rather than artificially flat floors.

During each tick, for basins where `basin_spill_elev − basin_sink_elev >= 0.001` (minimum depression depth), surface water across all basin tiles is aggregated into `basin_volume`. If the mean WSE (basin volume / area + sink elevation) is below `basin_spill_elev`, the basin retains all water (lake storage). If WSE exceeds the spill elevation, the excess routes through `basin_spill_tile` to the downstream basin or ocean, simulating lake overflow and spill-chain connectivity.

This mechanism enables persistent lakes to form in topographic depressions without requiring terrain-generation to pre-classify lake tiles.

### 6c. D8 Flow Accumulation (Two-Reservoir)

After WSE runoff (step 6) is applied, a **D8 steepest-descent flow accumulation** pass updates per-tile discharge. Tiles are visited in topographic order (`DynamicState::topo_order`, computed once at init by elevation-sorting), and each tile propagates flow to its single steepest downhill neighbor (`DynamicState::downhill`, the D8 sink). A `SINK_TOLERANCE=0.001` allows micro-depressions to retain a small puddle rather than draining completely — these become potential lake sites.

D8 accumulation is split into two separate reservoirs propagated independently along the same D8 graph:

- **Quickflow** (`accum_quick`): surface runoff routed to WSE neighbors, coastal drain, and cap overflow flash runoff. Represents fast storm-pulse response. Smoothed with exponential EMA, time constant tau=0.3 days (~7 hours).
- **Baseflow** (`accum_base`): groundwater discharge to surface. Represents slow perennial flow. Smoothed with exponential EMA, time constant tau=10 days.

`DynamicTile::discharge` is updated as the sum of EMA-smoothed `quickflow` + `baseflow_d` each tick. `DynamicState::accum_discharge` holds the combined accumulation buffer for visualization. The Discharge (F5) overlay and `print_hydrology_diagnostics()` both read from `accum_discharge`.

### 7. Terrain Feedback

Total evaporation from each tile feeds back into local humidity, which decays each tick. This creates a dampening effect: wet areas evaporate more, raising local humidity, which then suppresses further evaporation.

### 8. Overflow Handling

Surface water is capped by `surface_water_cap(slope01)`: 3.0 on flat terrain (slope=0), 1.5 at slope=0.1, with a 0.65× multiplier applied above slope=0.12 for steep terrain. This allows realistic lake depth in depression basins while preventing water accumulation on slopes. Overflow above the cap transfers into soil moisture (up to soil capacity). All fields are clamped to valid ranges.

### 9. Aridity Index Update

PET (potential evapotranspiration) is computed from temperature and wind speed with no humidity suppression — it represents the evaporative demand of the atmosphere regardless of water availability. The instantaneous aridity index is the ratio of effective precipitation to PET. An exponential moving average (50-day window, EMA weight 70/30 — previously 80/20) smooths the aridity value to remove day-to-day noise while tracking seasonal shifts faster. Values above 1.0 indicate humid conditions (more rain than evaporative demand); values below 0.2 indicate arid to hyper-arid conditions. The color gradient scale spans 0–1.5 (desert to very humid).

---

## Data Model

### AtmosphereCell

Each cell in the coarse atmosphere grid contains multiple field groups.

**Climate baseline (slow, seasonally updated)**

| Field     | Type  | Range | Description |
|-----------|-------|-------|-------------|
| `T_base`  | float | C     | Baseline temperature from bake + seasonal shift |
| `q_base`  | float | 0-1   | Baseline specific humidity |
| `p_base`  | float | hPa   | Baseline pressure |

**Weather anomaly (fast, advected, relaxed toward 0)**

| Field     | Type  | Range | Description |
|-----------|-------|-------|-------------|
| `T_anom`  | float | C     | Temperature anomaly |
| `q_anom`  | float | 0-1   | Humidity anomaly |
| `p_anom`  | float | hPa   | Pressure anomaly |

**Composite fields (base + anom, updated end-of-tick; all external code reads these)**

| Field           | Type  | Range       | Description |
|-----------------|-------|-------------|-------------|
| `T`             | float | C           | Air temperature |
| `q`             | float | 0-1         | Specific humidity |
| `p`             | float | hPa (arbitrary) | Pressure |
| `u`             | float | cells/day   | Zonal wind (east = positive) |
| `v`             | float | cells/day   | Meridional wind (south = positive) |
| `cloud`         | float | 0-1         | Condensed water (cloud cover) |
| `precip_rate`   | float | 0-1         | Precipitation output this tick |
| `storminess`    | float | 0-1         | Storm intensity |
| `ground_temp`   | float | C           | Surface temperature (ground coupling) |
| `avg_elevation` | float | 0-1         | Mean terrain elevation in this cell |
| `avg_roughness` | float | 0-1         | Mean terrain roughness in this cell |
| `avg_snow`      | float | 0-1+        | Mean snow depth from dynamic state |
| `avg_soil_wet`  | float | 0-1+        | Mean soil + surface water from dynamic state |
| `is_water`      | bool  | --          | True if majority of underlying terrain tiles are water |

**Terrain coupling fields (Phase 0)**

| Field              | Type  | Description |
|--------------------|-------|-------------|
| `avg_slope`        | float | Mean slope01 of terrain tiles in this cell |
| `avg_aspect`       | float | Mean terrain aspect (atan2 slope direction) |
| `avg_dist_ocean`   | float | Mean distance-to-ocean of terrain tiles |

**Moisture budget (Phase 1)**

| Field           | Type  | Range | Description |
|-----------------|-------|-------|-------------|
| `precip_budget` | float | 0-2   | Remaining precipitation capacity; advected with wind, depleted by rain, recharged over ocean |

**Upper troposphere (Phase 3)**

| Field       | Type  | Range     | Description |
|-------------|-------|-----------|-------------|
| `T_upper`   | float | C         | Upper air temperature |
| `q_upper`   | float | 0-1       | Upper specific humidity |
| `u_upper`   | float | cells/day | Upper wind x (diagnosed from psi1) |
| `v_upper`   | float | cells/day | Upper wind y (diagnosed from psi1) |
| `p_upper`   | float | hPa       | Upper pressure (~500 hPa) |
| `stability` | float | C         | T_upper - T (negative = convectively unstable) |

**QG dynamics (ADR-0014)**

| Field    | Type  | Range          | Description |
|----------|-------|----------------|-------------|
| `psi1`   | float | cell^2/day     | Upper-layer streamfunction; u_upper = -dpsi1/dy, v_upper = dpsi1/dx |
| `psi2`   | float | cell^2/day     | Lower-layer streamfunction; u = -dpsi2/dy, v = dpsi2/dx |
| `q1_pv`  | float | 1/day          | Upper-layer potential vorticity; set stochastically by eddy parameterization (step 2b), subject to Ekman drag on lower layer |
| `q2_pv`  | float | 1/day          | Lower-layer potential vorticity; set stochastically with opposite-sign tilt, subject to Ekman drag |

### AtmosphereState

| Field                    | Type     | Description |
|--------------------------|----------|-------------|
| `width`, `height`        | uint32_t | Coarse grid dimensions |
| `scale`                  | uint32_t | Terrain tiles per atmosphere cell (default 4) |
| `time_of_day`            | float    | Hours, 0-24 |
| `day_of_year`            | float    | 0-365, starts at ~spring equinox (day 80) |
| `cells`                  | vector   | Current atmosphere cells |
| `scratch`                | vector   | Double-buffer for advection |
| `total_water`            | float    | Sum of q + cloud across all cells (water conservation diagnostic) |
| `last_water_correction`  | float    | Advection drift correction applied last tick |
| `last_dt_days`           | float    | Timestep used in last tick |
| `initial_energy`         | float    | Total atmospheric energy at initialization (Phase 4 energy tracking) |
| `total_energy`           | float    | Current total atmospheric energy (Phase 4) |

### WaterBudget

Cumulative accounting of water flows across the entire simulation run. Stored in `DynamicState::budget` and reported by `print_hydrology_diagnostics()` at the end of headless runs.

| Field                 | Type   | Description |
|-----------------------|--------|-------------|
| `total_precip`        | double | Total precipitation delivered to land tiles (rain + snowmelt) |
| `total_evap_surface`  | double | Total evaporation from surface water |
| `total_evap_soil`     | double | Total evaporation from soil moisture |
| `total_ocean_drain`   | double | Total water lost to ocean (coastal WSE drain + cap overflow flash runoff) |
| `total_coastal_drain` | double | WSE outflow from land tiles directly to ocean-neighbour tiles |
| `total_overflow`      | double | Flash runoff from surface-water cap routed to D8 downstream (or ocean if no downstream exists) |
| `total_gw_recharge`   | double | Total groundwater recharge from deep infiltration |
| `total_snowmelt`      | double | Total snowmelt converted to surface water |
| `total_headwater`     | double | Total headwater minimum injection (fixed floor for tiles with elevation > 0.55) |
| `total_accum_raw`     | double | Total D8 accumulation seed before headwater injection |
| `initial_storage`     | double | Total water stored at simulation start (sum of sw + sm + gw + snow) |

### DynamicTile

| Field               | Type  | Range | Description |
|---------------------|-------|-------|-------------|
| `surface_water`     | float | 0-1+  | Standing water / puddles |
| `soil_moisture`     | float | 0-1   | Root-zone water |
| `snow_depth`        | float | 0-1+  | Accumulated snow |
| `local_humidity`    | float | 0-1   | Evaporation feedback contribution |
| `effective_precip`  | float | 0-1   | Current precipitation rate after weather modulation |
| `effective_moisture`| float | 0-1   | Combined moisture (60% soil + 25% surface + 15% humidity) |
| `effective_evap`    | float | 0-1   | Actual evaporation rate this tick |
| `effective_storm`   | float | 0-1   | Storminess from weather fronts |
| `groundwater`       | float | 0+    | Groundwater reservoir depth (meters). Fed by deep drainage; discharges to surface_water as baseflow. |
| `discharge`         | float | 0+    | EMA-smoothed total discharge this tick (quickflow + baseflow_d). Used to visualize emergent rivers. |
| `quickflow`         | float | 0+    | Fast-response discharge component (surface runoff, storm pulses). EMA tau=0.3 days. |
| `baseflow_d`        | float | 0+    | Slow-response discharge component (groundwater-fed perennial flow). EMA tau=10 days. |
| `pet`               | float | 0+    | Potential evapotranspiration (temp + wind, no humidity suppression) |
| `aridity`           | float | 0+    | EMA-smoothed aridity index (precip / PET, 70/30 EMA weight); > 1.0 = humid, 0.5-1.0 = sub-humid, 0.2-0.5 = semi-arid, < 0.2 = arid/desert; seeded at init from static bake |

### DynamicState

| Field           | Type     | Description |
|-----------------|----------|-------------|
| `width`, `height` | uint32_t | Matches terrain grid dimensions |
| `tiles`         | vector   | Per-tile dynamic state |
| `elapsed_days`  | float    | Total simulation time elapsed |
| `time_scale`    | float    | Speed multiplier (0.25x to 32x) |
| `paused`        | bool     | Whether the dynamic simulation is paused |
| `runoff_buf`        | vector\<float\>    | Pre-allocated buffer for WSE runoff accumulation; reused each tick to avoid per-tick heap allocation |
| `downhill`          | vector\<int32_t\>  | D8 steepest-descent sink index per tile (-1 for ocean/boundary tiles). Computed once at init. |
| `topo_order`        | vector\<uint32_t\> | Tile indices sorted from highest to lowest elevation. Used to propagate D8 flow in correct upstream-first order. Computed once at init. |
| `accum_discharge`   | vector\<float\>    | Combined D8 flow accumulation buffer (quickflow + baseflow, reset each tick). Drives Discharge (F5) overlay and `print_hydrology_diagnostics()`. |
| `accum_quick`       | vector\<float\>    | Per-tick quickflow accumulation buffer propagated along D8 (surface runoff, coastal drain, overflow). |
| `accum_base`        | vector\<float\>    | Per-tick baseflow accumulation buffer propagated along D8 (groundwater discharge). |
| `basin_id`          | vector\<uint16_t\> | Per-tile basin ID (0xFFFF = not in any sink basin). Assigned at init from BFS over D8 sinks. |
| `basin_spill_elev`  | vector\<float\>    | Per-basin minimum boundary elevation (spill point). Basin fills until WSE reaches this level. |
| `basin_sink_elev`   | vector\<float\>    | Per-basin minimum elevation (the D8 sink tile). |
| `basin_area`        | vector\<uint32_t\> | Per-basin tile count. |
| `basin_volume`      | vector\<float\>    | Per-basin current water volume (surface_water summed over basin tiles). Updated each tick. |
| `basin_spill_tile`  | vector\<uint32_t\> | Per-basin tile index where overflow exits the basin when WSE exceeds `basin_spill_elev`. |
| `num_basins`        | uint16_t           | Total number of identified sink basins. |
| `budget`            | WaterBudget        | Cumulative water-budget accounting across the run. |

---

## Diagnostics and Telemetry

Source: `sandboxes/weather/telemetry.h`, `sandboxes/weather/telemetry.cpp`

### AtmosphereStats

Computed from the atmosphere grid, this structure provides:

- Min/max/mean for temperature, humidity, cloud, precipitation, wind speed, and pressure
- Standard deviation for temperature, wind speed, humidity, and precipitation (spatial variation)
- Wind direction spread (0 = all winds aligned, 1 = uniformly distributed)
- Latitude-band diagnostics: mean temperature, wind speed, and humidity for three bands (north polar, equatorial, south polar)
- Extreme event counts: severe storms (storminess > 0.5), heavy rain (precip > 0.15), heat waves (T > 30 C), deep freezes (T < -25 C), gale-force winds (speed > 4.0)
- Invariant monitoring: total atmospheric water, Courant number, water conservation correction
- **Phase 1 moisture budget**: budget_min, budget_max, budget_mean — tracks remaining precipitation capacity across cells
- **Phase 2 anomaly diagnostics**: T_anom_min, T_anom_max, T_anom_stddev, q_anom_stddev — measures weather variability separated from seasonal baseline
- **Phase 3 upper layer diagnostics**: T_upper_mean, wind_upper_mean, wind_upper_max, stability_min, stability_max, stability_mean, inversion_cells (stability > INVERSION_THRESHOLD)
- **Phase 4 energy budget**: total_energy, solar_in (energy input this tick), radiative_out (energy lost this tick), latent_release (from condensation), energy_drift_pct (% drift from initial state). Energy tracked decomposed into E_lower (boundary layer), E_upper (free troposphere), E_latent (condensed water), E_ground (surface temperature). Artificial bulk energy correction removed — upper-layer Stefan-Boltzmann OLR (invariant 2) is the root-cause fix.

### DynamicStats

Computed from the dynamic terrain state (land tiles only):

- Mean and max for surface water, soil moisture, and snow depth
- Count of snow-covered tiles (snow > 0.01) and flooded tiles (surface water > 0.3)
- `groundwater_mean`, `groundwater_max` — groundwater reservoir diagnostics
- `discharge_mean`, `discharge_max` — surface outflow (emergent river strength)
- `aridity_mean`, `aridity_min`, `aridity_max` — distribution of EMA aridity index across land tiles
- `pet_mean` — mean potential evapotranspiration across land tiles
- `arid_tiles` — count of tiles with aridity < 0.2 (arid to hyper-arid)
- `humid_tiles` — count of tiles with aridity > 1.0 (humid)

### Headless Diagnostic Mode

The sandbox supports a headless mode for automated testing and tuning, invoked via command-line arguments:

- `--headless N [preset]` -- Simulate N days on a single preset without opening a window. The preset argument accepts either a numeric index (0–5) or a preset name string (e.g., "Pangaea", "Archipelago"). Defaults to preset 0 if omitted. Prints a columnar diagnostic table every 2 days (or 15 days for runs over 120 days) with latitude-band temperatures, global extremes, variation metrics, event counts, and aridity mean/min/max. Ends with a summary (including peak aridity and PET) and warnings for collapsed gradients or missing weather events. After the atmosphere summary, four **reality-check (RC) metrics** are printed to verify the system produces physically structured (not just statistically correct) output:
  - **RC1: Wind direction temporal variability** — circular autocorrelation of wind angle at 6 sample points across the grid, measured at lag 1/3/7/14 days. Mean resultant length (MRL) < 0.4 passes (varied direction); MRL > 0.7 fails (wind locked to one direction). Real weather: lag-3 autocorrelation should fall below ~0.5.
  - **RC2: Spatial wind direction structure** — distribution of adjacent-cell wind angle differences across the grid. Reports p10/p50/p90/max. Median diff < 3° = FAIL (spatially uniform); 3–8° = WARN; ≥ 8° = OK. At ~625 km grid cells, cells within the same weather system (500–1500 km) are expected to have similar wind directions, so moderate differences are correct.
  - **RC3: Precipitation intermittency** — per-cell fraction of snapshots with active rain (precip_rate > 0.005). Reports p10/p25/p50/p75/p90 and counts of always-dry (<1%) and always-wet (>90%) cells. FAIL if median rain fraction < 2% (rain never reaches most cells) or >25% of cells always rain.
  - **RC4: Temperature anomaly spatial scale** — zonal autocorrelation of T_anom at lag 1/2/4/8/16/32 cells. Should drop below 0.5 by lag 4–8 cells, indicating synoptic-scale weather systems rather than grid-wide anomalies.
  After the RC section, `print_hydrology_diagnostics()` prints a full hydrology report.

- `--headless-all N` -- Runs the headless diagnostic for all six presets sequentially.

Per-tick cumulative tracking records the peak values of extreme events across the entire run, reported in the final summary.

### Hydrology Diagnostics

`print_hydrology_diagnostics()` (declared in `dynamics.h`, called at the end of headless runs from `main.cpp`) prints a comprehensive water-budget and flow-network report:

- **Water budget**: total precipitation, surface evaporation, soil evaporation, ocean drain, groundwater recharge, snowmelt, storage change (current storage minus initial storage). These should approximately balance to zero for a steady-state run.
- **Storage distributions**: percentile breakdowns (min / p25 / p50 / p75 / max) for surface water, soil moisture, groundwater, and snow depth across land tiles.
- **Discharge network thresholds**: fraction of land tiles carrying discharge above 0.01, 0.1, and 1.0 units; useful for assessing river network connectivity.
- **Discharge log-histogram**: log₂-binned histogram of `accum_discharge` values, showing distribution of stream orders in the emergent river network.
- **Lake connected-component analysis**: count and area distribution of lake bodies (tiles with surface water above a threshold), identifying where pooling produces persistent lakes.
- **Discharge by elevation band**: mean discharge in each of five elevation quintile bands, revealing whether headwaters or lowlands dominate throughflow.
- **Discharge by aridity zone**: mean discharge in arid (aridity < 0.2), semi-arid (0.2–0.5), sub-humid (0.5–1.0), and humid (> 1.0) zones.
- **D8 sink count**: number of tiles whose D8 downhill pointer is a terminal sink (ocean or local depression), indicating the number of independent drainage basins.

### Interactive Diagnostics

In the SDL2 window mode, diagnostics are printed to stdout every 2 simulation days with atmosphere state (temperature range, humidity, cloud, precipitation, wind, pressure) and dynamic state (surface water, soil moisture, snow). Invariant monitoring (total atmospheric water, CFL number, conservation correction) is included in each diagnostic line.

---

## Overlays

Twenty overlay modes visualize different aspects of the weather system, toggled by keyboard:

| Key    | Overlay          | Source                | Color Map |
|--------|------------------|-----------------------|-----------|
| 1      | None             | --                    | Terrain only |
| 2      | Temperature      | Atmosphere T or baked | Blue (-20 C) to white (10 C) to red (40 C) |
| 3      | Precipitation    | Atmosphere precip_rate (nearest-neighbor) or baked | Brown (dry) to green to blue (wet) |
| 4      | Moisture         | Atmosphere q or baked | Brown (dry) to green (wet) |
| 5      | Wind Direction   | Atmosphere or baked wind vectors | Yellow-white arrows with dark outlines |
| 6      | Evaporation      | Dynamic effective_evap or baked | Yellow (low) to red (high) |
| 7      | Storminess       | Atmosphere storminess (nearest-neighbor) or baked | Gray (calm) to purple (stormy) |
| 8      | MoistureBars     | Dynamic effective_moisture or baked | Blue bars, height proportional to moisture |
| 9      | RainShadow       | Dynamic upwind trace or baked | Orange gradient (light to deep) |
| 0      | SurfaceWater     | Dynamic surface_water | Tan (dry) to cyan to deep blue (flooded) |
| -      | SoilMoisture     | Dynamic soil_moisture | Brown (dry) to dark green (saturated) |
| =      | SnowDepth        | Dynamic snow_depth | Transparent to bright white |
| `      | PrecipBudget     | AtmosphereCell::precip_budget | Brown (depleted) to green (full, 1.0) to white (surplus > 1.0) |
| F1     | UpperWind        | Upper troposphere u_upper/v_upper | Wind arrows for upper-layer circulation |
| F2     | Stability        | AtmosphereCell::stability | Red (unstable, negative) to gray (neutral) to blue (inversion, positive) |
| F3     | Aridity          | DynamicTile::aridity | Red (desert, aridity ~0) → yellow (semi-arid) → green (sub-humid) → blue (humid, aridity ~1.5+); color scale normalized to aridity / 1.5 |
| F4     | Groundwater      | DynamicTile::groundwater | Tan (empty) to cyan to blue (deep reservoir) |
| F5     | Discharge        | DynamicTile::discharge | Log-scaled black → cyan → white; transparent for no-flow tiles; reveals emergent river channels |
| F6     | Geology          | TerrainTile::rock (RockType) | Categorical swatches: Granite / Basalt / Limestone / Sandstone / Shale / Metamorphic |
| F7     | SoilTextureOverlay | TerrainTile::soil (SoilTexture) | Categorical swatches: Sand / Loam / Silt / Clay / Peat |

When the atmosphere simulation is running (unpaused), overlays switch to live atmosphere values. Precipitation and storminess use nearest-neighbor sampling from the coarse grid to preserve sharp rain/storm cell boundaries. Other fields use bilinear interpolation for smooth gradients.

---

## Interactive Controls

| Key            | Action |
|----------------|--------|
| W/A/S/D or Arrow keys | Pan camera |
| Mouse wheel    | Zoom (centered on cursor) |
| 1-9, 0, -, =  | Select overlay mode (None through SnowDepth) |
| ` (backtick)  | PrecipBudget overlay |
| F1             | UpperWind overlay |
| F2             | Stability overlay |
| F3             | Aridity overlay |
| F4             | Groundwater overlay |
| F5             | Discharge overlay |
| F6             | Geology overlay |
| F7             | SoilTextureOverlay |
| V              | Toggle wind arrows |
| G              | Toggle grid |
| T              | Play/pause dynamic simulation |
| , (comma)      | Halve simulation speed (min 0.25x) |
| . (period)     | Double simulation speed (max 32x) |
| R              | Reset dynamic simulation to initial state |
| N or ]         | Next world preset |
| P or [         | Previous world preset |
| Space          | Rebake weather (regenerates static climate and reinitializes dynamics + atmosphere) |
| F              | Toggle FPS display |
| F12            | Save screenshot as BMP |
| L              | Export weather log (timings + statistics) to text file |
| Escape         | Quit |

UI buttons for Prev, Next, and Rebake are rendered at the top of the window, along with an **Overlay Cycle** button that advances through all overlay modes on click. The controls hint panel is aligned at Y=40 (below the button row) and shows clearly labelled key groups. The **camera fits the world on startup** (`fit_world()`) so the full map is visible without manual zoom-out; `clamp_to_world()` is called every frame to prevent panning into the void beyond map edges. The rain shadow count displayed in screenshots mode and in the live stats panel is computed dynamically from the current simulation state rather than from stale bake data.

---

## Rendering

Source: `sandboxes/weather/renderer.h`, `sandboxes/weather/renderer.cpp`, `sandboxes/weather/tile_texture.h`, `sandboxes/weather/tile_texture.cpp`

### Terrain Rendering (Clipmap + Autotile Templates)

Terrain is rendered through a two-level clipmap texture cache that drives a discrete autotile template system, providing SNES/GBA-style pixel art terrain at any zoom level without per-frame overdraw.

**Cache levels** (defined by `TerrainCacheLevel`):

| Level | Alias | Resolution | Coverage | Lifetime |
|---|---|---|---|---|
| L0 | Macro | 4 px/tile | Whole world | Baked once at startup via `bake_terrain_cache()` |
| L1 | Meso | 16 px/tile | Visible camera region + 2-tile margin | Regenerated when camera pans > 2 tiles via `update_meso_cache()` |

At zoom below ~6 screen-pixels-per-tile the macro level is displayed; above that threshold the meso level is blitted. A flat-color per-tile fallback fires only when no cache is available (e.g., before `bake_terrain_cache()` has completed). `bake_terrain_cache(world, seed, water_level)` must be called once after terrain generation and again after any preset change. `invalidate_terrain_cache()` destroys both textures and resets validity flags (called from `Renderer::shutdown()`). `generate_template_atlas(atlas, seed)` must be called before the first `bake_terrain_cache()` call.

**Autotile classification** (`tile_texture.h/cpp`):

Each tile is classified by `classify_tile(terrain, tx, ty, water_level)` into one of three families:

| Family | Description |
|---|---|
| `PureTerrain` | No boundary — filled with a uniform material texture |
| `Coast` | A land/water boundary line crosses this tile |
| `Elevation` | An elevation contour boundary crosses this tile |

Transition tiles additionally carry an `EdgePair` (which edges the boundary enters and exits) and a `TransitionGrade`:

| Grade | Coast treatment | Elevation treatment |
|---|---|---|
| Shallow | Sandy beach transition | Gentle gradient ramp |
| Medium | Rocky shore transition | Breakline |
| Steep | Cliff face | Terrace / escarpment |

The boundary line is a Bezier curve from the entry edge midpoint to the exit edge midpoint, dividing the tile into two distinct regions (`MaskRegion::RegionA` / `RegionB` / `Boundary`).

**Template atlas** (`TemplateAtlas`):

Pre-generated 16×16 sprite templates indexed by (canonical edge pair × grade × shape variant). Only two canonical edge pairs are stored:

- `CanonicalPair::NS` — straight-through boundary (N→S)
- `CanonicalPair::NE` — corner/arc boundary (N→E)

All 12 other edge combinations are derived via `rotation_lut[entry][exit]` which records the canonical pair, number of 90° clockwise rotation steps (0–3), and a horizontal flip flag. This keeps the atlas compact (2 × 3 grades × 3 shape variants = 18 transition templates).

**Material system**:

`Mat` enum defines 12 material types: Water, Sand, SoilLoam, SoilDry, SoilClay, Gravel, Granite, Basalt, Limestone, Sandstone, Shale, Metamorphic. `classify_tile_mat(tile, water_level)` maps a `TerrainTile` to its material from `RockType`, elevation, and slope. Each pure-terrain material has 3 texture variants for micro-variation. Hypsometric tinting uses 8 discrete warm→cool elevation bands.

**Pixel evaluator**:

`eval_terrain_pixel(terrain, world_x, world_y, pixels_per_tile, seed, water_level)` evaluates one RGBA32 pixel at arbitrary sub-tile world-space coordinates using the autotile system. `render_terrain_region()` fills a caller-owned pixel buffer for a rectangular patch (used by both cache levels). `TerrainLOD` (Macro/Meso/Micro) gates noise detail; edge blending between adjacent tiles ensures seamless boundaries.

Mountain triangle and hill bump glyphs have been removed — autotile texture provides terrain detail at all zoom levels. The `dim_glyphs` flag has been removed from `render_terrain()`.

**Legacy API**: `generate_tile_texture()` and `render_tile_patch()` are retained for compatibility but wrap the new autotile system.

**Dynamic tinting** (composited on top of the clipmap blit):

When the dynamic simulation is active, terrain tiles are overlaid with semi-transparent SDL rectangles encoding ground conditions:

- **Wet soil:** Darkens the red channel, slightly greens the tile, and reduces blue -- simulating saturated earth.
- **Snow:** Blends toward white (240, 240, 255) proportional to snow depth, saturating at a depth of about 0.33.
- **Standing water:** Blends toward blue (40, 100, 200) when surface water exceeds 0.1, up to 60% blend strength.

### Precipitation Overlay

Precipitation uses nearest-neighbor sampling from the coarse atmosphere grid rather than bilinear interpolation. This preserves the discrete boundaries of rain showers -- rain either falls on a tile or it does not -- rather than smearing into invisible gradients. The raw atmosphere precipitation rate (which peaks around 0.2-0.3) is rescaled by 4x to use the full color range.

### Wind Arrows

Wind is rendered as directional arrows in neutral gray (no yellow-white fill) with density reduced at low zoom so the arrows do not obscure terrain at small scales. Arrow length scales with wind speed (capped at 1.5). Arrow spacing adapts to zoom level. When the atmosphere is running, arrows show live wind; otherwise they show the baked wind field.

### Font Glyphs

The bitmap font used in the hint panel and legend labels includes glyphs for comma (`,`) and forward slash (`/`), added to support key-binding labels such as `,/.` for speed control and `/` in overlay descriptions.

### UI Component Library

Source: `sandboxes/weather/ui.h`, `sandbox::ui` namespace.

All panels (stats, dynamics/metrics, hint box, tooltips) are built from a shared set of CSS-inspired primitives:

| Component | Description |
|---|---|
| `Theme` | Centralized `SDL_Color` palette (panel_bg, panel_border, text, text_muted, text_bright, accent/gold, info/blue, danger/red, ok/green). Singleton via `Theme::instance()`. |
| `Panel` | Box-model struct. `draw_bg()` fills a fully opaque background (alpha 255 — eliminates stale-pixel artifacts), draws a 1-px border and 2-px drop shadow. `push_clip()`/`pop_clip()` set/restore SDL clip rect for `overflow:hidden`. |
| `ColumnLayout` | Flexbox-column cursor. `text()`, `separator()`, `spacer()` advance the Y cursor. |
| `anchor_rect()` | Positions a box by `TopLeft/TopRight/BottomLeft/BottomRight` anchor within a parent area. |
| `render_tooltip()` | Floating tooltip near the cursor. `text` is split on `|` into up to 8 lines. The box flips horizontally or vertically when near a screen edge. Border uses accent gold at 70% opacity. |
| Spacing tokens | `S0`–`S5` (0, 4, 8, 12, 16, 24 px). Safe-area insets `SAFE_L/T/R/B` (8 px each). |
| `auto_width()` | Shrink-wraps a panel width to its longest line of text plus padding. |
| `row_height()` | Returns glyph height + vertical gap for a given font scale. |

The right-hand dynamics/metrics panel uses a **fixed pixel width** (not `auto_width`) to prevent layout jitter as numeric values change each tick. All drawing uses `SDL_RenderFillRect`/`SDL_RenderDrawRect`/`SDL_RenderDrawLine` — no SDL texture allocation.

### Tooltips

Hovering over a value in either the left-hand weather stats panel or the right-hand dynamics/metrics panel shows a floating tooltip with a plain-language explanation of that metric. Tooltips are rendered last (on top of all other UI elements) by `main.cpp` after the telemetry and legend panels are drawn. Multi-line tooltips use `|` as a line delimiter in the tooltip string.

### Overlay Info Panel

When an overlay is active, a secondary info box is drawn immediately to the right of the color legend panel in the bottom-left corner, bottom-aligned with it. The box shows 4–6 lines of plain-English text describing what drives changes in the displayed metric and how it interacts with other simulation layers.

Implementation in `telemetry.cpp`:

- `OverlayInfo` struct: `const char* lines[8]`, `int count`.
- `get_overlay_info(OverlayMode)` — static function returning the description for each mode. All 19 active overlay modes have entries: Temperature, Precipitation, Moisture, WindDirection, Evaporation, Storminess, MoistureBars, RainShadow, SurfaceWater, SoilMoisture, SnowDepth, PrecipBudget, UpperWind, Stability, Aridity, Groundwater, Discharge, Geology, SoilTexture.
- `INFO_PANEL_TEXT_W` — static constant computed once at startup via a lambda that iterates all overlay modes and measures the widest text line. All info panels share this fixed width to prevent layout shift when cycling overlays.
- The panel height is `max(legend_rect.h, info_content_h + 2*padding)`, so it stretches to match the legend when the legend is taller than the text.

---

## Integration Points

- **Depends on worldgen terrain:** Elevation, slope, ocean masks, geological substrate (`RockType`, `SoilTexture`, `ksat`, `field_capacity`, `wilting_point`, `bedrock_permeability`), roughness, distance-to-ocean, and elevation bands. Precomputed downhill routing, lake masks, soil fertility/hold, and river flow are no longer inputs — all hydrology is computed dynamically. All terrain data comes from `sandboxes/weather/terrain_gen.h` and `sandboxes/weather/terrain_gen.cpp`, kept in sync with the worldgen sandbox pipeline.

- **Feeds into veggen:** The veggen sandbox uses soil moisture, surface water, temperature, and precipitation patterns to drive plant growth. The weather system's dynamic terrain interaction layer produces the environmental state that vegetation responds to.

- **World presets:** The sandbox includes six world presets (Archipelago, Pangaea, Island Chain, Continental, Highland, Waterworld), each with distinct terrain parameters and seeds. These are defined in `sandboxes/weather/main.cpp`.
