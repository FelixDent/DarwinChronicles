#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "weather.h"
#include "terrain_gen.h"

namespace sandbox {

struct DynamicState;  // forward declaration

// ── Atmosphere cell (coarse grid) ──────────────────────────────────────

struct AtmosphereCell {
    // Atmospheric state
    float T = 15.0f;           // air temperature (C)
    float q = 0.3f;            // specific humidity [0..1]
    float p = 1013.0f;         // pressure (hPa-ish, arbitrary units)
    float u = 0.0f;            // wind x component (east = positive)
    float v = 0.0f;            // wind y component (south = positive)
    float cloud = 0.0f;        // condensed water [0..1]
    float precip_rate = 0.0f;  // precipitation output this tick [0..1]
    float storminess = 0.0f;   // storm intensity [0..1] — from convergence + instability

    // Ground coupling
    float ground_temp = 15.0f;  // surface temperature (C)

    // Precomputed terrain averages for this cell
    float avg_elevation = 0.0f;
    float avg_roughness = 0.0f;
    float avg_snow = 0.0f;
    float avg_soil_wet = 0.0f;
    bool is_water = false;  // majority water cell
};

// ── Atmosphere state ───────────────────────────────────────────────────

struct AtmosphereState {
    uint32_t width = 0;   // coarse grid width
    uint32_t height = 0;  // coarse grid height
    uint32_t scale = 4;   // terrain tiles per atmosphere cell
    float time_of_day = 12.0f;  // hours [0..24)
    float day_of_year = 80.0f;  // [0..365), starts ~spring equinox

    std::vector<AtmosphereCell> cells;
    std::vector<AtmosphereCell> scratch;  // double-buffer for advection

    // Invariant tracking
    float total_water = 0.0f;            // sum(q + cloud) — changes due to evap/precip are intentional
    float last_water_correction = 0.0f;  // advection drift correction applied last tick
    float last_dt_days = 0.0f;           // dt used in last tick (for CFL computation)

    AtmosphereCell& cell_at(uint32_t x, uint32_t y) { return cells[y * width + x]; }
    const AtmosphereCell& cell_at(uint32_t x, uint32_t y) const { return cells[y * width + x]; }

    // Nearest-neighbor sample a field at terrain-tile coordinates
    // Use for fields with sharp boundaries (precipitation, storminess)
    float sample_nearest(uint32_t tile_x, uint32_t tile_y, float AtmosphereCell::*field) const {
        uint32_t cx = std::min(tile_x / scale, width - 1);
        uint32_t cy = std::min(tile_y / scale, height - 1);
        return cells[static_cast<size_t>(cy) * width + cx].*field;
    }

    // Bilinear sample a field at terrain-tile coordinates
    float sample(uint32_t tile_x, uint32_t tile_y, float AtmosphereCell::*field) const {
        float fx = static_cast<float>(tile_x) / static_cast<float>(scale);
        float fy = static_cast<float>(tile_y) / static_cast<float>(scale);
        fx = std::clamp(fx, 0.0f, static_cast<float>(width - 1));
        fy = std::clamp(fy, 0.0f, static_cast<float>(height - 1));

        auto x0 = static_cast<int>(fx);
        auto y0 = static_cast<int>(fy);
        int x1 = std::min(x0 + 1, static_cast<int>(width - 1));
        int y1 = std::min(y0 + 1, static_cast<int>(height - 1));
        float sx = fx - static_cast<float>(x0);
        float sy = fy - static_cast<float>(y0);

        float v00 = cells[static_cast<size_t>(y0) * width + static_cast<size_t>(x0)].*field;
        float v10 = cells[static_cast<size_t>(y0) * width + static_cast<size_t>(x1)].*field;
        float v01 = cells[static_cast<size_t>(y1) * width + static_cast<size_t>(x0)].*field;
        float v11 = cells[static_cast<size_t>(y1) * width + static_cast<size_t>(x1)].*field;

        return (v00 * (1.0f - sx) + v10 * sx) * (1.0f - sy) +
               (v01 * (1.0f - sx) + v11 * sx) * sy;
    }
};

// ── Atmosphere diagnostics ────────────────────────────────────────────

struct AtmosphereStats {
    float T_min = 0, T_max = 0, T_mean = 0;
    float q_min = 0, q_max = 0, q_mean = 0;
    float cloud_min = 0, cloud_max = 0, cloud_mean = 0;
    float precip_min = 0, precip_max = 0, precip_mean = 0;
    float wind_min = 0, wind_max = 0, wind_mean = 0;
    float storm_mean = 0;
    float p_min = 0, p_max = 0;
    uint32_t raining_cells = 0;  // precip_rate > 0.01
    uint32_t cloudy_cells = 0;   // cloud > 0.05

    // Invariant monitoring
    float total_atmo_water = 0;  // sum(q + cloud) across all cells — changes due to evap/precip
    float max_courant = 0;       // max(wind * dt * advect_speed) — should be <= 1
    float water_correction = 0;  // conservation correction applied this tick

    // Latitude-band diagnostics (thirds: polar-north, equatorial, polar-south)
    float T_band[3] = {};      // mean temperature per band
    float wind_band[3] = {};   // mean wind speed per band
    float q_band[3] = {};      // mean humidity per band

    // Variation metrics (standard deviation across all cells)
    float T_stddev = 0;        // temperature spatial variation
    float wind_stddev = 0;     // wind speed spatial variation
    float q_stddev = 0;        // humidity spatial variation
    float wind_dir_spread = 0; // wind direction spread [0..1] (0=uniform, 1=all directions)
    float precip_stddev = 0;   // precipitation spatial variation

    // Extreme event metrics (per-cell peaks)
    float storm_max = 0;      // peak storminess across all cells
    float precip_max_cell = 0; // peak single-cell precipitation rate
    float precip_p95 = 0;     // 95th percentile precipitation (approx)
    uint32_t severe_storm_cells = 0;  // cells with storminess > 0.5
    uint32_t heavy_rain_cells = 0;    // cells with precip_rate > 0.15
    uint32_t heat_wave_cells = 0;     // cells with T > 30
    uint32_t deep_freeze_cells = 0;   // cells with T < -25
    uint32_t gale_cells = 0;          // cells with wind > 4.0
};

AtmosphereStats compute_atmosphere_stats(const AtmosphereState& atmo);

// ── API ────────────────────────────────────────────────────────────────

// Initialize atmosphere grid from baked climate + terrain
void init_atmosphere(AtmosphereState& atmo, const Terrain& world, const ClimateData& climate,
                     uint32_t scale = 4);

// Advance atmosphere simulation by dt_seconds of real time
void tick_atmosphere(AtmosphereState& atmo, const Terrain& world, const DynamicState& dyn,
                     float dt_seconds);

}  // namespace sandbox
