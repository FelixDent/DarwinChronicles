#include "dynamics.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace sandbox {

// ── Constants ───────────────────────────────────────────────────────────────

static constexpr float FREEZING_C = 0.0f;
static constexpr float SNOW_MELT_RATE = 0.08f;    // snow melted per day per degree above 0
static constexpr float RAIN_TO_SURFACE = 0.75f;    // fraction of precip that becomes surface water
static constexpr float INFILTRATION_BASE = 0.04f;  // base infiltration rate per day (slow!)
static constexpr float EVAP_SURFACE_RATE = 0.06f;  // evaporation from surface water per day
static constexpr float EVAP_SOIL_RATE = 0.03f;     // evaporation from soil moisture per day
static constexpr float RUNOFF_RATE = 0.12f;        // fraction of surface water that flows downhill
static constexpr float HUMIDITY_FEEDBACK = 0.02f;  // wet terrain contribution to local humidity

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

    for (size_t i = 0; i < size; ++i) {
        const auto& tt = world.tiles[i];
        const auto& ct = climate.tiles[i];
        auto& dt = state.tiles[i];

        dt.surface_water = 0.0f;
        dt.snow_depth = 0.0f;
        dt.local_humidity = 0.0f;

        if (tt.is_ocean || tt.is_lake) {
            dt.soil_moisture = 1.0f;
            dt.surface_water = 1.0f;
        } else {
            // Initialize soil moisture from climate moisture
            dt.soil_moisture = std::clamp(ct.moisture, 0.0f, 1.0f);
            // Cold areas start with some snow
            if (ct.temperature < FREEZING_C) {
                dt.snow_depth = std::clamp(-ct.temperature * 0.02f, 0.0f, 0.5f);
            }
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

    // Temporary buffer for runoff accumulation (can't modify tiles while iterating)
    std::vector<float> runoff_in(size, 0.0f);

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

        // Skip ocean/lake — they stay saturated
        if (tt.is_ocean || tt.is_lake) {
            dt.surface_water = 1.0f;
            dt.soil_moisture = 1.0f;
            dt.effective_precip = tile_precip;
            dt.effective_moisture = 1.0f;

            if (atmo) {
                // Derive evaporation from atmosphere temperature + wind over water
                float wind_speed =
                    std::sqrt(tile_wind_u * tile_wind_u + tile_wind_v * tile_wind_v);
                float temp_factor = std::clamp(tile_temp / 30.0f, 0.0f, 1.5f);
                dt.effective_evap = std::clamp(temp_factor * (1.0f + wind_speed * 0.5f) * 0.3f,
                                               0.0f, 1.0f);
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

            float front = weather_noise(wx, wy, time_step, FRONT_SIZE) * 0.7f +
                          weather_noise(wx * 2.1f, wy * 2.1f, time_step + 97, FRONT_SIZE / 2) * 0.3f;

            float threshold = 0.55f - tile_precip * 0.3f;
            float modulation = std::clamp((front - threshold) / (1.0f - threshold), 0.0f, 1.0f);

            float effective = tile_precip * modulation * 2.0f;
            dt.effective_precip = std::clamp(effective, 0.0f, 1.0f);
        }

        // Atmosphere precip_rate is already a calibrated daily rate (~0-0.4);
        // baked precipitation is a normalized annual index [0-1] that needs the 0.3 damper.
        float precip_amount = atmo ? dt.effective_precip * dt_days
                                   : dt.effective_precip * dt_days * 0.3f;

        if (tile_temp < FREEZING_C) {
            // Precipitation falls as snow
            dt.snow_depth += precip_amount;
        } else {
            // Rain: split between surface water and direct soil infiltration
            dt.surface_water += precip_amount * RAIN_TO_SURFACE;
            dt.soil_moisture += precip_amount * (1.0f - RAIN_TO_SURFACE);
        }

        // ── 2. Snowmelt ─────────────────────────────────────────────────
        if (dt.snow_depth > 0.0f && tile_temp > FREEZING_C) {
            float melt = std::min(dt.snow_depth, SNOW_MELT_RATE * tile_temp * dt_days);
            dt.snow_depth -= melt;
            dt.surface_water += melt;
        }

        // ── 3. Infiltration: surface → soil ──────────────────────────────
        // Higher soil_hold = better infiltration; steep slope reduces it
        float infiltration = INFILTRATION_BASE * (0.5f + tt.soil_hold * 0.5f) *
                             (1.0f - tt.slope01 * 0.7f) * dt_days;
        float actual_infil = std::min(infiltration, dt.surface_water);
        actual_infil = std::min(actual_infil, 1.0f - dt.soil_moisture);  // don't exceed capacity
        dt.surface_water -= actual_infil;
        dt.soil_moisture += actual_infil;

        // ── 4. Evaporation ───────────────────────────────────────────────
        // Temperature and wind increase evaporation; humidity decreases it
        float temp_factor = std::clamp(tile_temp / 30.0f, 0.0f, 1.5f);
        float wind_speed = std::sqrt(tile_wind_u * tile_wind_u + tile_wind_v * tile_wind_v);
        float wind_factor = 1.0f + wind_speed * 0.5f;
        float humidity_suppress = 1.0f - dt.local_humidity * 0.4f;
        float evap_multiplier = temp_factor * wind_factor * humidity_suppress;

        // Evaporate from surface first, then soil
        // Deep standing water evaporates faster (larger exposed area, lake-like)
        float depth_boost = 1.0f + std::min(dt.surface_water, 1.0f) * 3.0f;
        float surface_evap =
            std::min(dt.surface_water, EVAP_SURFACE_RATE * evap_multiplier * depth_boost * dt_days);
        dt.surface_water -= surface_evap;

        float soil_evap =
            std::min(dt.soil_moisture, EVAP_SOIL_RATE * evap_multiplier * dt_days);
        dt.soil_moisture -= soil_evap;

        // ── 5. Runoff: surface water flows downhill ──────────────────────
        float runoff = dt.surface_water * RUNOFF_RATE * (0.3f + tt.slope01 * 0.7f) * dt_days;
        runoff = std::min(runoff, dt.surface_water);
        dt.surface_water -= runoff;

        if (tt.downhill_x >= 0 && tt.downhill_y >= 0) {
            size_t dest = static_cast<size_t>(tt.downhill_y) * w +
                          static_cast<size_t>(tt.downhill_x);
            // Only accumulate runoff for land tiles; ocean/lake is an intentional sink
            const auto& dest_tile = world.tiles[dest];
            if (!dest_tile.is_ocean && !dest_tile.is_lake)
                runoff_in[dest] += runoff;
        } else {
            // Basin tile (no downhill): excess water seeps into groundwater
            float seepage = dt.surface_water * 0.05f * dt_days;
            dt.surface_water -= seepage;
        }

        // ── 6. Terrain feedback: evaporation → local humidity ────────────
        float total_evap = surface_evap + soil_evap;
        dt.local_humidity =
            std::clamp(dt.local_humidity * 0.95f + total_evap * HUMIDITY_FEEDBACK, 0.0f, 1.0f);

        // Water conservation: excess surface water infiltrates soil before being lost
        if (dt.surface_water > 1.0f) {
            float overflow = dt.surface_water - 1.0f;
            float absorbed = std::min(overflow, 1.0f - dt.soil_moisture);
            dt.soil_moisture += absorbed;
            dt.surface_water = 1.0f;
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

        // Storminess: use atmosphere storm metric when available, else approximate
        if (atmo) {
            dt.effective_storm = atmo->sample(tx, ty, &AtmosphereCell::storminess);
        } else {
            float precip_intensity = dt.effective_precip;
            float wind_storm = std::clamp(wind_speed * 0.8f, 0.0f, 1.0f);
            dt.effective_storm = std::clamp(precip_intensity * 0.6f + wind_storm * 0.3f +
                                                ct.storminess * 0.1f,
                                            0.0f, 1.0f);
        }
    }

    // Apply accumulated runoff, with overflow correction
    for (size_t i = 0; i < size; ++i) {
        if (runoff_in[i] > 0.0f) {
            state.tiles[i].surface_water += runoff_in[i];
            // Same overflow handling: excess infiltrates soil
            if (state.tiles[i].surface_water > 1.0f) {
                float overflow = state.tiles[i].surface_water - 1.0f;
                float absorbed = std::min(overflow, 1.0f - state.tiles[i].soil_moisture);
                state.tiles[i].soil_moisture += absorbed;
                state.tiles[i].surface_water = 1.0f;
            }
        }
    }
}

// ── Statistics ──────────────────────────────────────────────────────────────

DynamicStats compute_dynamic_stats(const DynamicState& state, const Terrain& world) {
    DynamicStats stats{};
    size_t size = state.tiles.size();
    if (size == 0)
        return stats;

    uint32_t land_count = 0;
    double sw_sum = 0, sm_sum = 0, snow_sum = 0;

    for (size_t i = 0; i < size; ++i) {
        const auto& tt = world.tiles[i];
        if (tt.is_ocean || tt.is_lake)
            continue;

        const auto& dt = state.tiles[i];
        land_count++;

        sw_sum += dt.surface_water;
        sm_sum += dt.soil_moisture;
        snow_sum += dt.snow_depth;

        stats.surface_water_max = std::max(stats.surface_water_max, dt.surface_water);
        stats.soil_moisture_max = std::max(stats.soil_moisture_max, dt.soil_moisture);
        stats.snow_max = std::max(stats.snow_max, dt.snow_depth);

        if (dt.snow_depth > 0.01f)
            ++stats.snow_tiles;
        if (dt.surface_water > 0.3f)
            ++stats.flooded_tiles;
    }

    if (land_count > 0) {
        auto n = static_cast<float>(land_count);
        stats.surface_water_mean = static_cast<float>(sw_sum / n);
        stats.soil_moisture_mean = static_cast<float>(sm_sum / n);
        stats.snow_mean = static_cast<float>(snow_sum / n);
    }

    return stats;
}

}  // namespace sandbox
