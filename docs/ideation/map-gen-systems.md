Below is a C++-oriented implementation spec (modules + structs + step-by-step pipeline + concise pseudocode) for:

1. **Base terrain generation**
2. **Weather/climate system** (planet/atmosphere config + cached climatology; uses terrain + water bodies)
3. **Plant ecology model** (vegetation density/type from environment)

---

# 0) Common conventions

* World grid: `W x H` tiles (e.g., `W=1000, H=500`)
* Latitude: per tile row `y` (0..H-1), map to `lat_rad ∈ [-π/2, +π/2]`
* Use SoA arrays for performance: `std::vector<float> elev(W*H);`
* Prefer normalized units internally:

  * temperature in °C (or normalized then convert)
  * moisture/soil water in `[0,1]`
  * biomass in `[0, K_i]`
* “Canonical year cache”: store climate/forcing in `N_SEASON_BINS` (e.g., 36 ten-day bins) to repeat.

---

# 1) Procedural base terrain generation

## 1.1 Core structs

```cpp
struct TerrainGenParams {
  uint64_t seed;

  // Continental layout
  float continent_noise_scale;     // e.g. 0.0008
  float continent_threshold;       // controls land fraction
  float ridge_noise_scale;         // mountains
  float ridge_strength;

  // Elevation shaping
  float sea_level;                 // [0..1] threshold
  float erosion_iters;             // optional
  float uplift_strength;

  // Hydrology
  int   river_sources_per_1e6_tiles;
  float lake_fill_level;           // how easily basins fill
};

struct SoilParams {
  float base_fertility;            // [0..1]
  float fertility_from_slope;      // reduce fertility on steep slopes
  float fertility_from_rivers;     // increase near rivers
  float water_hold_from_material;  // sand/clay proxy
};
```

```cpp
enum class ElevBand : uint8_t { Water=0, Lowland=1, Hills=2, Mountains=3 };

struct TerrainTile {
  float elev01;          // [0..1]
  ElevBand band;

  bool is_ocean;
  bool is_lake;

  float slope01;         // [0..1]
  float dist_ocean;      // in tiles
  float dist_water;      // ocean or lake

  float river_flow;      // optional
  float soil_fertility;  // [0..1]
  float soil_hold;       // [0..1] water holding capacity

  float roughness;       // [0..1], from band + vegetation later
};
```

## 1.2 Terrain generation pipeline

### Step A — Generate raw elevation (continents + mountains)

1. Create “continental mask” using low-frequency noise:

* `c = fbm(seed, x, y, continent_noise_scale)`
* land if `c > continent_threshold`

2. Create mountains using ridge noise / domain-warped ridges:

* `r = ridged_fbm(seed2, x, y, ridge_noise_scale)`

3. Combine:

* `e = landMask * (base_land_elev + uplift_strength * r) + ocean_baseline`

4. Normalize to `[0..1]`

### Step B — Set sea level; define ocean

* `is_land = elev01 > sea_level`
* Flood-fill from map edges through water tiles to mark **ocean**.
* Any water not connected to edge is potential **lake basin**.

### Step C — Lakes (basin fill)

* For each enclosed basin, optionally “fill” to a local spill height:

  * compute basin boundary minimum rim height
  * fill interior water up to `min(rim_height, lake_fill_level)`
* Mark `is_lake`.

### Step D — Slope and elevation bands

* `slope01` from local gradient (central difference)
* Bands (example):

  * if `water` → `Water`
  * else if `elev01 < sea_level + 0.15` → `Lowland`
  * else if `elev01 < sea_level + 0.35` → `Hills`
  * else → `Mountains`

### Step E — Rivers (optional but great for ecology)

* Choose river sources: high elevation + high wetness later (or just high elev).
* Compute flow direction (steepest descent) and accumulate flow.
* Carve river channels (slightly reduce elev along high-flow paths).
* Store `river_flow`.

### Step F — Distance fields

* BFS or multi-source Dijkstra from ocean tiles → `dist_ocean`
* BFS from all water tiles (ocean + lakes + rivers optionally) → `dist_water`

### Step G — Soil fields (cheap procedural)

* Soil fertility:

  * start with `base_fertility + noise_smallscale`
  * decrease with slope
  * increase near rivers/water
* Soil water-holding (`soil_hold`):

  * from material noise (sand vs clay proxy)
  * increase in lowlands, decrease on steep slopes

## 1.3 Key outputs (used by climate + plants)

* `elev01`, `band`, `slope01`
* `is_ocean`, `is_lake`, `dist_ocean`, `dist_water`, `river_flow`
* `soil_fertility`, `soil_hold`

---

# 2) Weather / climate system (with atmosphere configuration)

Goal: **fast**, “realistic enough”, supports:

* latitude + day/night moving across map
* seasons from orbital/axial parameters
* terrain + oceans + rain shadows
* cached canonical year climatology + stochastic events

## 2.1 Atmosphere + astronomy config

```cpp
struct AstronomyConfig {
  float tilt_rad;             // obliquity ε
  float rotation_period_s;    // Prot
  float orbital_period_s;     // Porb
  float eccentricity;         // e
  float periapsis_long_rad;   // ϖ
  float solar_constant;       // S0 (scale)
};

struct AtmosphereConfig {
  float albedo_ocean;         // base reflectance
  float albedo_land;
  float albedo_snow;

  float greenhouse;           // scalar warming offset
  float lapse_rate_C_per_km;  // ~6.5 C/km equivalent

  // Moisture + precipitation behavior
  float evap_ocean;           // base evap scale over ocean
  float evap_land;            // base evap scale over land
  float orographic_factor;    // rain enhancement from uplift
  float rainout_rate;         // how fast moisture flux converts to precip

  // Wind climatology control (very cheap)
  float hadley_strength;
  float jet_strength;
  float monsoon_strength;     // seasonal wind reversal amplitude

  // Event rates (optional)
  float storm_rate_base;
  float drought_rate_base;
  float cold_snap_rate_base;
};
```

## 2.2 Climate cache layout (“canonical year”)

Use `N_BINS` season bins (e.g., 36). For each bin store per tile or per **coarse climate cell** (recommended).

```cpp
constexpr int N_BINS = 36;

struct ClimateBinFields {
  // store at climate resolution (e.g., Wc x Hc)
  std::vector<float> T_mean;     // °C
  std::vector<float> P_mean;     // precip rate (0..1 or mm/day)
  std::vector<float> evap;       // evap demand (0..1)
  std::vector<float> wind_u;     // optional
  std::vector<float> wind_v;     // optional
  std::vector<float> storminess; // 0..1 event intensity
};

struct ClimateYearCache {
  int Wc, Hc;
  std::array<ClimateBinFields, N_BINS> bins;
};
```

**Note:** day/night “moving terminator” is computed at runtime for active time, not needed for long-term cache unless you want diurnal microclimate.

## 2.3 Climate generation pipeline (bake once)

### Step A — Precompute latitude tables

* `lat[y]`, `sinLat[y]`, `cosLat[y]`
* avoid trig in inner loops

### Step B — For each bin `b` compute astronomical forcing

Compute day-of-year angle `λ_b` and optionally eccentricity distance scaling `S_b`.

At minimum:

* `declination δ_b = asin( sin(tilt) * sin(λ_b + phase) )`

Compute **daily mean insolation** per latitude band (fast; no per-tile trig):

* `Q_lat[y] = daily_mean_insolation(lat[y], δ_b) * S_b`

### Step C — Build temperature target `Teq` map

For each climate cell or tile:

* base from insolation and albedo:

  * `Teq = T0 + kQ * (Q_lat - Qref)`
* apply greenhouse:

  * `Teq += greenhouse`
* apply elevation lapse:

  * `Teq -= lapse_rate * elev_meters`
* apply ocean moderation:

  * `Teq = lerp(Teq, Teq_ocean_smoothed, coastal_weight(dist_ocean))` (optional)

### Step D — Wind climatology field (no PDE)

Define latitudinal wind bands, optionally season-shifted by declination (ITCZ shift):

Example: zonal winds only:

* trades near equator, westerlies mid-lat, polar easterlies
* implement as `wind_u(lat, season)`; `wind_v` small meridional component toward ITCZ

Add monsoon reversal near large landmasses (optional):

* compute land fraction by latitude band and season; flip sign in tropics when land heats.

### Step E — Precipitation field via moisture flux sweeps

Key idea: directional sweeps along prevailing wind, carrying a “moisture flux” `F`.

For each climate cell:

* moisture source:

  * over ocean/lakes: `source = evap_ocean * f(temp)`
  * over land: `source = evap_land * SM_proxy` (or just smaller)

Perform 1–2 sweeps:

* one along primary wind direction
* optional second along seasonal/monsoon component

Per sweep cell update:

* `F += source`
* compute uplift:

  * `uplift = max(0, elev(next) - elev(curr))` along wind direction
* precipitation sink:

  * `sink = rainout_rate * F * (1 + orographic_factor * uplift)`
  * clamp sink <= F
* `P += sink`
* `F -= sink`

This creates:

* wet windward coasts
* drying inland
* rain shadows behind mountains
* stronger rainfall where uplift exists

### Step F — Evap demand

Compute an evap demand scalar for later moisture/plant modeling:

* `evap = clamp(kE * max(0, Teq) * (1 + kL * insolation_norm), 0, 1)`

### Step G — Store into `ClimateYearCache`

For each bin:

* `T_mean = Teq` (or relax an ocean heat reservoir if you want)
* `P_mean = P`
* `evap`, `wind`, `storminess`

## 2.4 Runtime day/night moving across the map

At runtime current time `t`:

* compute subsolar longitude from `rotation_period_s` and epoch
* for each tile/cell:

  * compute local hour angle `H = lon - subsolar_lon`
  * compute `cos_zenith = sinLat*sinδ + cosLat*cosδ*cosH`
  * `daylight = max(0, cos_zenith)`
    Use `daylight` to:
* modulate instantaneous temperature around `T_mean`
* modulate animal behavior (visibility, activity)

For speed, do this only for:

* active region
* or coarse grid with interpolation

---

# 3) Plant ecology model (vegetation density/type from environment)

Goal: cheap, continuous biomasses, climate-consistent, supports organism interactions.

## 3.1 State per tile (or vegetation grid)

Plant functional groups (PFGs):

* `G` grass/herbs
* `S` shrub
* `T` trees/canopy
* `M` moss/lichen

Plus soil moisture bucket `SM` and optional nutrients `N`.

```cpp
struct VegTile {
  float B_G, B_S, B_T, B_M; // biomass
  float SM;                 // soil moisture [0..1]
  float N;                  // fertility [0..1] (optional)
};
```

## 3.2 Traits (fixed PFG parameters)

```cpp
struct PFGTraits {
  float r;          // intrinsic growth rate
  float K;          // carrying capacity contribution scale
  float m0;         // baseline mortality
  float t_min, t_opt, t_max; // temp window
  float w_min, w_opt, w_max; // moisture window
  float shade_tol;  // 0..1 (1 = loves shade)
  float fire_res;   // 0..1
};
```

You define 4 trait sets (G,S,T,M) once.

## 3.3 Environmental inputs (per tile per time bin)

From climate cache and terrain:

* `temp = T_mean(bin)`
* `precip = P_mean(bin)`
* `evap_demand = evap(bin)`
* `elev`, `slope`, `soil_hold`, `soil_fertility`
* `water nearby` boosts local humidity/SM

## 3.4 Update loop per time step (daily or per bin)

### Step 0 — Update soil moisture bucket

Use precipitation and evap demand; include soil holding capacity and vegetation feedback.

```cpp
float infil = 0.8f; // tune
float evap_scale = 0.6f; // tune

SM += precip * infil;
SM -= evap_demand * evap_scale * (0.3f + 0.7f * vegetation_cover);
SM *= (0.98f + 0.02f * soil_hold);  // water holding slows loss
SM = clamp01(SM);
```

`vegetation_cover` can be derived from biomasses.

### Step 1 — Compute canopy/shade

```cpp
float canopy = clamp01(B_T / K_T);
float light  = 1.0f - canopy;
```

### Step 2 — Suitability functions

Define smooth window:

```cpp
inline float smooth_window(float x, float xmin, float xopt, float xmax) {
  // piecewise smoothstep triangle
  if (x <= xmin || x >= xmax) return 0.f;
  if (x < xopt) {
    float t = (x - xmin) / (xopt - xmin);
    return t * t * (3 - 2*t);
  } else {
    float t = (xmax - x) / (xmax - xopt);
    return t * t * (3 - 2*t);
  }
}
```

Per group i:

```cpp
S_temp = smooth_window(temp, t_min, t_opt, t_max);
S_wet  = smooth_window(SM,   w_min, w_opt, w_max);

// Light term (simple)
S_light =
  (i==G) ? light :
  (i==T) ? 1.0f :
  (i==S) ? (0.5f + 0.5f*light) :
           (0.3f + 0.7f*canopy); // M

S_nutr = lerp(0.6f, 1.0f, N);

S = S_temp * S_wet * S_light * S_nutr;
```

### Step 3 — Competition pressure and logistic growth

```cpp
float P = (B_G/K_G) + (B_S/K_S) + (B_T/K_T) + (B_M/K_M);
P = std::min(P, 2.0f); // clamp pressure

growth_i = r_i * S_i * B_i * (1 - P);
```

### Step 4 — Stress + baseline mortality

```cpp
float stress = aW * std::max(0.f, w_min - SM)
             + aT * std::max(0.f, t_min - temp)
             + bT * std::max(0.f, temp - t_max);

mort_i = (m0_i + stress) * B_i;
```

### Step 5 — Disturbance (fire/frost) driven by climate

Compute per tile:

```cpp
float fuel = clamp01((B_G + B_S) / (K_G + K_S));
float dryness = 1.0f - SM;
float hot = clamp01((temp - 10.f) / 20.f);

float p_fire = fire_base * hot * dryness * fuel;
if (rand01() < p_fire) {
  B_T *= (0.3f + 0.7f*fire_res_T);
  B_S *= (0.5f + 0.5f*fire_res_S);
  B_G *= 0.8f;
}
```

Frost kill example:

```cpp
if (temp < frost_thresh) {
  B_T *= 0.7f;
  B_S *= 0.85f;
}
```

### Step 6 — Spread / colonization (cheap diffusion)

Do per group occasionally (per bin) using neighbor average:

```cpp
B_i += spread_i * (avg_neighbor(B_i) - B_i);
```

### Step 7 — Apply update

```cpp
B_i += (growth_i - mort_i) * dt;
B_i = clamp(B_i, 0.f, K_i);
```

## 3.5 Derived fields for organism interaction

Compute per tile:

* **Food availability**:

  * grazers: `food = cG * B_G`
  * browsers: `food = cS * B_S`
  * fruit/seed pulses: `food += pulse(bin) * cT * B_T` (optional)
* **Cover/shelter**:

  * `cover = clamp01(a*B_G + b*B_S + c*B_T)`
* **Movement cost**:

  * `move_cost = base + kS*B_S + kT*B_T + kSlope*slope`
* **Visibility**:

  * `visibility = 1 - cover` (or more nuanced)
* **Microclimate**:

  * `temp_local = temp - canopy*ΔT_shade`
  * `evap_local = evap_demand * (1 - canopy*0.5)`

These feed directly into your organism energy/hazard models.

---

# Recommended integration order

1. **Generate terrain** (`TerrainTile[]`)
2. **Bake climate year cache** at coarse resolution using terrain:

   * `T_mean[bin]`, `P_mean[bin]`, `evap[bin]`, `wind[bin]`, `storminess[bin]`
3. **Initialize vegetation**:

   * set initial biomasses from a simple rule using `T_mean` and `P_mean`
4. **Run vegetation forward** for ~50–200 years (bins repeating) to reach equilibrium
5. Runtime:

   * use cached climate + day/night instantaneous modifier
   * update soil moisture + vegetation at bin cadence (or daily)
   * organisms query derived fields (food, cover, hazards)

---

If you want, I can turn this into a concrete file layout for Claude Code like:

* `terrain_gen.h/.cpp`
* `climate_cache.h/.cpp` (insolation + winds + moisture sweeps)
* `vegetation.h/.cpp` (PFG model)
* `world_fields.h` (SoA storage helpers)

…with recommended constants for an “Earth-like” default planet and tuning notes.
