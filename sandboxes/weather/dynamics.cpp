#include "dynamics.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numeric>
#include <queue>

namespace sandbox {

// ── Constants ───────────────────────────────────────────────────────────────

static constexpr float FREEZING_C = 0.0f;
static constexpr float SNOW_MELT_RATE = 0.08f;     // snow melted per day per degree above 0
static constexpr float RAIN_TO_SURFACE = 0.45f;    // fraction of precip that becomes surface water
static constexpr float EVAP_SURFACE_RATE = 0.01f;  // evaporation from surface water per day
static constexpr float EVAP_SOIL_RATE = 0.007f;    // evaporation from soil moisture per day
static constexpr float HUMIDITY_FEEDBACK = 0.02f;  // wet terrain contribution to local humidity
static constexpr float ARIDITY_EMA_RATE = 0.02f;   // EMA smoothing for aridity (~50-day window)
static constexpr float K_BASEFLOW = 0.01f;         // groundwater baseflow rate (slow, persistent)

// D8 neighbor offsets
static constexpr int DX8[] = {-1, 0, 1, -1, 1, -1, 0, 1};
static constexpr int DY8[] = {-1, -1, -1, 0, 0, 1, 1, 1};

// ── Surface water cap ───────────────────────────────────────────────────
// 1.0 on flat → 0.3 at slope 0.1 → ~0.2 at slope 0.2+ (steep terrain sheds water)
// At 50km/tile, even 1.0 represents a deep lake; most land holds far less.
static inline float surface_water_cap(float slope01) {
    float cap = 0.3f + std::max(0.0f, 0.7f - slope01 / 0.10f * 0.7f);
    if (slope01 > 0.12f) cap *= std::max(0.5f, 1.0f - (slope01 - 0.12f) * 3.0f);
    return cap;
}

// ── Weather pattern noise ───────────────────────────────────────────────
// Cheap hash-based noise for time-varying weather fronts. Returns [0, 1].
// The pattern drifts with wind so rain cells move across the map.

static float hash_noise(int x, int y, int t) {
    uint32_t h = static_cast<uint32_t>(x * 374761393 + y * 668265263 + t * 1274126177);
    h = (h ^ (h >> 13)) * 1103515245;
    h = h ^ (h >> 16);
    return static_cast<float>(h & 0xFFFF) / 65535.0f;
}

// Smooth noise: bilinear interpolation of hash values at integer grid points.
// cell_size controls the spatial scale of weather fronts (larger = bigger systems).
static float weather_noise(float wx, float wy, int time_step, int cell_size) {
    int cx = static_cast<int>(std::floor(wx / static_cast<float>(cell_size)));
    int cy = static_cast<int>(std::floor(wy / static_cast<float>(cell_size)));
    float fx = wx / static_cast<float>(cell_size) - static_cast<float>(cx);
    float fy = wy / static_cast<float>(cell_size) - static_cast<float>(cy);

    float n00 = hash_noise(cx, cy, time_step);
    float n10 = hash_noise(cx + 1, cy, time_step);
    float n01 = hash_noise(cx, cy + 1, time_step);
    float n11 = hash_noise(cx + 1, cy + 1, time_step);

    float nx0 = n00 + (n10 - n00) * fx;
    float nx1 = n01 + (n11 - n01) * fx;
    return nx0 + (nx1 - nx0) * fy;
}

// ── Init ────────────────────────────────────────────────────────────────────

void init_dynamics(DynamicState& state, const Terrain& world, const ClimateData& climate) {
    state.width = world.width;
    state.height = world.height;
    state.elapsed_days = 0.0f;

    size_t size = static_cast<size_t>(world.width) * world.height;
    state.tiles.resize(size);
    state.runoff_buf.resize(size, 0.0f);
    state.accum_discharge.resize(size, 0.0f);

    // Create hydrology elevation with micro-relief noise to produce real depressions.
    // The terrain DEM is too smooth at this resolution — most D8 "sinks" have near-zero depth.
    // Adding spatially correlated noise (amplitude ~0.004) creates basins deep enough for lakes.
    std::vector<float> elev_hydro(size);
    for (size_t i = 0; i < size; ++i) {
        float e = world.tiles[i].elev01;
        if (!world.tiles[i].is_ocean) {
            auto tx = static_cast<int>(i % world.width);
            auto ty = static_cast<int>(i / world.width);
            auto tile_hash = [](int x, int y, int seed) -> float {
                uint32_t h = static_cast<uint32_t>(x * 374761393 + y * 668265263 + seed);
                h = (h ^ (h >> 13)) * 1103515245;
                h = h ^ (h >> 16);
                return static_cast<float>(h & 0xFFFF) / 65535.0f - 0.5f;
            };
            auto bilinear = [&](int scale, int seed) -> float {
                int cx = tx / scale, cy = ty / scale;
                float fx = static_cast<float>(tx % scale) / static_cast<float>(scale);
                float fy = static_cast<float>(ty % scale) / static_cast<float>(scale);
                float n00 = tile_hash(cx, cy, seed);
                float n10 = tile_hash(cx + 1, cy, seed);
                float n01 = tile_hash(cx, cy + 1, seed);
                float n11 = tile_hash(cx + 1, cy + 1, seed);
                float nx0 = n00 + (n10 - n00) * fx;
                float nx1 = n01 + (n11 - n01) * fx;
                return nx0 + (nx1 - nx0) * fy;
            };
            // Two-octave noise: large (8-tile) for lake-scale basins + fine (3-tile) for detail
            float noise_large = bilinear(8, 7919);  // 8-tile wavelength → 10-50 tile basins
            float noise_fine = bilinear(3, 41077);  // 3-tile wavelength → fine depressions
            float noise = noise_large * 0.7f + noise_fine * 0.3f;
            // Scale by terrain flatness — flat areas get deeper depressions
            float flatness = std::max(0.0f, 1.0f - world.tiles[i].slope01 * 5.0f);
            e += noise * (0.003f + flatness * 0.009f);  // ±0.0015 steep → ±0.006 flat
        }
        elev_hydro[i] = e;
    }

    // Compute D8 steepest-descent directions using hydrology elevation
    static constexpr float SINK_TOLERANCE = 0.001f;  // lowered for better depression detection
    state.downhill.resize(size, -1);
    for (size_t i = 0; i < size; ++i) {
        if (world.tiles[i].is_ocean)
            continue;
        auto tx = static_cast<int>(i % world.width);
        auto ty = static_cast<int>(i / world.width);
        float best_drop = SINK_TOLERANCE;
        int32_t best_idx = -1;
        for (int d = 0; d < 8; ++d) {
            int nx = tx + DX8[d];
            int ny = ty + DY8[d];
            if (nx < 0 || nx >= static_cast<int>(world.width) || ny < 0 ||
                ny >= static_cast<int>(world.height))
                continue;
            size_t ni = static_cast<size_t>(ny) * world.width + static_cast<size_t>(nx);
            float dist = (DX8[d] != 0 && DY8[d] != 0) ? 1.414f : 1.0f;
            float drop = (elev_hydro[i] - elev_hydro[ni]) / dist;
            if (drop > best_drop) {
                best_drop = drop;
                best_idx = static_cast<int32_t>(ni);
            }
        }
        state.downhill[i] = best_idx;
    }

    // Topological sort: tiles ordered by hydrology elevation (descending) for flow accumulation.
    // Must use elev_hydro (not raw elev01) since D8 downhill was computed from elev_hydro.
    state.topo_order.resize(size);
    std::iota(state.topo_order.begin(), state.topo_order.end(), 0u);
    std::sort(state.topo_order.begin(), state.topo_order.end(),
              [&elev_hydro](uint32_t a, uint32_t b) { return elev_hydro[a] > elev_hydro[b]; });

    for (size_t i = 0; i < size; ++i) {
        const auto& tt = world.tiles[i];
        const auto& ct = climate.tiles[i];
        auto& dt = state.tiles[i];

        dt.surface_water = 0.0f;
        dt.snow_depth = 0.0f;
        dt.local_humidity = 0.0f;

        if (tt.is_ocean) {
            dt.soil_moisture = 1.0f;
            dt.surface_water = 1.0f;
            dt.aridity = 2.0f;  // ocean: fully humid
        } else {
            // Initialize soil moisture from climate moisture
            dt.soil_moisture = std::clamp(ct.moisture, 0.0f, 1.0f);
            // Seed aridity from static bake: precip / (evap + epsilon)
            dt.aridity = ct.precipitation / (ct.evaporation + 0.01f);
            dt.aridity = std::clamp(dt.aridity, 0.0f, 3.0f);
            // Cold areas start with some snow
            if (ct.temperature < FREEZING_C) {
                dt.snow_depth = std::clamp(-ct.temperature * 0.02f, 0.0f, 0.5f);
            }
        }
    }

    // ── Basin spillway computation for lakes ────────────────────────────
    // Build reverse graph and identify sink basins with spill elevations
    state.basin_id.assign(size, DynamicState::NO_BASIN);
    state.accum_quick.resize(size, 0.0f);
    state.accum_base.resize(size, 0.0f);

    // Reverse graph: which tiles drain into each tile via D8?
    std::vector<std::vector<uint32_t>> upstream(size);
    for (size_t i = 0; i < size; ++i) {
        if (state.downhill[i] >= 0)
            upstream[static_cast<size_t>(state.downhill[i])].push_back(static_cast<uint32_t>(i));
    }

    // Find all sink basins via BFS upstream from each sink
    uint16_t basin_count = 0;
    std::vector<float> spill_elevs, sink_elevs;
    std::vector<uint32_t> basin_areas, spill_tiles;
    std::vector<std::vector<uint32_t>> basin_tile_lists;

    for (size_t i = 0; i < size; ++i) {
        if (world.tiles[i].is_ocean || state.downhill[i] >= 0)
            continue;
        // This is a D8 sink — trace upstream
        if (basin_count >= DynamicState::NO_BASIN - 1)
            break;  // overflow protection: reserve NO_BASIN as sentinel
        uint16_t bid = basin_count++;

        std::queue<size_t> bq;
        bq.push(i);
        state.basin_id[i] = bid;
        float min_elev = elev_hydro[i];
        float spill = 1.0f;
        uint32_t spill_t = static_cast<uint32_t>(i);
        uint32_t area = 0;
        std::vector<uint32_t> btiles;

        while (!bq.empty()) {
            size_t cur = bq.front();
            bq.pop();
            btiles.push_back(static_cast<uint32_t>(cur));
            area++;
            if (elev_hydro[cur] < min_elev)
                min_elev = elev_hydro[cur];

            for (uint32_t up : upstream[cur]) {
                if (state.basin_id[up] == DynamicState::NO_BASIN && !world.tiles[up].is_ocean) {
                    state.basin_id[up] = bid;
                    bq.push(up);
                }
            }
        }

        // Find spill elevation: lowest neighboring tile NOT in this basin
        for (uint32_t bt : btiles) {
            auto bx = static_cast<int>(bt % world.width);
            auto by = static_cast<int>(bt / world.width);
            for (int d = 0; d < 8; ++d) {
                int nx = bx + DX8[d];
                int ny = by + DY8[d];
                if (nx < 0 || nx >= static_cast<int>(world.width) || ny < 0 ||
                    ny >= static_cast<int>(world.height))
                    continue;
                size_t ni = static_cast<size_t>(ny) * world.width + static_cast<size_t>(nx);
                if (state.basin_id[ni] != bid && !world.tiles[ni].is_ocean) {
                    if (elev_hydro[ni] < spill) {
                        spill = elev_hydro[ni];
                        spill_t = static_cast<uint32_t>(ni);
                    }
                }
            }
        }

        // If spill tile is still the sink itself, this is a closed basin (surrounded by
        // ocean/edges). Set spill elevation very high so spillway logic never triggers.
        if (spill_t == static_cast<uint32_t>(i))
            spill = 1e6f;

        spill_elevs.push_back(spill);
        sink_elevs.push_back(min_elev);
        basin_areas.push_back(area);
        spill_tiles.push_back(spill_t);
        basin_tile_lists.push_back(std::move(btiles));
    }

    state.num_basins = basin_count;
    state.basin_spill_elev = std::move(spill_elevs);
    state.basin_sink_elev = std::move(sink_elevs);
    state.basin_area = std::move(basin_areas);
    state.basin_spill_tile = std::move(spill_tiles);
    state.basin_tiles = std::move(basin_tile_lists);
    state.basin_volume.assign(basin_count, 0.0f);

    // Record initial storage for budget tracking
    state.budget = WaterBudget{};
    for (size_t i = 0; i < size; ++i) {
        if (!world.tiles[i].is_ocean) {
            const auto& dt = state.tiles[i];
            state.budget.initial_storage +=
                dt.surface_water + dt.soil_moisture + dt.groundwater + dt.snow_depth;
        }
    }
}

// ── Tick ────────────────────────────────────────────────────────────────────

void tick_dynamics(DynamicState& state, const Terrain& world, const ClimateData& climate,
                   float dt_seconds, const AtmosphereState* atmo) {
    if (state.paused)
        return;

    float dt_days = dt_seconds * state.time_scale * DAYS_PER_SECOND;
    state.elapsed_days += dt_days;

    uint32_t w = state.width;
    uint32_t h = state.height;
    size_t size = static_cast<size_t>(w) * h;

    // Zero runoff buffer (pre-allocated in DynamicState to avoid per-tick heap alloc)
    std::fill(state.runoff_buf.begin(), state.runoff_buf.end(), 0.0f);
    auto& runoff_in = state.runoff_buf;

    // Zero discharge seed buffers — quickflow (surface runoff) and baseflow (groundwater)
    auto& accum = state.accum_discharge;
    auto& aq = state.accum_quick;
    auto& ab = state.accum_base;
    std::fill(accum.begin(), accum.end(), 0.0f);
    std::fill(aq.begin(), aq.end(), 0.0f);
    std::fill(ab.begin(), ab.end(), 0.0f);

    // Weather front pattern: changes every ~2 days, drifts with average wind.
    // time_step controls how fast fronts change; cell_size controls front width.
    int time_step = static_cast<int>(state.elapsed_days * 0.5f);  // new pattern every 2 days
    constexpr int FRONT_SIZE = 24;  // tiles per weather cell — large fronts

    for (size_t i = 0; i < size; ++i) {
        const auto& tt = world.tiles[i];
        const auto& ct = climate.tiles[i];
        auto& dt = state.tiles[i];

        auto tx = static_cast<uint32_t>(i % w);
        auto ty = static_cast<uint32_t>(i / w);

        // Sample atmosphere if available, otherwise use static climate
        float tile_temp = ct.temperature;
        float tile_precip = ct.precipitation;
        float tile_wind_u = ct.wind_u;
        float tile_wind_v = ct.wind_v;

        if (atmo) {
            tile_temp = atmo->sample(tx, ty, &AtmosphereCell::T);
            tile_precip = atmo->sample(tx, ty, &AtmosphereCell::precip_rate);
            tile_wind_u = atmo->sample(tx, ty, &AtmosphereCell::u);
            tile_wind_v = atmo->sample(tx, ty, &AtmosphereCell::v);
        }

        // Skip ocean — stays saturated
        if (tt.is_ocean) {
            dt.surface_water = 1.0f;
            dt.soil_moisture = 1.0f;
            dt.effective_precip = tile_precip;
            dt.effective_moisture = 1.0f;

            if (atmo) {
                // Derive evaporation from atmosphere temperature + wind over water
                float wind_speed = std::sqrt(tile_wind_u * tile_wind_u + tile_wind_v * tile_wind_v);
                float temp_factor = std::clamp(tile_temp / 30.0f, 0.0f, 1.5f);
                dt.effective_evap =
                    std::clamp(temp_factor * (1.0f + wind_speed * 0.5f) * 0.3f, 0.0f, 1.0f);
                dt.effective_storm = atmo->sample(tx, ty, &AtmosphereCell::storminess);
                dt.local_humidity = std::clamp(dt.effective_evap * 0.8f, 0.0f, 1.0f);
            } else {
                dt.effective_evap = ct.evaporation;
                dt.effective_storm = ct.storminess;
                dt.local_humidity = std::clamp(ct.evaporation * 0.8f, 0.0f, 1.0f);
            }
            continue;
        }

        // ── 1. Precipitation ─────────────────────────────────────────────
        if (atmo) {
            // Atmosphere provides precipitation directly
            dt.effective_precip = std::clamp(tile_precip, 0.0f, 1.0f);
        } else {
            // Fallback: noise-modulated static precipitation
            float wx = static_cast<float>(tx) - tile_wind_u * state.elapsed_days * 0.3f;
            float wy = static_cast<float>(ty) - tile_wind_v * state.elapsed_days * 0.3f;

            float front =
                weather_noise(wx, wy, time_step, FRONT_SIZE) * 0.7f +
                weather_noise(wx * 2.1f, wy * 2.1f, time_step + 97, FRONT_SIZE / 2) * 0.3f;

            float threshold = 0.55f - tile_precip * 0.3f;
            float modulation = std::clamp((front - threshold) / (1.0f - threshold), 0.0f, 1.0f);

            float effective = tile_precip * modulation * 2.0f;
            dt.effective_precip = std::clamp(effective, 0.0f, 1.0f);
        }

        // Atmosphere precip_rate is already a calibrated daily rate (~0-0.4);
        // baked precipitation is a normalized annual index [0-1] that needs the 0.3 damper.
        float precip_amount =
            atmo ? dt.effective_precip * dt_days : dt.effective_precip * dt_days * 0.3f;

        state.budget.total_precip += precip_amount;

        if (tile_temp < FREEZING_C) {
            // Precipitation falls as snow
            dt.snow_depth += precip_amount;
        } else {
            // Rain: split between surface water and direct soil infiltration
            // Steeper slopes → more direct runoff (Horton overland flow)
            float slope_runoff = std::min(tt.slope01 * 0.5f, 0.15f);
            float surface_frac = RAIN_TO_SURFACE + slope_runoff;
            float to_surface = precip_amount * surface_frac;
            float to_soil = precip_amount * (1.0f - surface_frac);
            // Soil can only absorb up to field_capacity directly from rain.
            // Excess becomes saturation-excess overland flow → surface water.
            float soil_room = std::max(0.0f, tt.field_capacity - dt.soil_moisture);
            float actual_soil = std::min(to_soil, soil_room);
            dt.soil_moisture += actual_soil;
            dt.surface_water += to_surface + (to_soil - actual_soil);
        }

        // ── 2. Snowmelt ─────────────────────────────────────────────────
        if (dt.snow_depth > 0.0f && tile_temp > FREEZING_C) {
            float melt = std::min(dt.snow_depth, SNOW_MELT_RATE * tile_temp * dt_days);
            dt.snow_depth -= melt;
            dt.surface_water += melt;
            state.budget.total_snowmelt += melt;
        }

        // ── 3. Infiltration: surface → soil ──────────────────────────────
        // ksat (m/s) → normalize to fraction/day by dividing by soil_depth
        // so the rate is "fraction of standing water absorbed per day"
        // 0.80x scaling: allow most infiltration at natural ksat rate.
        // Real soils absorb precipitation quickly — surface water persists only
        // in depressions (managed by basin spillway) or when soil is saturated.
        float infil_rate =
            (tt.soil_depth > 0.01f) ? tt.ksat * 86400.0f / tt.soil_depth * 0.80f : 0.0f;
        float slope_factor = std::max(
            0.35f, 1.0f - std::pow(tt.slope01, 0.7f));  // superlinear: steep slopes infiltrate less
        float infiltration = infil_rate * slope_factor * dt_days;
        float remaining_capacity = tt.field_capacity - dt.soil_moisture;
        float actual_infil =
            std::min({infiltration, dt.surface_water, std::max(remaining_capacity, 0.0f)});
        dt.surface_water -= actual_infil;
        dt.soil_moisture += actual_infil;

        // Gravity drainage: soil above field capacity percolates to groundwater.
        // This is the primary path for soil → groundwater recharge (real hydrology:
        // field capacity is the maximum water held against gravity; excess drains).
        float gw_cap = tt.soil_depth * tt.porosity;  // max groundwater = saturated zone capacity
        if (dt.soil_moisture > tt.field_capacity && dt.groundwater < gw_cap) {
            float excess_soil = dt.soil_moisture - tt.field_capacity;
            // Drain at ksat rate, proportional to excess above field capacity
            float perc_rate = tt.ksat * 86400.0f / tt.soil_depth * tt.bedrock_permeability;
            float percolation = std::min(excess_soil, perc_rate * dt_days);
            percolation = std::min(percolation, gw_cap - dt.groundwater);
            dt.soil_moisture -= percolation;
            dt.groundwater += percolation;
            state.budget.total_gw_recharge += percolation;
        }

        // Excess infiltration from surface → groundwater (when soil is saturated)
        if (dt.surface_water > 0.0f && dt.soil_moisture >= tt.field_capacity &&
            dt.groundwater < gw_cap) {
            float gw_infil = std::min(dt.surface_water * 0.05f * tt.bedrock_permeability * dt_days,
                                      dt.surface_water);
            gw_infil = std::min(gw_infil, gw_cap - dt.groundwater);
            dt.surface_water -= gw_infil;
            dt.groundwater += gw_infil;
            state.budget.total_gw_recharge += gw_infil;
        }

        // ── 4. Evaporation ───────────────────────────────────────────────
        // Temperature and wind increase evaporation; humidity decreases it
        float temp_factor = std::clamp(tile_temp / 30.0f, 0.0f, 1.5f);
        float wind_speed = std::sqrt(tile_wind_u * tile_wind_u + tile_wind_v * tile_wind_v);
        float wind_factor = 1.0f + wind_speed * 0.5f;
        float humidity_suppress = 1.0f - dt.local_humidity * 0.4f;
        float evap_multiplier = temp_factor * wind_factor * humidity_suppress;

        // Evaporate from surface first, then soil
        // Thin films evaporate much faster (high surface-area:volume ratio at 50km tile scale).
        // Below 0.05 depth, evaporation rate scales inversely with depth — a 0.01 film
        // evaporates 5x faster than a 0.05 layer. This clears non-lake surface water
        // without affecting deep lake evaporation rates.
        float thin_film_boost = (dt.surface_water < 0.05f && dt.surface_water > 0.0f)
                                    ? std::max(1.0f, std::min(0.05f / (dt.surface_water + 0.001f), 8.0f))
                                    : 1.0f;
        float depth_boost = thin_film_boost;
        // PET caps total evapotranspiration (actual ET never exceeds potential ET)
        float pet_limit =
            (EVAP_SURFACE_RATE + EVAP_SOIL_RATE) * temp_factor * wind_factor * dt_days;

        float surface_evap =
            std::min(dt.surface_water, EVAP_SURFACE_RATE * evap_multiplier * depth_boost * dt_days);
        float evap_available = std::max(dt.soil_moisture - tt.wilting_point, 0.0f);
        float soil_evap = std::min(evap_available, EVAP_SOIL_RATE * evap_multiplier * dt_days);

        // Cap combined ET to PET
        float total_et = surface_evap + soil_evap;
        if (total_et > pet_limit && total_et > 0.0f) {
            float scale = pet_limit / total_et;
            surface_evap *= scale;
            soil_evap *= scale;
        }

        dt.surface_water -= surface_evap;
        state.budget.total_evap_surface += surface_evap;
        dt.soil_moisture -= soil_evap;
        state.budget.total_evap_soil += soil_evap;

        // ── 5. Runoff: WSE-based multi-neighbor flow ─────────────────────
        // Groundwater baseflow: slow persistent discharge based on slope + permeability
        if (dt.groundwater > 0.0f) {
            float baseflow = K_BASEFLOW * dt.groundwater * (0.2f + tt.slope01 * 0.8f) *
                             tt.bedrock_permeability * dt_days;
            baseflow = std::min(baseflow, dt.groundwater);
            dt.groundwater -= baseflow;
            dt.surface_water += baseflow;
            accum[i] += baseflow;
            ab[i] += baseflow;  // baseflow → slow discharge component
        }

        // Compute water surface elevation and distribute to lower-WSE neighbors
        float wse_i = tt.elev01 + dt.surface_water;
        float sum_dh = 0.0f;
        struct FlowTarget {
            size_t idx;
            float dh;
        };
        FlowTarget targets[8];
        int ntargets = 0;

        float ocean_drain = 0.0f;
        for (int d = 0; d < 8; ++d) {
            int nx = static_cast<int>(tx) + DX8[d];
            int ny = static_cast<int>(ty) + DY8[d];
            if (nx < 0 || nx >= static_cast<int>(w) || ny < 0 || ny >= static_cast<int>(h))
                continue;
            size_t ni = static_cast<size_t>(ny) * w + static_cast<size_t>(nx);
            // Ocean neighbors drain water but don't participate in land flow routing
            if (world.tiles[ni].is_ocean) {
                float dh = wse_i - world.tiles[ni].elev01;
                if (dh > 0.0f)
                    ocean_drain += dh;
                continue;
            }
            float wse_n = world.tiles[ni].elev01 + state.tiles[ni].surface_water;
            float dh = wse_i - wse_n;
            // Minimum WSE gradient to route flow — lower threshold allows better drainage
            if (dh > 0.001f) {
                targets[ntargets++] = {ni, dh};
                sum_dh += dh;
            }
        }

        // Drain to ocean (coastal sink) — balanced for wetlands without ponding
        if (ocean_drain > 0.0f) {
            float K_OUT = 0.30f;
            float effective_k = std::min(K_OUT * dt_days, 0.5f);
            float drain = std::min(dt.surface_water * 0.8f, effective_k * ocean_drain);
            dt.surface_water -= drain;
            state.budget.total_ocean_drain += drain;
            state.budget.total_coastal_drain += drain;
            accum[i] += drain;
            aq[i] += drain;  // coastal drain → fast discharge component
        }

        if (ntargets > 0 && sum_dh > 0.0f) {
            // Steeper terrain routes faster (mountains drain, lowlands hold)
            float K_OUT = 0.15f + std::clamp((tt.slope01 - 0.08f) * 2.0f, 0.0f, 0.25f);
            float effective_k = std::min(K_OUT * dt_days, 0.5f);  // CFL-like cap
            float max_out = dt.surface_water * 0.8f;
            float out = std::min(max_out, effective_k * sum_dh);
            for (int t = 0; t < ntargets; ++t) {
                float flux = out * (targets[t].dh / sum_dh);
                runoff_in[targets[t].idx] += flux;
            }
            dt.surface_water -= out;
            accum[i] += out;
            aq[i] += out;  // surface runoff → fast discharge component
        }

        // ── 6. Terrain feedback: evaporation → local humidity ────────────
        float total_evap = surface_evap + soil_evap;
        dt.local_humidity =
            std::clamp(dt.local_humidity * 0.95f + total_evap * HUMIDITY_FEEDBACK, 0.0f, 1.0f);

        // Water conservation: excess surface water infiltrates soil/groundwater; remainder is runoff
        float max_sw = surface_water_cap(tt.slope01);
        if (dt.surface_water > max_sw) {
            float overflow = dt.surface_water - max_sw;
            // Route half of overflow to groundwater (subsurface absorption)
            float gw_cap2 = gw_cap;
            float gw_absorb = std::min(overflow * 0.5f, std::max(0.0f, gw_cap2 - dt.groundwater));
            dt.groundwater += gw_absorb;
            state.budget.total_gw_recharge += gw_absorb;
            overflow -= gw_absorb;
            float absorbed = std::max(0.0f, std::min(overflow, 1.0f - dt.soil_moisture));
            dt.soil_moisture += absorbed;
            float flash_runoff = overflow - absorbed;
            // Route overflow to D8 downstream neighbor instead of deleting
            if (flash_runoff > 0.0f) {
                int32_t ds = state.downhill[i];
                if (ds >= 0 && !world.tiles[static_cast<size_t>(ds)].is_ocean) {
                    runoff_in[static_cast<size_t>(ds)] += flash_runoff;
                } else {
                    state.budget.total_ocean_drain += flash_runoff;
                }
                state.budget.total_overflow += flash_runoff;
                aq[i] += flash_runoff;
                accum[i] += flash_runoff;
            }
            dt.surface_water = max_sw;
        }
        dt.surface_water = std::max(dt.surface_water, 0.0f);
        dt.soil_moisture = std::clamp(dt.soil_moisture, 0.0f, 1.0f);
        dt.snow_depth = std::max(dt.snow_depth, 0.0f);

        // ── 7. Derived overlay values ────────────────────────────────────
        // Effective moisture: combines soil + surface + local humidity
        dt.effective_moisture = std::clamp(
            dt.soil_moisture * 0.6f + dt.surface_water * 0.25f + dt.local_humidity * 0.15f, 0.0f,
            1.0f);

        // Effective evaporation: normalize total evap to [0..1] range
        dt.effective_evap = std::clamp(total_evap / (dt_days * 0.15f + 0.001f), 0.0f, 1.0f);

        // ── 8. Potential evapotranspiration and aridity index ──────────
        // PET = evaporation demand assuming unlimited water supply
        // Uses temperature + wind + solar (via latitude) — no humidity suppression
        float pet_rate = (EVAP_SURFACE_RATE + EVAP_SOIL_RATE) * temp_factor * wind_factor;
        dt.pet = std::clamp(pet_rate, 0.0f, 1.0f);

        // Instantaneous aridity index: precip / PET
        float instant_aridity = dt.effective_precip / (pet_rate * dt_days + 0.001f);
        instant_aridity = std::min(instant_aridity, 3.0f);  // cap outliers

        // EMA smoothing: represents seasonal moisture balance, not daily weather
        float alpha = std::min(ARIDITY_EMA_RATE * dt_days, 0.2f);
        dt.aridity = dt.aridity * (1.0f - alpha) + instant_aridity * alpha;

        // Storminess: use atmosphere storm metric when available, else approximate
        if (atmo) {
            dt.effective_storm = atmo->sample(tx, ty, &AtmosphereCell::storminess);
        } else {
            float precip_intensity = dt.effective_precip;
            float wind_storm = std::clamp(wind_speed * 0.8f, 0.0f, 1.0f);
            dt.effective_storm = std::clamp(
                precip_intensity * 0.6f + wind_storm * 0.3f + ct.storminess * 0.1f, 0.0f, 1.0f);
        }
    }

    // Apply accumulated runoff (WSE flow receives)
    for (size_t i = 0; i < size; ++i) {
        if (runoff_in[i] > 0.0f) {
            state.tiles[i].surface_water += runoff_in[i];
            // Smooth lake depth cap (skip basin tiles — they use spillway logic)
            if (state.basin_id[i] == DynamicState::NO_BASIN) {
                float max_depth = surface_water_cap(world.tiles[i].slope01);
                if (state.tiles[i].surface_water > max_depth) {
                    float overflow = state.tiles[i].surface_water - max_depth;
                    float absorbed =
                        std::max(0.0f, std::min(overflow, 1.0f - state.tiles[i].soil_moisture));
                    state.tiles[i].soil_moisture =
                        std::min(state.tiles[i].soil_moisture + absorbed, 1.0f);
                    float flash_runoff = overflow - absorbed;
                    // Route to D8 downstream (mirrors per-tile overflow logic)
                    if (flash_runoff > 0.0f) {
                        int32_t ds = state.downhill[i];
                        if (ds >= 0 && !world.tiles[static_cast<size_t>(ds)].is_ocean) {
                            state.tiles[static_cast<size_t>(ds)].surface_water += flash_runoff;
                        } else {
                            state.budget.total_ocean_drain += flash_runoff;
                        }
                        state.budget.total_overflow += flash_runoff;
                    }
                    state.tiles[i].surface_water = max_depth;
                }
            }
        }
    }

    // ── Basin spillway logic: aggregate surface water, enforce spill elevation ──
    // For each sink basin, collect water and only allow outflow when WSE > spill level.
    // This creates persistent lakes in terrain depressions.
    if (state.num_basins > 0) {
        // Aggregate surface water into basin volumes
        for (uint16_t bi = 0; bi < state.num_basins; ++bi)
            state.basin_volume[bi] = 0.0f;

        for (size_t i = 0; i < size; ++i) {
            uint16_t bid = state.basin_id[i];
            if (bid == DynamicState::NO_BASIN || world.tiles[i].is_ocean)
                continue;
            state.basin_volume[bid] += state.tiles[i].surface_water;
        }

        // Compute basin WSE and spill excess
        // Only manage basins with meaningful depression depth (>=0.005)
        // Shallower basins use normal WSE routing — prevents thousands of
        // micro-basins from trapping water as fake "lakes"
        for (uint16_t bi = 0; bi < state.num_basins; ++bi) {
            if (state.basin_area[bi] == 0)
                continue;
            float depression_depth = state.basin_spill_elev[bi] - state.basin_sink_elev[bi];
            if (depression_depth < 0.005f)
                continue;  // too shallow for spillway management

            float area_f = static_cast<float>(state.basin_area[bi]);
            float lake_depth = state.basin_volume[bi] / area_f;
            float basin_wse = state.basin_sink_elev[bi] + lake_depth;
            float spill = state.basin_spill_elev[bi];

            if (basin_wse > spill) {
                // Spill excess water: drain proportionally from basin tiles
                float excess_depth = basin_wse - spill;
                float excess_volume = excess_depth * area_f;
                // Rate-limit: spill at most 80% of excess per tick for stability
                excess_volume *= 0.8f;
                // Safety: don't drain more than we have
                excess_volume = std::min(excess_volume, state.basin_volume[bi] * 0.9f);
                float drain_frac = excess_volume / (state.basin_volume[bi] + 0.0001f);
                drain_frac = std::min(drain_frac, 0.9f);
                double total_drained = 0;
                for (uint32_t ti : state.basin_tiles[bi]) {
                    auto idx = static_cast<size_t>(ti);
                    if (!world.tiles[idx].is_ocean) {
                        float drained = state.tiles[idx].surface_water * drain_frac;
                        state.tiles[idx].surface_water -= drained;
                        total_drained += drained;
                    }
                }
                // Route spill to downstream — add directly to spill tile surface water
                auto st = static_cast<size_t>(state.basin_spill_tile[bi]);
                auto td = static_cast<float>(total_drained);
                if (!world.tiles[st].is_ocean) {
                    state.tiles[st].surface_water += td;
                    // Apply capacity check on spill tile to prevent unrealistic spikes
                    float max_sw = surface_water_cap(world.tiles[st].slope01);
                    if (state.tiles[st].surface_water > max_sw) {
                        float spill_overflow = state.tiles[st].surface_water - max_sw;
                        state.tiles[st].surface_water = max_sw;
                        // Route spill overflow further downstream
                        int32_t ds = state.downhill[st];
                        if (ds >= 0 && !world.tiles[static_cast<size_t>(ds)].is_ocean) {
                            state.tiles[static_cast<size_t>(ds)].surface_water += spill_overflow;
                        } else {
                            state.budget.total_ocean_drain += spill_overflow;
                        }
                        state.budget.total_overflow += spill_overflow;
                    }
                    aq[st] += td;
                    accum[st] += td;
                } else {
                    state.budget.total_ocean_drain += total_drained;
                    state.budget.total_coastal_drain += total_drained;
                }
            }
        }
    }

    // ── D8 flow accumulation ──────────────────────────────────────────────
    // Track raw accum total before headwater injection
    for (size_t i = 0; i < size; ++i) {
        if (!world.tiles[i].is_ocean)
            state.budget.total_accum_raw += accum[i];
    }

    // Headwater minimum: high-elevation tiles get a fixed contribution
    for (size_t i = 0; i < size; ++i) {
        if (world.tiles[i].is_ocean)
            continue;
        float elev_frac = world.tiles[i].elev01;
        if (elev_frac > 0.55f) {
            float headwater_strength = std::min((elev_frac - 0.55f) / 0.25f, 1.0f);
            float min_contrib = 0.005f * headwater_strength * dt_days;
            float precip_contrib =
                state.tiles[i].effective_precip * 0.3f * headwater_strength * dt_days;
            float old_accum = accum[i];
            accum[i] = std::max(accum[i], min_contrib + precip_contrib);
            aq[i] = std::max(aq[i], min_contrib + precip_contrib);
            float added = accum[i] - old_accum;
            state.budget.total_headwater += added;
        }
    }

    // Propagate quickflow and baseflow downstream along D8 steepest-descent paths
    for (uint32_t idx : state.topo_order) {
        int32_t downstream = state.downhill[idx];
        if (downstream >= 0) {
            auto ds = static_cast<size_t>(downstream);
            if (!world.tiles[ds].is_ocean) {
                aq[ds] += aq[idx];
                ab[ds] += ab[idx];
            }
        }
    }

    // ── Two-reservoir discharge with time-constant EMA ───────────────────
    // Quickflow: fast response (tau ~0.3 days = ~7 hours) — storm pulses
    // Baseflow:  slow response (tau ~10 days) — perennial groundwater-fed flow
    static constexpr float TAU_FAST = 0.3f;   // days
    static constexpr float TAU_SLOW = 10.0f;  // days
    float a_fast = 1.0f - std::exp(-dt_days / TAU_FAST);
    float a_slow = 1.0f - std::exp(-dt_days / TAU_SLOW);

    for (size_t i = 0; i < size; ++i) {
        auto& dt = state.tiles[i];
        dt.quickflow += (aq[i] - dt.quickflow) * a_fast;
        dt.baseflow_d += (ab[i] - dt.baseflow_d) * a_slow;
        dt.discharge = dt.quickflow + dt.baseflow_d;
    }
}

// ── Statistics ──────────────────────────────────────────────────────────────

DynamicStats compute_dynamic_stats(const DynamicState& state, const Terrain& world) {
    DynamicStats stats{};
    size_t size = state.tiles.size();
    if (size == 0)
        return stats;

    uint32_t land_count = 0;
    double sw_sum = 0, sm_sum = 0, snow_sum = 0, aridity_sum = 0, pet_sum = 0;
    double gw_sum = 0, discharge_sum = 0;
    stats.aridity_min = 999.0f;

    for (size_t i = 0; i < size; ++i) {
        const auto& tt = world.tiles[i];
        if (tt.is_ocean)
            continue;

        const auto& dt = state.tiles[i];
        land_count++;

        sw_sum += dt.surface_water;
        sm_sum += dt.soil_moisture;
        snow_sum += dt.snow_depth;
        aridity_sum += dt.aridity;
        pet_sum += dt.pet;
        gw_sum += dt.groundwater;
        discharge_sum += dt.discharge;

        stats.surface_water_max = std::max(stats.surface_water_max, dt.surface_water);
        stats.soil_moisture_max = std::max(stats.soil_moisture_max, dt.soil_moisture);
        stats.snow_max = std::max(stats.snow_max, dt.snow_depth);
        stats.aridity_min = std::min(stats.aridity_min, dt.aridity);
        stats.aridity_max = std::max(stats.aridity_max, dt.aridity);
        stats.groundwater_max = std::max(stats.groundwater_max, dt.groundwater);
        stats.discharge_max = std::max(stats.discharge_max, dt.discharge);

        if (dt.snow_depth > 0.01f)
            ++stats.snow_tiles;
        if (dt.surface_water > 0.3f)
            ++stats.flooded_tiles;
        if (dt.aridity < 0.2f)
            ++stats.arid_tiles;
        if (dt.aridity > 1.0f)
            ++stats.humid_tiles;
    }

    if (land_count > 0) {
        auto n = static_cast<float>(land_count);
        stats.surface_water_mean = static_cast<float>(sw_sum / n);
        stats.soil_moisture_mean = static_cast<float>(sm_sum / n);
        stats.snow_mean = static_cast<float>(snow_sum / n);
        stats.aridity_mean = static_cast<float>(aridity_sum / n);
        stats.pet_mean = static_cast<float>(pet_sum / n);
        stats.groundwater_mean = static_cast<float>(gw_sum / n);
        stats.discharge_mean = static_cast<float>(discharge_sum / n);
    } else {
        stats.aridity_min = 0.0f;
    }

    return stats;
}

// ── Hydrology diagnostics ────────────────────────────────────────────────

void print_hydrology_diagnostics(const DynamicState& state, const Terrain& world) {
    size_t size = state.tiles.size();
    uint32_t w = state.width;
    uint32_t h = state.height;

    // Collect per-compartment values for land tiles
    std::vector<float> sw_vals, sm_vals, gw_vals, discharge_vals;
    double sw_total = 0, sm_total = 0, gw_total = 0, snow_total = 0;
    uint32_t land_count = 0;

    for (size_t i = 0; i < size; ++i) {
        if (world.tiles[i].is_ocean)
            continue;
        const auto& dt = state.tiles[i];
        land_count++;
        sw_vals.push_back(dt.surface_water);
        sm_vals.push_back(dt.soil_moisture);
        gw_vals.push_back(dt.groundwater);
        discharge_vals.push_back(dt.discharge);
        sw_total += dt.surface_water;
        sm_total += dt.soil_moisture;
        gw_total += dt.groundwater;
        snow_total += dt.snow_depth;
    }

    if (land_count == 0)
        return;

    // Sort for percentiles
    auto percentile = [](std::vector<float>& v, float p) -> float {
        if (v.empty())
            return 0.0f;
        size_t idx = static_cast<size_t>(p * static_cast<float>(v.size() - 1));
        return v[std::min(idx, v.size() - 1)];
    };
    std::sort(sw_vals.begin(), sw_vals.end());
    std::sort(sm_vals.begin(), sm_vals.end());
    std::sort(gw_vals.begin(), gw_vals.end());
    std::sort(discharge_vals.begin(), discharge_vals.end());

    const auto& b = state.budget;
    double final_storage = sw_total + sm_total + gw_total + snow_total;
    double delta_storage = final_storage - b.initial_storage;
    double total_evap = b.total_evap_surface + b.total_evap_soil;

    printf("\n=== HYDROLOGY DIAGNOSTICS (%u land tiles, %.0f days) ===\n", land_count,
           state.elapsed_days);

    // 1. Water budget
    printf("\n--- WATER BUDGET (cumulative) ---\n");
    printf("  Precipitation total:  %12.1f\n", b.total_precip);
    printf("  Evaporation surface:  %12.1f\n", b.total_evap_surface);
    printf("  Evaporation soil:     %12.1f\n", b.total_evap_soil);
    printf("  Evaporation total:    %12.1f  (%.1f%% of precip)\n", total_evap,
           b.total_precip > 0 ? total_evap / b.total_precip * 100.0 : 0.0);
    printf("  Ocean drain:          %12.1f  (%.1f%% of precip)\n", b.total_ocean_drain,
           b.total_precip > 0 ? b.total_ocean_drain / b.total_precip * 100.0 : 0.0);
    printf("  GW recharge:          %12.1f  (%.1f%% of precip)\n", b.total_gw_recharge,
           b.total_precip > 0 ? b.total_gw_recharge / b.total_precip * 100.0 : 0.0);
    printf("  Snowmelt:             %12.1f\n", b.total_snowmelt);
    printf("  Delta storage:        %12.1f  (%.1f%% of precip)\n", delta_storage,
           b.total_precip > 0 ? delta_storage / b.total_precip * 100.0 : 0.0);
    double residual = b.total_precip - total_evap - b.total_ocean_drain - delta_storage;
    printf("  Residual (leak):      %12.1f  (%.2f%% of precip)\n", residual,
           b.total_precip > 0 ? residual / b.total_precip * 100.0 : 0.0);

    // 2. Storage distributions
    printf("\n--- STORAGE DISTRIBUTIONS (end of run) ---\n");
    printf("  %-15s  %8s %8s %8s %8s %8s %8s %8s\n", "Compartment", "min", "25th", "median", "75th",
           "95th", "99th", "max");
    auto print_dist = [&](const char* name, std::vector<float>& v) {
        printf("  %-15s  %8.4f %8.4f %8.4f %8.4f %8.4f %8.4f %8.4f\n", name, v.front(),
               percentile(v, 0.25f), percentile(v, 0.50f), percentile(v, 0.75f),
               percentile(v, 0.95f), percentile(v, 0.99f), v.back());
    };
    print_dist("surface_water", sw_vals);
    print_dist("soil_moisture", sm_vals);
    print_dist("groundwater", gw_vals);
    print_dist("discharge", discharge_vals);

    // 3. Discharge thresholds
    printf("\n--- DISCHARGE NETWORK ---\n");
    float thresholds[] = {0.01f, 0.05f, 0.1f, 0.5f, 1.0f, 2.0f, 5.0f};
    for (float t : thresholds) {
        uint32_t count = 0;
        for (float d : discharge_vals)
            if (d >= t)
                count++;
        printf("  discharge >= %5.2f: %6u tiles  (%.2f%% of land)\n", t, count,
               100.0f * static_cast<float>(count) / static_cast<float>(land_count));
    }

    // 4. Discharge log-histogram
    printf("\n--- DISCHARGE LOG-HISTOGRAM ---\n");
    int bins[12] = {};  // -4 to 1.5 in 0.5 steps
    for (float d : discharge_vals) {
        if (d < 1e-4f) {
            bins[0]++;
            continue;
        }
        float logd = std::log10(d);
        int bin = static_cast<int>((logd + 4.0f) * 2.0f);  // -4→0, -3.5→1, ..., 1.5→11
        bin = std::clamp(bin, 0, 11);
        bins[bin]++;
    }
    const char* bin_labels[] = {"<-4",     "-4:-3.5", "-3.5:-3", "-3:-2.5", "-2.5:-2", "-2:-1.5",
                                "-1.5:-1", "-1:-0.5", "-0.5:0",  "0:0.5",   "0.5:1",   ">1"};
    for (int b2 = 0; b2 < 12; ++b2) {
        int bar = bins[b2] * 50 / static_cast<int>(land_count + 1);
        printf("  log10 %-8s: %6d  ", bin_labels[b2], bins[b2]);
        for (int j = 0; j < bar; ++j)
            printf("#");
        printf("\n");
    }

    // 5. Lake/flooding diagnostics — connected component analysis
    printf("\n--- LAKE DIAGNOSTICS ---\n");
    uint32_t flooded_count = 0;
    float flooded_depth_sum = 0;
    std::vector<bool> visited(size, false);
    std::vector<uint32_t> lake_sizes;

    for (size_t i = 0; i < size; ++i) {
        if (world.tiles[i].is_ocean || state.tiles[i].surface_water < 0.3f || visited[i])
            continue;
        // BFS to find connected flooded region
        std::queue<size_t> q;
        q.push(i);
        visited[i] = true;
        uint32_t lake_size = 0;
        while (!q.empty()) {
            size_t cur = q.front();
            q.pop();
            lake_size++;
            flooded_count++;
            flooded_depth_sum += state.tiles[cur].surface_water;
            auto cx = static_cast<int>(cur % w);
            auto cy = static_cast<int>(cur / w);
            for (int d = 0; d < 8; ++d) {
                int nx2 = cx + DX8[d];
                int ny2 = cy + DY8[d];
                if (nx2 < 0 || nx2 >= static_cast<int>(w) || ny2 < 0 || ny2 >= static_cast<int>(h))
                    continue;
                size_t ni = static_cast<size_t>(ny2) * w + static_cast<size_t>(nx2);
                if (!visited[ni] && !world.tiles[ni].is_ocean &&
                    state.tiles[ni].surface_water >= 0.3f) {
                    visited[ni] = true;
                    q.push(ni);
                }
            }
        }
        lake_sizes.push_back(lake_size);
    }
    std::sort(lake_sizes.begin(), lake_sizes.end(), std::greater<>());

    printf("  Flooded tiles (sw>=0.3): %u\n", flooded_count);
    printf("  Mean flooded depth: %.3f\n",
           flooded_count > 0 ? flooded_depth_sum / static_cast<float>(flooded_count) : 0.0f);
    printf("  Number of lakes: %zu\n", lake_sizes.size());
    uint32_t lakes_gt10 = 0, lakes_gt50 = 0;
    for (uint32_t ls : lake_sizes) {
        if (ls > 10)
            ++lakes_gt10;
        if (ls > 50)
            ++lakes_gt50;
    }
    printf("  Lakes > 10 tiles: %u\n", lakes_gt10);
    printf("  Lakes > 50 tiles: %u\n", lakes_gt50);
    if (!lake_sizes.empty()) {
        printf("  Largest lake: %u tiles\n", lake_sizes[0]);
        printf("  Lake size histogram: ");
        uint32_t s1 = 0, s2_4 = 0, s5_10 = 0, s11_50 = 0, s_gt50 = 0;
        for (uint32_t ls : lake_sizes) {
            if (ls == 1)
                ++s1;
            else if (ls <= 4)
                ++s2_4;
            else if (ls <= 10)
                ++s5_10;
            else if (ls <= 50)
                ++s11_50;
            else
                ++s_gt50;
        }
        printf("1:%u  2-4:%u  5-10:%u  11-50:%u  >50:%u\n", s1, s2_4, s5_10, s11_50, s_gt50);
    }

    // 5b. Terrain sink diagnostics
    uint32_t num_sinks = 0;
    for (size_t i = 0; i < size; ++i) {
        if (!world.tiles[i].is_ocean && state.downhill[i] < 0)
            ++num_sinks;
    }
    printf("  D8 sinks (local minima): %u (%.1f%% of land)\n", num_sinks,
           100.0f * static_cast<float>(num_sinks) / static_cast<float>(land_count));

    // 5c. Sink basin analysis: depression depth and area
    // For each D8 sink, trace its basin (all tiles flowing to it) and compute spill height
    printf("\n--- SINK BASIN ANALYSIS ---\n");
    // Build reverse graph: for each tile, which tiles flow into it?
    std::vector<std::vector<uint32_t>> upstream(size);
    for (size_t i = 0; i < size; ++i) {
        if (state.downhill[i] >= 0)
            upstream[static_cast<size_t>(state.downhill[i])].push_back(static_cast<uint32_t>(i));
    }

    // BFS from each sink to find its basin
    struct BasinInfo {
        uint32_t area;
        float sink_elev;
        float spill_elev;
        float depth;         // spill - sink
        float max_sw;        // max surface_water in basin
        float mean_sw;       // mean surface_water in basin
        uint32_t wet_tiles;  // tiles with sw > 0.05
    };
    std::vector<BasinInfo> basins;
    std::vector<int32_t> basin_id(size, -1);  // which basin each tile belongs to

    for (size_t i = 0; i < size; ++i) {
        if (world.tiles[i].is_ocean || state.downhill[i] >= 0)
            continue;
        // This is a sink — trace upstream to find basin
        std::queue<size_t> bq;
        bq.push(i);
        basin_id[i] = static_cast<int32_t>(basins.size());
        BasinInfo bi{};
        bi.sink_elev = world.tiles[i].elev01;
        bi.spill_elev = 1.0f;  // will find minimum boundary elevation
        float sw_sum_basin = 0;
        uint32_t wet_count = 0;

        std::vector<size_t> basin_tiles;
        while (!bq.empty()) {
            size_t cur = bq.front();
            bq.pop();
            basin_tiles.push_back(cur);
            bi.area++;
            float sw = state.tiles[cur].surface_water;
            sw_sum_basin += sw;
            bi.max_sw = std::max(bi.max_sw, sw);
            if (sw > 0.05f)
                wet_count++;

            for (uint32_t up : upstream[cur]) {
                if (basin_id[up] < 0 && !world.tiles[up].is_ocean) {
                    basin_id[up] = basin_id[i];
                    bq.push(up);
                }
            }
        }

        // Find spill elevation: minimum elevation on basin boundary
        // (tiles adjacent to basin that are NOT in the basin)
        for (size_t bt : basin_tiles) {
            auto bx = static_cast<int>(bt % w);
            auto by = static_cast<int>(bt / w);
            for (int d = 0; d < 8; ++d) {
                int nx2 = bx + DX8[d];
                int ny2 = by + DY8[d];
                if (nx2 < 0 || nx2 >= static_cast<int>(w) || ny2 < 0 || ny2 >= static_cast<int>(h))
                    continue;
                size_t ni = static_cast<size_t>(ny2) * w + static_cast<size_t>(nx2);
                if (basin_id[ni] != basin_id[i] && !world.tiles[ni].is_ocean) {
                    bi.spill_elev = std::min(bi.spill_elev, world.tiles[ni].elev01);
                }
            }
        }

        bi.depth = bi.spill_elev - bi.sink_elev;
        bi.mean_sw = bi.area > 0 ? sw_sum_basin / static_cast<float>(bi.area) : 0;
        bi.wet_tiles = wet_count;
        basins.push_back(bi);
    }

    // Sort basins by area descending
    std::sort(basins.begin(), basins.end(),
              [](const BasinInfo& x, const BasinInfo& y) { return x.area > y.area; });

    printf("  Total sink basins: %zu\n", basins.size());
    if (!basins.empty()) {
        // Depression depth distribution
        std::vector<float> depths, areas;
        uint32_t shallow_count = 0, deep_count = 0;
        for (const auto& bi : basins) {
            depths.push_back(bi.depth);
            areas.push_back(static_cast<float>(bi.area));
            if (bi.depth < 0.005f)
                shallow_count++;
            if (bi.depth >= 0.02f)
                deep_count++;
        }
        std::sort(depths.begin(), depths.end());
        std::sort(areas.begin(), areas.end());

        printf("  Depression depth: p50=%.4f p90=%.4f p99=%.4f max=%.4f\n",
               percentile(depths, 0.5f), percentile(depths, 0.9f), percentile(depths, 0.99f),
               depths.back());
        printf("  Basin area: p50=%.0f p90=%.0f p99=%.0f max=%.0f\n", percentile(areas, 0.5f),
               percentile(areas, 0.9f), percentile(areas, 0.99f), areas.back());
        printf("  Shallow basins (depth<0.005): %u (%.1f%%)\n", shallow_count,
               100.0f * static_cast<float>(shallow_count) / static_cast<float>(basins.size()));
        printf("  Deep basins (depth>=0.02): %u (%.1f%%)\n", deep_count,
               100.0f * static_cast<float>(deep_count) / static_cast<float>(basins.size()));

        // Wet basin status
        uint32_t basins_with_water = 0;
        for (const auto& bi : basins) {
            if (bi.wet_tiles > 0)
                basins_with_water++;
        }
        printf("  Basins with water (sw>0.05): %u of %zu\n", basins_with_water, basins.size());

        // Top 10 largest basins detail
        printf("\n  Top basins (by area):\n");
        printf("  %6s %8s %8s %8s %8s %8s %6s\n", "area", "sink_el", "spill_el", "depth", "max_sw",
               "mean_sw", "wet");
        for (size_t b2 = 0; b2 < std::min(basins.size(), size_t(10)); ++b2) {
            const auto& bi = basins[b2];
            printf("  %6u %8.4f %8.4f %8.4f %8.4f %8.4f %6u\n", bi.area, bi.sink_elev,
                   bi.spill_elev, bi.depth, bi.max_sw, bi.mean_sw, bi.wet_tiles);
        }
    }

    // 5d. Headwater vs natural runoff contribution
    printf("\n--- HEADWATER CONTRIBUTION ---\n");
    printf("  Raw accum total (before headwater): %.1f\n", b.total_accum_raw);
    printf("  Headwater injection total: %.1f\n", b.total_headwater);
    double total_accum = b.total_accum_raw + b.total_headwater;
    printf("  Headwater fraction: %.1f%%\n",
           total_accum > 0 ? b.total_headwater / total_accum * 100.0 : 0.0);

    // 5e. Ocean drain split
    printf("\n--- OCEAN DRAIN SPLIT ---\n");
    printf("  Coastal drain (WSE to ocean): %.1f (%.1f%% of precip)\n", b.total_coastal_drain,
           b.total_precip > 0 ? b.total_coastal_drain / b.total_precip * 100.0 : 0.0);
    printf("  Overflow cap (flash runoff):  %.1f (%.1f%% of precip)\n", b.total_overflow,
           b.total_precip > 0 ? b.total_overflow / b.total_precip * 100.0 : 0.0);

    // 6. Discharge by elevation band
    printf("\n--- DISCHARGE BY ELEVATION ---\n");
    struct BandStats {
        double discharge_sum = 0;
        float discharge_max = 0;
        uint32_t count = 0;
        uint32_t river_tiles = 0;  // discharge > 0.1
    };
    BandStats elev_bands[4];  // 0-25%, 25-50%, 50-75%, 75-100% elevation
    for (size_t i = 0; i < size; ++i) {
        if (world.tiles[i].is_ocean)
            continue;
        int band = static_cast<int>(std::clamp(world.tiles[i].elev01 * 4.0f, 0.0f, 3.99f));
        auto& bs = elev_bands[band];
        bs.count++;
        bs.discharge_sum += state.tiles[i].discharge;
        bs.discharge_max = std::max(bs.discharge_max, state.tiles[i].discharge);
        if (state.tiles[i].discharge >= 0.1f)
            ++bs.river_tiles;
    }
    printf("  %-12s %8s %8s %8s %8s\n", "Elev band", "tiles", "d_mean", "d_max", "river%%");
    const char* band_names[] = {"0-25%", "25-50%", "50-75%", "75-100%"};
    for (int b2 = 0; b2 < 4; ++b2) {
        auto& bs = elev_bands[b2];
        printf("  %-12s %8u %8.4f %8.3f %7.1f%%\n", band_names[b2], bs.count,
               bs.count > 0 ? static_cast<float>(bs.discharge_sum / bs.count) : 0.0f,
               bs.discharge_max,
               bs.count > 0
                   ? 100.0f * static_cast<float>(bs.river_tiles) / static_cast<float>(bs.count)
                   : 0.0f);
    }

    // 7. Discharge by aridity zone
    printf("\n--- DISCHARGE BY ARIDITY ---\n");
    struct AridStats {
        double discharge_sum = 0;
        float discharge_max = 0;
        double sw_sum = 0;
        uint32_t count = 0;
        uint32_t river_tiles = 0;
    };
    AridStats arid_zones[4];  // arid(<0.2), semi-arid(0.2-0.5), sub-humid(0.5-1), humid(>1)
    for (size_t i = 0; i < size; ++i) {
        if (world.tiles[i].is_ocean)
            continue;
        float a = state.tiles[i].aridity;
        int zone = a < 0.2f ? 0 : a < 0.5f ? 1 : a < 1.0f ? 2 : 3;
        auto& az = arid_zones[zone];
        az.count++;
        az.discharge_sum += state.tiles[i].discharge;
        az.discharge_max = std::max(az.discharge_max, state.tiles[i].discharge);
        az.sw_sum += state.tiles[i].surface_water;
        if (state.tiles[i].discharge >= 0.1f)
            ++az.river_tiles;
    }
    printf("  %-12s %8s %8s %8s %8s %8s\n", "Zone", "tiles", "d_mean", "d_max", "sw_mean",
           "river%%");
    const char* zone_names[] = {"Arid", "Semi-arid", "Sub-humid", "Humid"};
    for (int z = 0; z < 4; ++z) {
        auto& az = arid_zones[z];
        printf("  %-12s %8u %8.4f %8.3f %8.4f %7.1f%%\n", zone_names[z], az.count,
               az.count > 0 ? static_cast<float>(az.discharge_sum / az.count) : 0.0f,
               az.discharge_max, az.count > 0 ? static_cast<float>(az.sw_sum / az.count) : 0.0f,
               az.count > 0
                   ? 100.0f * static_cast<float>(az.river_tiles) / static_cast<float>(az.count)
                   : 0.0f);
    }
}

}  // namespace sandbox
