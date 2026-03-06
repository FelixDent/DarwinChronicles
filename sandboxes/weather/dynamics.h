#pragma once

#include <cstdint>
#include <vector>

#include "atmosphere.h"
#include "weather.h"
#include "terrain_gen.h"

namespace sandbox {

// ── Per-tile dynamic environmental state ───────────────────────────────────
// These fields change every simulation tick based on weather-terrain interaction.

struct DynamicTile {
    float surface_water = 0.0f;    // standing water / puddles [0..1+]
    float soil_moisture = 0.0f;    // root-zone water [0..1]
    float snow_depth = 0.0f;       // accumulated snow [0..1+]
    float local_humidity = 0.0f;   // terrain feedback: evaporation contribution [0..1]
    float effective_precip = 0.0f;  // current precipitation rate after weather modulation [0..1]
    float effective_moisture = 0.0f;  // combined moisture (soil + surface + humidity) [0..1]
    float effective_evap = 0.0f;     // actual evaporation rate this tick [0..1]
    float effective_storm = 0.0f;    // storminess derived from weather fronts [0..1]
};

// ── Dynamic simulation state ───────────────────────────────────────────────

struct DynamicState {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<DynamicTile> tiles;
    float elapsed_days = 0.0f;
    float time_scale = 1.0f;
    bool paused = true;

    DynamicTile& tile_at(uint32_t x, uint32_t y) { return tiles[y * width + x]; }
    const DynamicTile& tile_at(uint32_t x, uint32_t y) const { return tiles[y * width + x]; }
};

// ── Simulation API ─────────────────────────────────────────────────────────

// Initialize dynamic state from terrain + climate (sets initial soil moisture etc.)
void init_dynamics(DynamicState& state, const Terrain& world, const ClimateData& climate);

// Advance simulation by dt_seconds of real time (scaled by state.time_scale)
// When atmo is non-null, reads T/precip/wind from atmosphere instead of static climate.
void tick_dynamics(DynamicState& state, const Terrain& world, const ClimateData& climate,
                   float dt_seconds, const AtmosphereState* atmo = nullptr);

// ── Dynamic statistics ─────────────────────────────────────────────────────

struct DynamicStats {
    float surface_water_mean = 0.0f;
    float surface_water_max = 0.0f;
    float soil_moisture_mean = 0.0f;
    float soil_moisture_max = 0.0f;
    float snow_mean = 0.0f;
    float snow_max = 0.0f;
    uint32_t snow_tiles = 0;
    uint32_t flooded_tiles = 0;  // surface_water > 0.3
};

DynamicStats compute_dynamic_stats(const DynamicState& state, const Terrain& world);

}  // namespace sandbox
