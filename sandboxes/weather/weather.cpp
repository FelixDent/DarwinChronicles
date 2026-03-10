#include "weather.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>

#include "atmosphere.h"
#include "dynamics.h"

namespace sandbox {

using Clock = std::chrono::high_resolution_clock;

static double elapsed_ms(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// ── Constants ───────────────────────────────────────────────────────────────

static constexpr float PI = 3.14159265358979f;

// ── Wind direction helper ───────────────────────────────────────────────────

// Returns the sign of the zonal wind for a given latitude band.
// Negative = easterlies (wind blowing westward), positive = westerlies.
static void compute_wind_at_latitude(float lat_rad, float cos_lat, const AtmosphereConfig& atmo,
                                     float& u_out, float& v_out) {
    float abs_lat = std::abs(lat_rad);
    float lat_deg = abs_lat * 180.0f / PI;

    if (lat_deg < 30.0f) {
        // Hadley cell: trade easterlies
        float t = lat_deg / 30.0f;
        u_out = -atmo.hadley_strength * std::sin(t * PI);
        // Meridional: toward equator (converging at ITCZ)
        v_out = (lat_rad > 0.0f ? 0.15f : -0.15f) * cos_lat;
    } else if (lat_deg < 60.0f) {
        // Ferrel cell: westerlies
        float t = (lat_deg - 30.0f) / 30.0f;
        u_out = atmo.jet_strength * std::sin(t * PI);
        // Meridional: poleward
        v_out = (lat_rad > 0.0f ? -0.1f : 0.1f) * cos_lat;
    } else {
        // Polar cell: polar easterlies
        float t = (lat_deg - 60.0f) / 30.0f;
        u_out = -atmo.polar_strength * std::sin(t * PI);
        v_out = (lat_rad > 0.0f ? 0.1f : -0.1f) * cos_lat;
    }
}

// ── Bake weather ────────────────────────────────────────────────────────────

ClimateData bake_weather(const Terrain& world, const AtmosphereConfig& atmo,
                         WeatherTimings* timings) {
    auto total_start = Clock::now();

    uint32_t w = world.width;
    uint32_t h = world.height;
    size_t size = static_cast<size_t>(w) * h;

    ClimateData climate;
    climate.width = w;
    climate.height = h;
    climate.tiles.resize(size);

    // ── Step 1: Latitude tables ─────────────────────────────────────────────
    auto t0 = Clock::now();

    std::vector<float> lat_rad(h);
    std::vector<float> sin_lat(h);
    std::vector<float> cos_lat(h);

    for (uint32_t y = 0; y < h; ++y) {
        // y=0 is north pole (+90), y=h-1 is south pole (-90)
        float lat_frac = 1.0f - 2.0f * static_cast<float>(y) / static_cast<float>(h - 1);
        lat_rad[y] = lat_frac * PI * 0.5f;
        sin_lat[y] = std::sin(lat_rad[y]);
        cos_lat[y] = std::cos(lat_rad[y]);
    }

    if (timings)
        timings->latitude_ms = elapsed_ms(t0);

    // ── Step 2: Temperature ─────────────────────────────────────────────────
    t0 = Clock::now();

    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            size_t idx = static_cast<size_t>(y) * w + x;
            const TerrainTile& tt = world.tiles[idx];

            // Base temperature: warm at equator, cold at poles
            float base_temp = 30.0f * cos_lat[y] - 10.0f + atmo.greenhouse;

            // Lapse rate: elevation cooling (elev01 maps to ~8km range)
            float elev = tt.is_ocean ? 0.0f : tt.elev01;
            base_temp -= atmo.lapse_rate * elev * 8.0f / 6.5f;

            // Ocean moderation: coastal areas have milder temperatures
            if (tt.is_ocean || tt.band == ElevBand::Water) {
                // Ocean tiles: moderate toward ocean mean
                base_temp = base_temp * 0.7f + 15.0f * 0.3f;
            } else if (tt.dist_ocean < 10.0f) {
                float coastal_w = 1.0f - tt.dist_ocean / 10.0f;
                base_temp += coastal_w * atmo.ocean_moderation * (base_temp < 15.0f ? 1.0f : -0.5f);
            }

            climate.tiles[idx].temperature = base_temp;
        }
    }

    if (timings)
        timings->temperature_ms = elapsed_ms(t0);

    // ── Step 3: Wind climatology ────────────────────────────────────────────
    t0 = Clock::now();

    for (uint32_t y = 0; y < h; ++y) {
        float base_u, base_v;
        compute_wind_at_latitude(lat_rad[y], cos_lat[y], atmo, base_u, base_v);

        for (uint32_t x = 0; x < w; ++x) {
            size_t idx = static_cast<size_t>(y) * w + x;
            const TerrainTile& tt = world.tiles[idx];

            float u = base_u;
            float v = base_v;

            // Mountain deflection: reduce wind magnitude where elevation is high
            if (!tt.is_ocean && tt.band != ElevBand::Water && tt.elev01 > 0.5f) {
                float deflect = 1.0f - (tt.elev01 - 0.5f) * 1.5f;
                deflect = std::max(deflect, 0.1f);
                u *= deflect;
                v *= deflect;
            }

            climate.tiles[idx].wind_u = u;
            climate.tiles[idx].wind_v = v;
        }
    }

    if (timings)
        timings->wind_ms = elapsed_ms(t0);

    // ── Step 4: Precipitation via moisture flux sweeps ──────────────────────
    t0 = Clock::now();

    // Raw precipitation accumulator
    std::vector<float> raw_precip(size, 0.0f);

    // Primary sweep: along zonal wind direction per row
    for (uint32_t y = 0; y < h; ++y) {
        // Determine sweep direction from mean zonal wind at this latitude
        float mean_u = climate.tiles[static_cast<size_t>(y) * w].wind_u;
        bool sweep_east = (mean_u >= 0.0f);

        float flux = 0.0f;

        if (sweep_east) {
            for (uint32_t x = 0; x < w; ++x) {
                size_t idx = static_cast<size_t>(y) * w + x;
                const TerrainTile& tt = world.tiles[idx];

                // Source
                if (tt.is_ocean || tt.band == ElevBand::Water) {
                    flux += atmo.ocean_evap * (0.5f + 0.5f * cos_lat[y]);
                } else {
                    flux += atmo.land_recycle;
                }

                // Uplift from elevation change
                float uplift = 0.0f;
                if (x + 1 < w) {
                    float next_elev = world.tiles[idx + 1].elev01;
                    uplift = std::max(0.0f, next_elev - tt.elev01);
                }

                // Rainout
                float sink = atmo.rainout_rate * flux * (1.0f + atmo.orographic_factor * uplift);
                sink = std::min(sink, flux);
                raw_precip[idx] += sink;
                flux -= sink;
                flux = std::max(flux, 0.0f);
            }
        } else {
            for (int x = static_cast<int>(w) - 1; x >= 0; --x) {
                size_t idx = static_cast<size_t>(y) * w + static_cast<size_t>(x);
                const TerrainTile& tt = world.tiles[idx];

                if (tt.is_ocean || tt.band == ElevBand::Water) {
                    flux += atmo.ocean_evap * (0.5f + 0.5f * cos_lat[y]);
                } else {
                    flux += atmo.land_recycle;
                }

                float uplift = 0.0f;
                if (x > 0) {
                    float prev_elev = world.tiles[idx - 1].elev01;
                    uplift = std::max(0.0f, prev_elev - tt.elev01);
                }

                float sink = atmo.rainout_rate * flux * (1.0f + atmo.orographic_factor * uplift);
                sink = std::min(sink, flux);
                raw_precip[idx] += sink;
                flux -= sink;
                flux = std::max(flux, 0.0f);
            }
        }
    }

    // Secondary sweep: meridional (N-S) along meridional wind
    for (uint32_t x = 0; x < w; ++x) {
        // Determine sweep direction from mean meridional wind
        float mean_v = climate.tiles[static_cast<size_t>(h / 2) * w + x].wind_v;
        bool sweep_south = (mean_v >= 0.0f);

        float flux = 0.0f;

        auto sweep_cell = [&](uint32_t y) {
            size_t idx = static_cast<size_t>(y) * w + x;
            const TerrainTile& tt = world.tiles[idx];

            if (tt.is_ocean || tt.band == ElevBand::Water) {
                flux += atmo.ocean_evap * 0.5f;
            } else {
                flux += atmo.land_recycle * 0.5f;
            }

            float uplift = 0.0f;
            if (sweep_south && y + 1 < h) {
                float next_elev = world.tiles[(static_cast<size_t>(y) + 1) * w + x].elev01;
                uplift = std::max(0.0f, next_elev - tt.elev01);
            } else if (!sweep_south && y > 0) {
                float prev_elev = world.tiles[(static_cast<size_t>(y) - 1) * w + x].elev01;
                uplift = std::max(0.0f, prev_elev - tt.elev01);
            }

            float sink = atmo.rainout_rate * flux * (1.0f + atmo.orographic_factor * 0.5f * uplift);
            sink = std::min(sink, flux);
            raw_precip[idx] += sink * atmo.meridional_weight;
            flux -= sink;
            flux = std::max(flux, 0.0f);
        };

        if (sweep_south) {
            for (uint32_t y = 0; y < h; ++y)
                sweep_cell(y);
        } else {
            for (int y = static_cast<int>(h) - 1; y >= 0; --y)
                sweep_cell(static_cast<uint32_t>(y));
        }
    }

    // Normalize precipitation to [0, 1]
    float max_precip = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        if (raw_precip[i] > max_precip)
            max_precip = raw_precip[i];
    }

    if (max_precip > 0.0f) {
        for (size_t i = 0; i < size; ++i) {
            climate.tiles[i].precipitation = raw_precip[i] / max_precip;
        }
    }

    if (timings)
        timings->precipitation_ms = elapsed_ms(t0);

    // ── Step 5: Evaporation demand ──────────────────────────────────────────
    t0 = Clock::now();

    for (uint32_t y = 0; y < h; ++y) {
        float insolation_norm = cos_lat[y];
        for (uint32_t x = 0; x < w; ++x) {
            size_t idx = static_cast<size_t>(y) * w + x;
            float temp = climate.tiles[idx].temperature;
            float evap = std::clamp(0.02f * std::max(0.0f, temp) * (1.0f + 0.5f * insolation_norm),
                                    0.0f, 1.0f);
            climate.tiles[idx].evaporation = evap;
        }
    }

    if (timings)
        timings->evaporation_ms = elapsed_ms(t0);

    // ── Step 6: Effective moisture (aridity-based) ─────────────────────────
    // Use aridity index (precip / PET) instead of raw subtraction.
    // This correctly distinguishes "800mm rain + cool climate = forest"
    // from "800mm rain + hot climate = grassland/desert".
    t0 = Clock::now();

    for (size_t i = 0; i < size; ++i) {
        float precip = climate.tiles[i].precipitation;
        float evap = climate.tiles[i].evaporation;
        // Aridity index: >1 = humid, <0.2 = arid
        float aridity = precip / (evap + 0.01f);
        // Map to [0,1] for moisture field: aridity of 1.0 → moisture 0.8
        climate.tiles[i].moisture = std::clamp(aridity * 0.8f, 0.0f, 1.0f);
    }

    if (timings)
        timings->moisture_ms = elapsed_ms(t0);

    // ── Step 7: Storminess ──────────────────────────────────────────────────
    t0 = Clock::now();

    for (uint32_t y = 1; y + 1 < h; ++y) {
        float abs_lat = std::abs(lat_rad[y]) * 180.0f / PI;

        // Cyclogenesis band (latitude-dependent storm formation zones)
        float midlat_dev = (abs_lat - 45.0f) / 15.0f;
        float midlat_peak = std::exp(-(midlat_dev * midlat_dev));
        float tropical_dev = (abs_lat - 18.0f) / 8.0f;
        float tropical_gauss = std::exp(-(tropical_dev * tropical_dev));

        for (uint32_t x = 1; x + 1 < w; ++x) {
            size_t idx = static_cast<size_t>(y) * w + x;
            size_t idx_up = (static_cast<size_t>(y) - 1) * w + x;
            size_t idx_down = (static_cast<size_t>(y) + 1) * w + x;
            const TerrainTile& tt = world.tiles[idx];
            const ClimateTile& ct = climate.tiles[idx];

            // Factor 1: Moisture convergence (weight 0.30)
            float m_here = ct.moisture;
            float m_avg = (climate.tiles[idx - 1].moisture + climate.tiles[idx + 1].moisture +
                           climate.tiles[idx_up].moisture + climate.tiles[idx_down].moisture) *
                          0.25f;
            float convergence = std::clamp(std::max(0.0f, m_avg - m_here) * 3.0f, 0.0f, 1.0f);

            // Factor 2: Wind shear (weight 0.20)
            float du = climate.tiles[idx + 1].wind_u - climate.tiles[idx - 1].wind_u;
            float dv = climate.tiles[idx_down].wind_v - climate.tiles[idx_up].wind_v;
            float shear = std::clamp(std::sqrt(du * du + dv * dv) * 2.0f, 0.0f, 1.0f);

            // Factor 3: Thermal instability (weight 0.20) — hot + moist = convective
            float instability =
                std::clamp(std::max(0.0f, ct.temperature / 40.0f) * ct.moisture, 0.0f, 1.0f);

            // Factor 4: Frontal zone — full 2D temperature gradient (weight 0.15)
            float dtx = climate.tiles[idx + 1].temperature - climate.tiles[idx - 1].temperature;
            float dty = climate.tiles[idx_down].temperature - climate.tiles[idx_up].temperature;
            float frontal = std::clamp(std::sqrt(dtx * dtx + dty * dty) * 0.1f, 0.0f, 1.0f);

            // Factor 5: Cyclogenesis band (weight 0.15)
            float trop_peak = tropical_gauss * (tt.is_ocean ? 1.0f : 0.0f) *
                              (ct.temperature > 26.0f ? 1.0f : 0.0f);
            float cyclogenesis = std::max(midlat_peak * 0.6f, trop_peak * 0.4f);

            float storm = convergence * 0.30f + shear * 0.20f + instability * 0.20f +
                          frontal * 0.15f + cyclogenesis * 0.15f;
            climate.tiles[idx].storminess = std::clamp(storm, 0.0f, 1.0f);
        }
    }

    if (timings)
        timings->storminess_ms = elapsed_ms(t0);

    // ── Step 8: Rain shadow detection ───────────────────────────────────────
    // A rain shadow tile is dry land that is downwind (leeward) of elevated
    // terrain which received significantly more precipitation. We trace upwind
    // from each dry land tile looking for a nearby mountain with higher precip.
    t0 = Clock::now();

    constexpr float SHADOW_PRECIP_THRESHOLD = 0.2f;  // Tile must be fairly dry
    constexpr int SHADOW_TRACE_DIST = 12;            // How far upwind to look
    constexpr float SHADOW_ELEV_MIN = 0.55f;         // Upwind terrain must be elevated
    constexpr float SHADOW_PRECIP_RATIO = 2.0f;      // Upwind must have N× more precip

    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            size_t idx = static_cast<size_t>(y) * w + x;
            const TerrainTile& tt = world.tiles[idx];
            const ClimateTile& ct = climate.tiles[idx];

            // Only check dry land tiles
            if (tt.is_ocean || tt.band == ElevBand::Water)
                continue;
            if (ct.precipitation >= SHADOW_PRECIP_THRESHOLD)
                continue;

            // Trace upwind: opposite of wind direction
            float speed = std::sqrt(ct.wind_u * ct.wind_u + ct.wind_v * ct.wind_v);
            if (speed < 0.05f)
                continue;

            float trace_dx = -ct.wind_u / speed;  // Upwind direction
            float trace_dy = -ct.wind_v / speed;

            float best_shadow = 0.0f;
            float fx = static_cast<float>(x);
            float fy = static_cast<float>(y);

            for (int step = 1; step <= SHADOW_TRACE_DIST; ++step) {
                fx += trace_dx;
                fy += trace_dy;

                int tx = static_cast<int>(std::round(fx));
                int ty = static_cast<int>(std::round(fy));
                if (tx < 0 || tx >= static_cast<int>(w) || ty < 0 || ty >= static_cast<int>(h))
                    break;

                size_t tidx = static_cast<size_t>(ty) * w + static_cast<size_t>(tx);
                const TerrainTile& upwind_tt = world.tiles[tidx];
                const ClimateTile& upwind_ct = climate.tiles[tidx];

                // Found elevated terrain upwind with much more precipitation?
                if (upwind_tt.elev01 >= SHADOW_ELEV_MIN &&
                    upwind_ct.precipitation > ct.precipitation * SHADOW_PRECIP_RATIO &&
                    upwind_ct.precipitation > ct.precipitation + 0.1f) {
                    // Strength: based on elevation difference and precip contrast
                    float elev_factor =
                        std::clamp((upwind_tt.elev01 - tt.elev01) * 3.0f, 0.0f, 1.0f);
                    float precip_contrast =
                        std::clamp((upwind_ct.precipitation - ct.precipitation) * 3.0f, 0.0f, 1.0f);
                    // Closer upwind mountains create stronger shadows
                    float dist_factor =
                        1.0f - static_cast<float>(step - 1) / static_cast<float>(SHADOW_TRACE_DIST);

                    float shadow = elev_factor * precip_contrast * dist_factor;
                    best_shadow = std::max(best_shadow, shadow);
                }
            }

            climate.tiles[idx].rain_shadow = best_shadow;
        }
    }

    if (timings)
        timings->rain_shadow_ms = elapsed_ms(t0);

    if (timings)
        timings->total_ms = elapsed_ms(total_start);

    return climate;
}

// ── Weather statistics ──────────────────────────────────────────────────────

WeatherStats compute_weather_stats(const ClimateData& climate, const Terrain& world) {
    WeatherStats stats;

    if (climate.tiles.empty())
        return stats;

    stats.temp_min = 1e9f;
    stats.temp_max = -1e9f;
    stats.precip_min = 1e9f;
    stats.precip_max = -1e9f;
    stats.moisture_min = 1e9f;
    stats.moisture_max = -1e9f;
    stats.evap_min = 1e9f;
    stats.evap_max = -1e9f;

    double temp_sum = 0.0;
    double precip_sum = 0.0;
    double moisture_sum = 0.0;
    double evap_sum = 0.0;
    size_t count = climate.tiles.size();

    for (size_t i = 0; i < count; ++i) {
        const auto& ct = climate.tiles[i];
        const auto& tt = world.tiles[i];

        stats.temp_min = std::min(stats.temp_min, ct.temperature);
        stats.temp_max = std::max(stats.temp_max, ct.temperature);
        temp_sum += ct.temperature;

        stats.precip_min = std::min(stats.precip_min, ct.precipitation);
        stats.precip_max = std::max(stats.precip_max, ct.precipitation);
        precip_sum += ct.precipitation;

        stats.moisture_min = std::min(stats.moisture_min, ct.moisture);
        stats.moisture_max = std::max(stats.moisture_max, ct.moisture);
        moisture_sum += ct.moisture;

        stats.evap_min = std::min(stats.evap_min, ct.evaporation);
        stats.evap_max = std::max(stats.evap_max, ct.evaporation);
        evap_sum += ct.evaporation;

        float speed = std::sqrt(ct.wind_u * ct.wind_u + ct.wind_v * ct.wind_v);
        if (speed > stats.wind_speed_max)
            stats.wind_speed_max = speed;

        if (ct.storminess > 0.3f)
            ++stats.storm_tiles;

        if (ct.rain_shadow > 0.2f)
            ++stats.rain_shadow_tiles;

        if (!tt.is_ocean && tt.band != ElevBand::Water && tt.dist_ocean < 5.0f &&
            ct.precipitation > 0.5f)
            ++stats.wet_coast_tiles;
    }

    auto n = static_cast<float>(count);
    stats.temp_mean = static_cast<float>(temp_sum / n);
    stats.precip_mean = static_cast<float>(precip_sum / n);
    stats.moisture_mean = static_cast<float>(moisture_sum / n);
    stats.evap_mean = static_cast<float>(evap_sum / n);

    return stats;
}

WeatherStats compute_live_weather_stats(const AtmosphereState& atmo, const DynamicState& dyn,
                                        const Terrain& world) {
    WeatherStats stats;
    if (atmo.cells.empty())
        return stats;

    stats.temp_min = 1e9f;
    stats.temp_max = -1e9f;
    stats.precip_min = 1e9f;
    stats.precip_max = -1e9f;
    stats.moisture_min = 1e9f;
    stats.moisture_max = -1e9f;
    stats.evap_min = 1e9f;
    stats.evap_max = -1e9f;

    double temp_sum = 0.0, precip_sum = 0.0, moisture_sum = 0.0, evap_sum = 0.0;

    // Iterate over terrain tiles and sample atmosphere
    size_t count = static_cast<size_t>(world.width) * world.height;
    for (size_t i = 0; i < count; ++i) {
        auto tx = static_cast<uint32_t>(i % world.width);
        auto ty = static_cast<uint32_t>(i / world.width);

        float T = atmo.sample(tx, ty, &AtmosphereCell::T);
        float q = atmo.sample(tx, ty, &AtmosphereCell::q);
        float u = atmo.sample(tx, ty, &AtmosphereCell::u);
        float v = atmo.sample(tx, ty, &AtmosphereCell::v);
        float storm = atmo.sample(tx, ty, &AtmosphereCell::storminess);

        const auto& dt = dyn.tiles[i];

        stats.temp_min = std::min(stats.temp_min, T);
        stats.temp_max = std::max(stats.temp_max, T);
        temp_sum += T;

        stats.precip_min = std::min(stats.precip_min, dt.effective_precip);
        stats.precip_max = std::max(stats.precip_max, dt.effective_precip);
        precip_sum += dt.effective_precip;

        stats.moisture_min = std::min(stats.moisture_min, q);
        stats.moisture_max = std::max(stats.moisture_max, q);
        moisture_sum += q;

        stats.evap_min = std::min(stats.evap_min, dt.effective_evap);
        stats.evap_max = std::max(stats.evap_max, dt.effective_evap);
        evap_sum += dt.effective_evap;

        float speed = std::sqrt(u * u + v * v);
        if (speed > stats.wind_speed_max)
            stats.wind_speed_max = speed;

        if (storm > 0.3f)
            ++stats.storm_tiles;

        // Dynamic rain shadow: trace upwind for mountains
        const auto& tt = world.tile_at(tx, ty);
        if (!tt.is_ocean && tt.band != ElevBand::Water) {
            if (speed > 0.05f) {
                float udx = -u / speed;
                float udy = -v / speed;
                float fx = static_cast<float>(tx);
                float fy = static_cast<float>(ty);
                float best_shadow = 0.0f;
                for (int step = 1; step <= 12; ++step) {
                    fx += udx;
                    fy += udy;
                    int ux = static_cast<int>(std::round(fx));
                    int uy = static_cast<int>(std::round(fy));
                    if (ux < 0 || ux >= static_cast<int>(world.width) || uy < 0 ||
                        uy >= static_cast<int>(world.height))
                        break;
                    const auto& uptt =
                        world.tile_at(static_cast<uint32_t>(ux), static_cast<uint32_t>(uy));
                    float elev_diff = uptt.elev01 - tt.elev01;
                    if (elev_diff > 0.08f && uptt.elev01 > 0.45f) {
                        float dist_decay = 1.0f - static_cast<float>(step - 1) / 12.0f;
                        float s = std::clamp(elev_diff * 3.0f, 0.0f, 1.0f) * dist_decay;
                        best_shadow = std::max(best_shadow, s);
                    }
                }
                if (best_shadow > 0.2f)
                    ++stats.rain_shadow_tiles;
            }
        }
    }

    auto n = static_cast<float>(count);
    stats.temp_mean = static_cast<float>(temp_sum / n);
    stats.precip_mean = static_cast<float>(precip_sum / n);
    stats.moisture_mean = static_cast<float>(moisture_sum / n);
    stats.evap_mean = static_cast<float>(evap_sum / n);

    return stats;
}

void print_weather_stats(const WeatherStats& stats) {
    std::cout << "\n=== Weather Statistics ===\n";

    auto print_field = [](const char* name, float mn, float mx, float mean) {
        std::cout << "  " << std::left << std::setw(16) << name << "min=" << std::fixed
                  << std::setprecision(2) << std::setw(8) << mn << " max=" << std::setw(8) << mx
                  << " mean=" << std::setw(8) << mean << "\n";
    };

    print_field("Temperature", stats.temp_min, stats.temp_max, stats.temp_mean);
    print_field("Precipitation", stats.precip_min, stats.precip_max, stats.precip_mean);
    print_field("Moisture", stats.moisture_min, stats.moisture_max, stats.moisture_mean);
    print_field("Evaporation", stats.evap_min, stats.evap_max, stats.evap_mean);

    std::cout << "  Wind speed max: " << std::fixed << std::setprecision(2) << stats.wind_speed_max
              << "\n";
    std::cout << "  Storm tiles (>0.3):      " << stats.storm_tiles << "\n";
    std::cout << "  Rain shadow tiles:        " << stats.rain_shadow_tiles << "\n";
    std::cout << "  Wet coast tiles:          " << stats.wet_coast_tiles << "\n\n";
}

}  // namespace sandbox
