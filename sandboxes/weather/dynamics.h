#pragma once

#include <cstdint>
#include <vector>

#include "atmosphere.h"
#include "terrain_gen.h"
#include "weather.h"

namespace sandbox {

// ── Per-tile dynamic environmental state ───────────────────────────────────
// These fields change every simulation tick based on weather-terrain interaction.

struct DynamicTile {
    float surface_water = 0.0f;       // standing water / puddles [0..1+]
    float soil_moisture = 0.0f;       // root-zone water [0..1]
    float snow_depth = 0.0f;          // accumulated snow [0..1+]
    float local_humidity = 0.0f;      // terrain feedback: evaporation contribution [0..1]
    float effective_precip = 0.0f;    // current precipitation rate after weather modulation [0..1]
    float effective_moisture = 0.0f;  // combined moisture (soil + surface + humidity) [0..1]
    float effective_evap = 0.0f;      // actual evaporation rate this tick [0..1]
    float effective_storm = 0.0f;     // storminess derived from weather fronts [0..1]

    // Hydrology
    float groundwater = 0.0f;  // groundwater storage (depth equivalent, meters)
    float discharge = 0.0f;    // total outflow per tick (for river visualization)
    float quickflow = 0.0f;    // fast-response discharge component (storm pulses)
    float baseflow_d = 0.0f;   // slow-response discharge component (groundwater-fed)

    // Moisture balance (aridity tracking)
    float pet = 0.0f;      // potential evapotranspiration this tick [0..1]
    float aridity = 0.5f;  // running aridity index: precip/PET [0..2+], EMA smoothed
                           // >1 = humid, 0.5-1 = sub-humid, 0.2-0.5 = semi-arid, <0.2 = arid
};

// ── Water budget tracking (cumulative over simulation) ───────────────────

struct WaterBudget {
    double total_precip = 0;         // total precipitation onto land
    double total_evap_surface = 0;   // total evaporation from surface water
    double total_evap_soil = 0;      // total evaporation from soil moisture
    double total_ocean_drain = 0;    // total water drained to ocean (coastal + overflow)
    double total_coastal_drain = 0;  // coastal WSE drain to ocean neighbors
    double total_overflow = 0;       // flash runoff from surface water cap
    double total_gw_recharge = 0;    // total infiltration to groundwater
    double total_snowmelt = 0;       // total snowmelt
    double total_headwater = 0;      // total headwater minimum injection
    double total_accum_raw = 0;      // total D8 accum seed (before headwater injection)
    double initial_storage = 0;      // sum(sw + sm + gw + snow) at start
};

// ── Dynamic simulation state ───────────────────────────────────────────────

struct DynamicState {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<DynamicTile> tiles;
    std::vector<float> runoff_buf;  // pre-allocated buffer for WSE flow accumulation

    // D8 flow routing (computed once at init from terrain)
    std::vector<int32_t> downhill;       // index of steepest-descent neighbor (-1 = sink/ocean)
    std::vector<uint32_t> topo_order;    // tiles sorted by elevation (descending) for flow accum
    std::vector<float> accum_discharge;  // per-tick accumulated discharge buffer
    std::vector<float> accum_quick;      // per-tick quickflow accumulation
    std::vector<float> accum_base;       // per-tick baseflow accumulation

    // Basin spillway storage for lakes
    static constexpr uint16_t NO_BASIN = 0xFFFF;
    std::vector<uint16_t> basin_id;          // per-tile basin ID (NO_BASIN = not in sink basin)
    std::vector<float> basin_spill_elev;     // per basin: minimum boundary elevation
    std::vector<float> basin_sink_elev;      // per basin: minimum elevation in basin
    std::vector<uint32_t> basin_area;        // per basin: number of tiles
    std::vector<float> basin_volume;         // per basin: current water volume
    std::vector<uint32_t> basin_spill_tile;  // per basin: tile where spill exits
    std::vector<std::vector<uint32_t>> basin_tiles;  // per basin: list of tile indices
    uint16_t num_basins = 0;

    WaterBudget budget;  // cumulative water budget tracking

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

// ── Hydrology diagnostics (called at end of headless run) ────────────────

void print_hydrology_diagnostics(const DynamicState& state, const Terrain& world);

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

    // Hydrology diagnostics
    float groundwater_mean = 0.0f;
    float groundwater_max = 0.0f;
    float discharge_mean = 0.0f;
    float discharge_max = 0.0f;

    // Aridity diagnostics
    float aridity_mean = 0.0f;
    float aridity_min = 0.0f;
    float aridity_max = 0.0f;
    float pet_mean = 0.0f;
    uint32_t arid_tiles = 0;   // aridity < 0.2
    uint32_t humid_tiles = 0;  // aridity > 1.0
};

DynamicStats compute_dynamic_stats(const DynamicState& state, const Terrain& world);

}  // namespace sandbox
