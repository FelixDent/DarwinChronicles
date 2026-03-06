# Weather System

**Implementation:** `sandboxes/weather/`

## Overview

The weather system generates and simulates atmospheric conditions across the procedurally generated world. It operates in three phases, each building on the previous:

1. **Static Weather Bake** -- A one-time pass that derives baseline climate fields (temperature, wind, precipitation, evaporation, storminess, rain shadows) from terrain and latitude. This provides the initial conditions and fallback values when the atmosphere simulation is not running.

2. **Atmosphere Simulation** -- A coarse-grid fluid dynamics model that evolves temperature, humidity, pressure, wind, cloud cover, and precipitation in real time. Runs on a grid 4x coarser than the terrain to keep the physics tractable at interactive framerates.

3. **Dynamic Terrain Interaction** -- A per-terrain-tile layer that converts atmospheric output into ground-level effects: rain accumulates as surface water or snow, snow melts, water infiltrates soil, evaporates, and flows downhill. Terrain feeds back into the atmosphere through local humidity.

The sandbox depends on the worldgen terrain system for elevation, slope, ocean/lake masks, soil properties, roughness, and precomputed downhill neighbors. Its output (soil moisture, surface water, snow depth, temperature) provides the environmental foundation that the veggen sandbox consumes.

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

### Step 6: Effective Moisture

Effective moisture is precipitation minus half the evaporation demand, clamped to 0-1. This represents the net water availability for vegetation and soil.

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

1. **Terrain coupling** -- Update each atmosphere cell's averaged snow, soil wetness from the dynamic terrain state below it.

2. **Pressure from temperature** -- Compute pressure field from the current temperature distribution. Warm air creates low pressure; cool air creates high pressure. Elevation reduces pressure. Wind convergence feeds back into pressure (convergence lowers it, divergence raises it). Humid air reduces pressure (lighter molecules). This must run before wind so the pressure gradient is ready for the next step.

3. **Wind from pressure gradients** -- Accelerate wind along the pressure gradient (high to low). Apply Coriolis deflection proportional to latitude, with a minimum magnitude near the equator to prevent pure meridional flow. Apply damping (air friction), terrain roughness drag, and multi-scale thermal turbulence (fast hourly gusts plus slow 6-hour mesoscale shifts, boosted during afternoon). Cap wind speed to the hard maximum.

4. **Semi-Lagrangian advection** -- Transport temperature, humidity, cloud cover, and wind momentum by tracing backwards through the wind field and sampling via bilinear interpolation. Temperature retains 15% of the local value to compensate for numerical diffusion that would otherwise erode the latitude gradient. Wind momentum is partially advected (60% advected, 40% local) to prevent self-reinforcing jets. East-west boundaries wrap periodically; north-south boundaries clamp at the poles. After advection, a water conservation correction is applied to counteract mass drift from bilinear interpolation.

5. **Heat and moisture diffusion** -- Laplacian diffusion smooths temperature and humidity gradients, preventing extreme spikes and numerical artifacts. East-west wraps; poles are walls.

6. **Solar forcing and ground-air coupling** -- This runs after advection so that transported air masses receive forcing in their new location rather than having forced values overwritten by stale upwind data. Solar intensity depends on latitude, season (sinusoidal annual cycle starting at the spring equinox), and time of day (diurnal cycle peaking in the afternoon). Albedo varies with snow cover, water bodies, and time of day. Cloud cover shades the surface. Ground temperature evolves under solar heating, radiative cooling (Stefan-Boltzmann T^4 law), evaporative cooling from soil moisture, and air-ground coupling. Water bodies have high thermal inertia and buffer temperature. Air temperature relaxes toward a latitude-elevation reference temperature (T_ref) with nonlinear strength: small deviations relax gently (preserving weather fronts for about 9 days), while large deviations snap back quickly (restoring climate within a day).

7. **Moisture, condensation, and precipitation** -- Evaporation from ocean (strong) and land (weaker, limited by soil moisture) adds humidity. Saturation humidity follows a Clausius-Clapeyron approximation (exponential in temperature). Orographic uplift from wind hitting rising terrain forces extra condensation. Condensation uses a noisy threshold -- smooth spatial noise at two scales (6-cell and 3-cell clusters) with temporal crossfade every ~3 hours creates patchy cloud systems instead of uniform layers. High pressure raises the effective saturation threshold via subsidence. Excess humidity above the noisy threshold condenses into cloud, releasing latent heat. Cloud must thicken above a precipitation threshold before rain begins; thicker cloud produces heavier rain. Orographic precipitation squeezes cloud where uplift is strong. Cloud dissipates back to vapor at a rate driven by humidity deficit and high-pressure subsidence.

8. **Convective instability** -- When the ground is much warmer than the air (above a 6 C threshold) and humidity exceeds 50% of saturation, convective uplift forces additional condensation and enhanced precipitation, simulating convective storms.

9. **Storm formation** -- Storminess is computed as a multiplicative combination of wind convergence (above a 0.2 threshold), temperature gradient magnitude, and humidity ratio. Cloud presence amplifies the result. The formula uses a cubic power law, making storms rare threshold events rather than gradual.

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
| 1 | Stefan-Boltzmann cooling | Ground radiative emission follows T^4 law, preventing runaway heating |
| 2 | Water conservation       | Total atmospheric water (humidity + cloud) is tracked; advection-induced drift from bilinear interpolation is corrected each tick |
| 3 | Clausius-Clapeyron       | Saturation humidity increases exponentially with temperature; excess condenses |
| 4 | Wind speed cap           | Hard maximum of 6 cells/day prevents numerical blowup |
| 5 | Heat diffusion           | Temperature spreads from hot to cold via Laplacian diffusion |
| 6 | Moisture diffusion       | Humidity spreads from wet to dry via Laplacian diffusion |
| 7 | Convective instability   | Large ground-air temperature differences trigger forced condensation and storm precipitation, releasing energy |
| 8 | Evaporation limits       | Land evaporation cannot exceed available soil moisture |
| 9 | Surface water overflow   | Excess surface water above capacity infiltrates soil before being lost |
| 10 | CFL/Courant limiter     | Advection trace distance is clamped to 1.5 cells per step; wind speed times advection speed times dt must remain near or below 1 |

---

## Phase 3: Dynamic Terrain Interaction

Source: `sandboxes/weather/dynamics.h`, `sandboxes/weather/dynamics.cpp`

Each terrain tile maintains a `DynamicTile` with surface water, soil moisture, snow depth, local humidity, and derived overlay values. The tick processes all land tiles in seven steps:

### 1. Precipitation

When the atmosphere simulation is active, precipitation rate is read directly from the atmosphere grid. Otherwise, a fallback path modulates the static baked precipitation with smooth hash-based noise that drifts with the wind, creating moving weather fronts with a spatial scale of 24 tiles.

If the local temperature is below freezing, precipitation accumulates as snow. Otherwise, 75% becomes surface water and 25% infiltrates directly into soil.

### 2. Snowmelt

When temperature rises above freezing, snow melts at a rate proportional to temperature (0.08 per day per degree C). Meltwater becomes surface water, creating seasonal flood pulses.

### 3. Infiltration

Surface water seeps into soil moisture at a base rate modulated by the terrain's soil holding capacity (higher capacity means better infiltration) and slope (steep slopes reduce infiltration by up to 70%). Infiltration cannot push soil moisture above capacity (1.0).

### 4. Evaporation

Evaporation draws from surface water first, then soil moisture. The rate scales with temperature, wind speed, and inverse local humidity. Deep standing water evaporates faster due to greater exposed surface area. On land with active atmosphere, evaporation is physically limited by available moisture.

### 5. Runoff

Surface water flows downhill via precomputed downhill neighbor links from the worldgen terrain. Runoff rate scales with slope (steeper = more runoff). Water reaching ocean or lake tiles is treated as a sink. Basin tiles with no downhill neighbor lose a small amount to groundwater seepage. Runoff accumulates in a temporary buffer and is applied after all tiles are processed to avoid order-dependent artifacts.

### 6. Terrain Feedback

Total evaporation from each tile feeds back into local humidity, which decays each tick. This creates a dampening effect: wet areas evaporate more, raising local humidity, which then suppresses further evaporation.

### 7. Overflow Handling

Surface water exceeding 1.0 overflows into soil moisture (up to soil capacity). The remaining excess is clamped. All fields are clamped to valid ranges.

---

## Data Model

### AtmosphereCell

Each cell in the coarse atmosphere grid contains:

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

### AtmosphereState

| Field                    | Type     | Description |
|--------------------------|----------|-------------|
| `width`, `height`        | uint32_t | Coarse grid dimensions |
| `scale`                  | uint32_t | Terrain tiles per atmosphere cell (default 4) |
| `time_of_day`            | float    | Hours, 0-24 |
| `day_of_year`            | float    | 0-365, starts at ~spring equinox (day 80) |
| `cells`                  | vector   | Current atmosphere cells |
| `scratch`                | vector   | Double-buffer for advection |
| `total_water`            | float    | Sum of q + cloud across all cells (diagnostic) |
| `last_water_correction`  | float    | Advection drift correction applied last tick |
| `last_dt_days`           | float    | Timestep used in last tick |

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

### DynamicState

| Field          | Type     | Description |
|----------------|----------|-------------|
| `width`, `height` | uint32_t | Matches terrain grid dimensions |
| `tiles`        | vector   | Per-tile dynamic state |
| `elapsed_days` | float    | Total simulation time elapsed |
| `time_scale`   | float    | Speed multiplier (0.25x to 32x) |
| `paused`       | bool     | Whether the dynamic simulation is paused |

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

### DynamicStats

Computed from the dynamic terrain state (land tiles only):

- Mean and max for surface water, soil moisture, and snow depth
- Count of snow-covered tiles (snow > 0.01) and flooded tiles (surface water > 0.3)

### Headless Diagnostic Mode

The sandbox supports a headless mode for automated testing and tuning, invoked via command-line arguments:

- `--headless N [preset]` -- Simulate N days on a single preset (default: preset 0) without opening a window. Prints a columnar diagnostic table every 2 days (or 15 days for runs over 120 days) with latitude-band temperatures, global extremes, variation metrics, and event counts. Ends with a summary and warnings for collapsed gradients or missing weather events.

- `--headless-all N` -- Runs the headless diagnostic for all six presets sequentially.

Per-tick cumulative tracking records the peak values of extreme events across the entire run, reported in the final summary.

### Interactive Diagnostics

In the SDL2 window mode, diagnostics are printed to stdout every 2 simulation days with atmosphere state (temperature range, humidity, cloud, precipitation, wind, pressure) and dynamic state (surface water, soil moisture, snow). Invariant monitoring (total atmospheric water, CFL number, conservation correction) is included in each diagnostic line.

---

## Overlays

Twelve overlay modes visualize different aspects of the weather system, toggled by keyboard:

| Key | Overlay        | Source                | Color Map |
|-----|----------------|-----------------------|-----------|
| 1   | None           | --                    | Terrain only |
| 2   | Temperature    | Atmosphere T or baked | Blue (-20 C) to white (10 C) to red (40 C) |
| 3   | Precipitation  | Atmosphere precip_rate (nearest-neighbor) or baked | Brown (dry) to green to blue (wet) |
| 4   | Moisture       | Atmosphere q or baked | Brown (dry) to green (wet) |
| 5   | Wind Direction | Atmosphere or baked wind vectors | Yellow-white arrows with dark outlines |
| 6   | Evaporation    | Dynamic effective_evap or baked | Yellow (low) to red (high) |
| 7   | Storminess     | Atmosphere storminess (nearest-neighbor) or baked | Gray (calm) to purple (stormy) |
| 8   | MoistureBars   | Dynamic effective_moisture or baked | Blue bars, height proportional to moisture |
| 9   | RainShadow     | Dynamic upwind trace or baked | Orange gradient (light to deep) |
| 0   | SurfaceWater   | Dynamic surface_water | Tan (dry) to cyan to deep blue (flooded) |
| -   | SoilMoisture   | Dynamic soil_moisture | Brown (dry) to dark green (saturated) |
| =   | SnowDepth      | Dynamic snow_depth | Transparent to bright white |

When the atmosphere simulation is running (unpaused), overlays switch to live atmosphere values. Precipitation and storminess use nearest-neighbor sampling from the coarse grid to preserve sharp rain/storm cell boundaries. Other fields use bilinear interpolation for smooth gradients.

---

## Interactive Controls

| Key            | Action |
|----------------|--------|
| W/A/S/D or Arrow keys | Pan camera |
| Mouse wheel    | Zoom (centered on cursor) |
| 1-9, 0, -, =  | Select overlay mode |
| V              | Toggle wind arrows |
| G              | Toggle grid |
| T              | Play/pause dynamic simulation |
| Numpad +       | Double simulation speed (max 32x) |
| Numpad -       | Halve simulation speed (min 0.25x) |
| R              | Reset dynamic simulation to initial state |
| N or ]         | Next world preset |
| P or [         | Previous world preset |
| Space          | Rebake weather (regenerates static climate and reinitializes dynamics + atmosphere) |
| F              | Toggle FPS display |
| F12            | Save screenshot as BMP |
| L              | Export weather log (timings + statistics) to text file |
| Escape         | Quit |

UI buttons for Prev, Next, and Rebake are also rendered at the top of the window.

---

## Rendering

Source: `sandboxes/weather/renderer.h`, `sandboxes/weather/renderer.cpp`

### Terrain Tinting

When the dynamic simulation is active, terrain tiles are tinted based on ground conditions:

- **Wet soil:** Darkens the red channel, slightly greens the tile, and reduces blue -- simulating saturated earth.
- **Snow:** Blends terrain color toward white (240, 240, 255) proportional to snow depth, saturating at a depth of about 0.33.
- **Standing water:** Blends toward blue (40, 100, 200) when surface water exceeds 0.1, up to 60% blend strength.

### Precipitation Overlay

Precipitation uses nearest-neighbor sampling from the coarse atmosphere grid rather than bilinear interpolation. This preserves the discrete boundaries of rain showers -- rain either falls on a tile or it does not -- rather than smearing into invisible gradients. The raw atmosphere precipitation rate (which peaks around 0.2-0.3) is rescaled by 4x to use the full color range.

### Wind Arrows

Wind is rendered as directional arrows with a dark outline for visibility against any background and a bright yellow-white foreground. Arrow length scales with wind speed (capped at 1.5). Arrow spacing adapts to zoom level. When the atmosphere is running, arrows show live wind; otherwise they show the baked wind field.

---

## Integration Points

- **Depends on worldgen terrain:** Elevation, slope, ocean/lake masks, soil holding capacity, roughness, distance-to-ocean, downhill neighbor links, and elevation bands. All terrain data comes from `sandboxes/weather/world_gen.h` and `sandboxes/weather/world_gen.cpp`, which are shared with the worldgen sandbox.

- **Feeds into veggen:** The veggen sandbox uses soil moisture, surface water, temperature, and precipitation patterns to drive plant growth. The weather system's dynamic terrain interaction layer produces the environmental state that vegetation responds to.

- **World presets:** The sandbox includes six world presets (Archipelago, Pangaea, Island Chain, Continental, Highland, Waterworld), each with distinct terrain parameters and seeds. These are defined in `sandboxes/weather/main.cpp`.
