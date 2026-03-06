#pragma once

#include <cstdint>
#include <vector>

#include "terrain_gen.h"

namespace sandbox {

// ── Shared simulation constant ──────────────────────────────────────────────

static constexpr float DAYS_PER_SECOND = 2.0f;  // 2 sim-days per real second at 1x speed

// ── Atmosphere configuration ────────────────────────────────────────────────

struct AtmosphereConfig {
    float greenhouse = 2.0f;         // Greenhouse warming offset (C)
    float lapse_rate = 6.5f;         // Temperature decrease per km elevation
    float ocean_moderation = 5.0f;   // Ocean temperature moderation (C)
    float hadley_strength = 0.8f;    // Trade wind strength
    float jet_strength = 1.0f;       // Westerly jet strength
    float polar_strength = 0.4f;     // Polar easterly strength
    float rainout_rate = 0.15f;      // Fraction of moisture lost per cell
    float orographic_factor = 8.0f;  // Mountain uplift precipitation multiplier
    float ocean_evap = 0.8f;         // Ocean evaporation rate
    float land_recycle = 0.05f;      // Land moisture recycling rate
    float meridional_weight = 0.3f;  // Weight of meridional (N-S) sweep
};

// ── Per-tile climate data ───────────────────────────────────────────────────

struct ClimateTile {
    float temperature = 0.0f;    // Celsius
    float precipitation = 0.0f;  // [0..1] normalized
    float moisture = 0.0f;       // Effective moisture [0..1]
    float evaporation = 0.0f;    // Evap demand [0..1]
    float wind_u = 0.0f;         // Zonal wind (positive = eastward)
    float wind_v = 0.0f;         // Meridional wind (positive = southward)
    float storminess = 0.0f;     // [0..1]
    float rain_shadow = 0.0f;    // [0..1] leeward dryness behind mountains
};

// ── Climate data grid ───────────────────────────────────────────────────────

struct ClimateData {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<ClimateTile> tiles;

    const ClimateTile& tile_at(uint32_t x, uint32_t y) const { return tiles[y * width + x]; }
    ClimateTile& tile_at(uint32_t x, uint32_t y) { return tiles[y * width + x]; }
};

// ── Weather bake timings ────────────────────────────────────────────────────

struct WeatherTimings {
    double latitude_ms = 0.0;
    double temperature_ms = 0.0;
    double wind_ms = 0.0;
    double precipitation_ms = 0.0;
    double evaporation_ms = 0.0;
    double moisture_ms = 0.0;
    double storminess_ms = 0.0;
    double rain_shadow_ms = 0.0;
    double total_ms = 0.0;
};

// ── Weather statistics ──────────────────────────────────────────────────────

struct WeatherStats {
    float temp_min = 0.0f, temp_max = 0.0f, temp_mean = 0.0f;
    float precip_min = 0.0f, precip_max = 0.0f, precip_mean = 0.0f;
    float moisture_min = 0.0f, moisture_max = 0.0f, moisture_mean = 0.0f;
    float evap_min = 0.0f, evap_max = 0.0f, evap_mean = 0.0f;
    float wind_speed_max = 0.0f;
    uint32_t storm_tiles = 0;
    uint32_t rain_shadow_tiles = 0;
    uint32_t wet_coast_tiles = 0;
};

// ── Bake weather from terrain ───────────────────────────────────────────────

ClimateData bake_weather(const Terrain& world, const AtmosphereConfig& atmo,
                         WeatherTimings* timings = nullptr);

WeatherStats compute_weather_stats(const ClimateData& climate, const Terrain& world);

// Forward declarations for live stats computation
struct AtmosphereState;
struct DynamicState;

// Compute live weather stats from running atmosphere + dynamics simulation
WeatherStats compute_live_weather_stats(const AtmosphereState& atmo, const DynamicState& dyn,
                                        const Terrain& world);

void print_weather_stats(const WeatherStats& stats);

}  // namespace sandbox
