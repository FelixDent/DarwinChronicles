#include "world_gen.h"

#include <FastNoiseLite.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <tuple>

namespace sandbox {

// ── Biome names ─────────────────────────────────────────────────────────────

const char* biome_name(Biome b) {
    switch (b) {
        case Biome::Ocean: return "Ocean";
        case Biome::Lake: return "Lake";
        case Biome::PolarDesert: return "PolarDesert";
        case Biome::Tundra: return "Tundra";
        case Biome::Steppe: return "Steppe";
        case Biome::BorealForest: return "BorealForest";
        case Biome::Grassland: return "Grassland";
        case Biome::TemperateForest: return "TemperateForest";
        case Biome::TemperateRainforest: return "TemperateRainforest";
        case Biome::Desert: return "Desert";
        case Biome::Savanna: return "Savanna";
        case Biome::TropicalForest: return "TropicalForest";
        default: return "Unknown";
    }
}

std::array<uint8_t, 3> biome_color(Biome b) {
    switch (b) {
        case Biome::Ocean: return {10, 30, 90};
        case Biome::Lake: return {70, 140, 200};
        case Biome::PolarDesert: return {210, 220, 230};
        case Biome::Tundra: return {200, 210, 220};
        case Biome::Steppe: return {170, 170, 120};
        case Biome::BorealForest: return {60, 100, 80};
        case Biome::Grassland: return {100, 160, 80};
        case Biome::TemperateForest: return {50, 120, 60};
        case Biome::TemperateRainforest: return {30, 110, 70};
        case Biome::Desert: return {220, 200, 150};
        case Biome::Savanna: return {180, 160, 90};
        case Biome::TropicalForest: return {30, 100, 40};
        default: return {0, 0, 0};
    }
}

// ── Biome properties ────────────────────────────────────────────────────────

BiomeProperties biome_properties(Biome biome) {
    switch (biome) {
        case Biome::Ocean:
        case Biome::Lake: return {0.3f, 0.02f, 2.0f};
        case Biome::PolarDesert: return {0.05f, 0.003f, 1.4f};
        case Biome::Tundra: return {0.2f, 0.008f, 1.3f};
        case Biome::Steppe: return {0.3f, 0.02f, 1.0f};
        case Biome::BorealForest: return {0.5f, 0.025f, 1.2f};
        case Biome::Grassland: return {0.6f, 0.04f, 1.0f};
        case Biome::TemperateForest: return {0.8f, 0.05f, 1.15f};
        case Biome::TemperateRainforest: return {1.0f, 0.06f, 1.25f};
        case Biome::Desert: return {0.05f, 0.005f, 1.4f};
        case Biome::Savanna: return {0.5f, 0.035f, 1.0f};
        case Biome::TropicalForest: return {1.0f, 0.06f, 1.3f};
        default: return {0.0f, 0.0f, 1.0f};
    }
}

// ── Classification (Whittaker biome diagram) ────────────────────────────────
//
//  Temp \ EffMoisture   Dry (<0.25)      Moderate (0.25-0.55)    Wet (>=0.55)
//  ──────────────────────────────────────────────────────────────────────────
//  Cold  (<268K)        PolarDesert      Tundra                  Tundra
//  Cool  (268-278K)     Steppe           BorealForest            BorealForest
//  Temp  (278-293K)     Grassland        TemperateForest         TemperateRainforest
//  Hot   (>=293K)       Desert           Savanna                 TropicalForest

Biome classify_biome(float height, float temperature_K, float effective_moisture,
                     float water_level) {
    // Water biomes (Ocean vs Lake resolved later by flood-fill from edges)
    if (height < water_level) {
        return Biome::Lake;  // Will be promoted to Ocean if edge-connected
    }

    // Land biomes by Whittaker grid: temperature × effective moisture
    if (temperature_K < 268.0f) {
        // Cold zone
        if (effective_moisture < 0.25f) return Biome::PolarDesert;
        return Biome::Tundra;
    } else if (temperature_K < 278.0f) {
        // Cool zone
        if (effective_moisture < 0.25f) return Biome::Steppe;
        return Biome::BorealForest;
    } else if (temperature_K < 293.0f) {
        // Temperate zone
        if (effective_moisture < 0.25f) return Biome::Grassland;
        if (effective_moisture < 0.55f) return Biome::TemperateForest;
        return Biome::TemperateRainforest;
    } else {
        // Hot zone
        if (effective_moisture < 0.25f) return Biome::Desert;
        if (effective_moisture < 0.55f) return Biome::Savanna;
        return Biome::TropicalForest;
    }
}

// ── Noise field helpers ─────────────────────────────────────────────────────

using Clock = std::chrono::high_resolution_clock;

static double elapsed_ms(Clock::time_point start) {
    auto end = Clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

// Height: terrain detail noise + low-frequency continental noise.
// The continental noise creates irregular shapes — multiple landmasses, peninsulas,
// channels, and bays — instead of a single radial blob. A soft edge falloff ensures
// map borders are ocean.
static void generate_height_field(std::vector<float>& out, uint32_t w, uint32_t h, uint32_t seed,
                                  float scale) {
    // Terrain detail: 4-octave Perlin for local hills, valleys, mountains
    FastNoiseLite terrain;
    terrain.SetSeed(static_cast<int>(seed));
    terrain.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    terrain.SetFrequency(1.0f);

    // Continental shape: higher frequency for multiple distinct landmasses.
    // At scale 0.015, features are ~67 tiles wide → 3-4 land/ocean blobs per axis.
    FastNoiseLite continent;
    continent.SetSeed(static_cast<int>(seed + 5000));
    continent.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    continent.SetFrequency(1.0f);
    continent.SetFractalType(FastNoiseLite::FractalType_FBm);
    continent.SetFractalOctaves(2);
    continent.SetFractalLacunarity(2.0f);
    continent.SetFractalGain(0.5f);

    // Coastline detail: mid-frequency noise for peninsulas, bays, fjords
    FastNoiseLite coast;
    coast.SetSeed(static_cast<int>(seed + 6000));
    coast.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    coast.SetFrequency(1.0f);
    coast.SetFractalType(FastNoiseLite::FractalType_FBm);
    coast.SetFractalOctaves(3);
    coast.SetFractalLacunarity(2.0f);
    coast.SetFractalGain(0.5f);

    // Lake noise: creates inland depressions where noise peaks align
    FastNoiseLite lake_noise;
    lake_noise.SetSeed(static_cast<int>(seed + 7000));
    lake_noise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    lake_noise.SetFrequency(1.0f);

    constexpr uint32_t OCTAVES = 4;
    constexpr float PERSISTENCE = 0.5f;

    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            // Terrain detail noise [0, 1]
            float total = 0.0f;
            float amplitude = 1.0f;
            float frequency = 1.0f;
            float max_value = 0.0f;

            for (uint32_t i = 0; i < OCTAVES; ++i) {
                float nx = static_cast<float>(x) * scale * frequency;
                float ny = static_cast<float>(y) * scale * frequency;
                total += terrain.GetNoise(nx, ny) * amplitude;
                max_value += amplitude;
                amplitude *= PERSISTENCE;
                frequency *= 2.0f;
            }
            float terrain_h = (total / max_value + 1.0f) * 0.5f;

            // Continental noise at higher frequency → 3-4 separate landmasses
            float cont = continent.GetNoise(
                static_cast<float>(x) * 0.015f, static_cast<float>(y) * 0.015f);

            // Coastline detail: mid-scale for irregular shores
            float coast_nx = static_cast<float>(x) * 0.025f;
            float coast_ny = static_cast<float>(y) * 0.025f;
            float coast_val = coast.GetNoise(coast_nx, coast_ny);  // ~[-0.7, 0.7]

            // Continental mask: blend of large-scale shape + coastline detail
            // Positive → land, negative → ocean. Coast noise adds peninsulas/bays.
            float land_signal = cont * 0.6f + coast_val * 0.4f;  // ~[-0.7, 0.7]

            // Edge falloff: moderate push toward ocean at borders
            float ex = (static_cast<float>(x) / static_cast<float>(w)) * 2.0f - 1.0f;
            float ey = (static_cast<float>(y) / static_cast<float>(h)) * 2.0f - 1.0f;
            float edge_dist = std::max(std::abs(ex), std::abs(ey));  // 0 center, 1 edge
            float edge_falloff = std::clamp((edge_dist - 0.75f) / 0.25f, 0.0f, 1.0f);
            land_signal -= edge_falloff * 0.5f;

            // Mask for continent shape — steeper multiplier sharpens land/ocean
            // boundary, lower bias creates more ocean for separated continents
            float mask = std::clamp(land_signal * 2.0f + 0.55f, 0.0f, 1.0f);
            mask = mask * mask * (3.0f - 2.0f * mask);  // Smoothstep for sharp coasts

            // Base height from continental mask
            // Land (mask=1): base = 0.62, Ocean (mask=0): base = 0.28
            float base_h = 0.28f + mask * 0.34f;

            // Terrain noise: stronger on land so valleys dip below water level → lakes
            // Land range: 0.60 ± 0.25 → 0.35-0.85 (bottom < water 0.45 → lake)
            // Ocean range: 0.25 ± 0.10 → 0.15-0.35 (all underwater)
            float terrain_weight = 0.10f + mask * 0.40f;
            float terrain_offset = (terrain_h - 0.5f) * terrain_weight;

            // Lake noise: carve basins on land where noise peaks align
            float lnx = static_cast<float>(x) * 0.018f;
            float lny = static_cast<float>(y) * 0.018f;
            float lake_val = lake_noise.GetNoise(lnx, lny);
            // Carve on solid land — isolated depressions become lakes
            float lake_carve = 0.0f;
            if (lake_val > 0.2f && mask > 0.6f) {
                // Strong carve: at lake_val=0.6, carve=0.24 → pushes 0.60 to 0.36
                lake_carve = (lake_val - 0.2f) * 0.6f * mask;
            }

            float final_height = base_h + terrain_offset - lake_carve;

            out[static_cast<size_t>(y) * w + x] = std::clamp(final_height, 0.0f, 1.0f);
        }
    }
}

// Temperature: latitude gradient + elevation lapse + noise
static void generate_temperature_field(std::vector<float>& out, const std::vector<float>& heights,
                                       uint32_t w, uint32_t h, uint32_t seed,
                                       float climate_temp_K) {
    FastNoiseLite noise;
    noise.SetSeed(static_cast<int>(seed + 1000));
    noise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    noise.SetFrequency(1.0f);

    for (uint32_t y = 0; y < h; ++y) {
        // lat_factor: 0 at poles (top/bottom), 1 at equator (middle)
        float lat_normalized = (static_cast<float>(y) / static_cast<float>(h)) - 0.5f;
        float lat_factor = 1.0f - std::abs(lat_normalized * 2.0f);

        for (uint32_t x = 0; x < w; ++x) {
            size_t idx = static_cast<size_t>(y) * w + x;
            float height = heights[idx];

            // Elevation cooling: ~6.5K per km, mapped to height units above sea level
            float elevation_offset = -std::max(height - 0.4f, 0.0f) * 30.0f;

            // Small noise for texture (±4K) — not enough to flip biome zones
            float nx = static_cast<float>(x) * 0.03f;
            float ny = static_cast<float>(y) * 0.03f;
            float noise_offset = noise.GetNoise(nx, ny) * 4.0f;

            // Latitude gradient: ~45K range pole-to-equator
            // Poles: climate_temp - 25K, Equator: climate_temp + 20K
            float base_temp = climate_temp_K + 45.0f * lat_factor - 25.0f;
            out[idx] = base_temp + elevation_offset + noise_offset;
        }
    }
}

// Moisture: rainfall noise (primary) + latitude bias (Hadley cell pattern)
// Water proximity bonus removed — replaced by continentality system.
static void generate_moisture_field(std::vector<float>& out, const std::vector<float>& heights,
                                    uint32_t w, uint32_t h, uint32_t seed, float global_humidity,
                                    float water_level) {
    // Base rainfall from multi-octave noise
    FastNoiseLite rainfall;
    rainfall.SetSeed(static_cast<int>(seed + 2000));
    rainfall.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    rainfall.SetFrequency(1.0f);
    rainfall.SetFractalType(FastNoiseLite::FractalType_FBm);
    rainfall.SetFractalOctaves(3);
    rainfall.SetFractalLacunarity(2.0f);
    rainfall.SetFractalGain(0.5f);

    // Humidity controls the center of the moisture distribution:
    //   humidity 0.0 → mean ~0.10 (desert world)
    //   humidity 0.5 → mean ~0.35 (mixed, forests only in wet noise zones)
    //   humidity 0.7 → mean ~0.45 (earth-like, good spread of dry-to-wet)
    //   humidity 1.0 → mean ~0.60 (jungle world, forests dominant)
    float center = global_humidity * 0.55f + 0.05f;  // Maps [0,1] → [0.05, 0.60]
    constexpr float SPREAD = 0.45f;                   // Wide variation for biome diversity

    for (uint32_t y = 0; y < h; ++y) {
        // Latitude moisture pattern (simplified Hadley cell):
        //   equator (lat_abs 0.0-0.15): wet  (ITCZ, tropical convergence)
        //   subtropics (lat_abs 0.25-0.45): dry  (descending air, desert belt)
        //   mid-latitudes (lat_abs 0.5-0.7): moderate (westerlies bring rain)
        //   poles (lat_abs 0.8-1.0): dry-ish (cold, low evaporation)
        float lat_abs = std::abs((static_cast<float>(y) / static_cast<float>(h)) * 2.0f - 1.0f);
        float equatorial_wet = 0.20f * std::exp(-(lat_abs * lat_abs) / 0.06f);
        float subtropical_dry =
            -0.18f * std::exp(-((lat_abs - 0.35f) * (lat_abs - 0.35f)) / 0.04f);
        float midlat_wet = 0.05f * std::exp(-((lat_abs - 0.60f) * (lat_abs - 0.60f)) / 0.04f);
        float lat_bias = equatorial_wet + subtropical_dry + midlat_wet;

        for (uint32_t x = 0; x < w; ++x) {
            size_t idx = static_cast<size_t>(y) * w + x;

            if (heights[idx] < water_level) {
                out[idx] = 1.0f;  // Water tiles are fully moist
                continue;
            }

            float nx = static_cast<float>(x) * 0.025f;
            float ny = static_cast<float>(y) * 0.025f;
            float noise_val = rainfall.GetNoise(nx, ny);  // ~[-0.7, 0.7]

            float elevation_dry = std::max(heights[idx] - 0.7f, 0.0f) * 0.5f;
            out[idx] = std::clamp(center + noise_val * SPREAD - elevation_dry + lat_bias,
                                  0.0f, 1.0f);
        }
    }
}

// Continentality: BFS distance-to-ocean → coastal areas are wetter, interior is drier.
static void compute_continentality(std::vector<float>& moisture, const std::vector<float>& heights,
                                   uint32_t w, uint32_t h, float water_level) {
    size_t size = static_cast<size_t>(w) * h;
    std::vector<float> ocean_dist(size, -1.0f);
    std::deque<std::pair<uint32_t, uint32_t>> queue;

    // Seed BFS from all water tiles
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            size_t idx = static_cast<size_t>(y) * w + x;
            if (heights[idx] < water_level) {
                ocean_dist[idx] = 0.0f;
                queue.push_back({x, y});
            }
        }
    }

    constexpr int DX[] = {-1, 1, 0, 0};
    constexpr int DY[] = {0, 0, -1, 1};

    while (!queue.empty()) {
        auto [qx, qy] = queue.front();
        queue.pop_front();
        float cur_dist = ocean_dist[static_cast<size_t>(qy) * w + qx];

        for (int d = 0; d < 4; ++d) {
            int nx = static_cast<int>(qx) + DX[d];
            int ny = static_cast<int>(qy) + DY[d];
            if (nx < 0 || ny < 0 || nx >= static_cast<int>(w) || ny >= static_cast<int>(h))
                continue;
            size_t nidx = static_cast<size_t>(ny) * w + static_cast<size_t>(nx);
            if (ocean_dist[nidx] >= 0.0f) continue;
            ocean_dist[nidx] = cur_dist + 1.0f;
            queue.push_back({static_cast<uint32_t>(nx), static_cast<uint32_t>(ny)});
        }
    }

    // Apply continentality modifier: coastal tiles get moisture boost, interior gets dried
    // Normalize distance to [0, 1] range based on map size
    float max_possible = static_cast<float>(w + h) * 0.25f;
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            size_t idx = static_cast<size_t>(y) * w + x;
            if (heights[idx] < water_level) continue;

            float dist_norm = std::clamp(ocean_dist[idx] / max_possible, 0.0f, 1.0f);
            // Coastal bonus (+0.1 at coast), interior penalty (-0.1 deep inland)
            float modifier = 0.1f * (1.0f - 2.0f * dist_norm);
            moisture[idx] = std::clamp(moisture[idx] + modifier, 0.0f, 1.0f);
        }
    }
}

// Orographic effects: prevailing wind pushes moisture against mountains.
// Windward side gets moisture boost, leeward side gets rain shadow.
static void apply_orographic_effects(std::vector<float>& moisture,
                                     const std::vector<float>& elevation,
                                     const std::vector<float>& heights, uint32_t w, uint32_t h,
                                     float wind_direction_deg, float water_level) {
    // Convert wind direction to unit vector (direction wind comes FROM)
    float wind_rad = wind_direction_deg * 3.14159265f / 180.0f;
    float wind_dx = std::cos(wind_rad);
    float wind_dy = std::sin(wind_rad);

    // We sweep in the wind direction: for each tile, look upwind for mountains
    // If mountains are upwind, this tile is in rain shadow (leeward → dry)
    // If this tile IS on the mountain facing the wind, it gets moisture boost (windward)
    size_t size = static_cast<size_t>(w) * h;
    std::vector<float> shadow(size, 0.0f);

    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            size_t idx = static_cast<size_t>(y) * w + x;
            if (heights[idx] < water_level) continue;

            float elev = elevation[idx];

            // Check upwind for high elevation — trace back along wind direction
            float accumulated_shadow = 0.0f;
            for (int step = 1; step <= 15; ++step) {
                // Look upwind (opposite of wind direction)
                int sx = static_cast<int>(x) - static_cast<int>(std::round(wind_dx * step));
                int sy = static_cast<int>(y) - static_cast<int>(std::round(wind_dy * step));
                if (sx < 0 || sy < 0 || sx >= static_cast<int>(w) || sy >= static_cast<int>(h))
                    break;
                size_t sidx = static_cast<size_t>(sy) * w + static_cast<size_t>(sx);
                float upwind_elev = elevation[sidx];
                if (upwind_elev > 0.3f) {
                    // Stronger shadow from taller mountains, decays with distance
                    float strength = upwind_elev * (1.0f - static_cast<float>(step) / 16.0f);
                    accumulated_shadow = std::max(accumulated_shadow, strength);
                }
            }

            // Windward boost: tile has significant elevation and upwind is lower
            float windward_boost = 0.0f;
            if (elev > 0.2f) {
                // Check one step upwind
                int ux = static_cast<int>(x) - static_cast<int>(std::round(wind_dx * 2.0f));
                int uy = static_cast<int>(y) - static_cast<int>(std::round(wind_dy * 2.0f));
                if (ux >= 0 && uy >= 0 && ux < static_cast<int>(w) &&
                    uy < static_cast<int>(h)) {
                    size_t uidx = static_cast<size_t>(uy) * w + static_cast<size_t>(ux);
                    float upwind_elev = elevation[uidx];
                    if (elev > upwind_elev + 0.1f) {
                        windward_boost = (elev - upwind_elev) * 0.3f;
                    }
                }
            }

            // Apply: rain shadow dries, windward wets
            float delta = windward_boost - accumulated_shadow * 0.25f;
            moisture[idx] = std::clamp(moisture[idx] + delta, 0.0f, 1.0f);
        }
    }
}

// Effective moisture: precipitation minus evapotranspiration.
// Hot areas lose more moisture to evaporation, so they need more rainfall to support forests.
// ET = clamp(0.005 * (T - 258), 0, 0.5)
static void compute_effective_moisture(std::vector<float>& effective_moisture,
                                       const std::vector<float>& moisture,
                                       const std::vector<float>& temperatures,
                                       const std::vector<float>& heights, uint32_t w, uint32_t h,
                                       float water_level) {
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            size_t idx = static_cast<size_t>(y) * w + x;

            if (heights[idx] < water_level) {
                effective_moisture[idx] = 1.0f;
                continue;
            }

            float precip = moisture[idx];
            float temp = temperatures[idx];
            float et = std::clamp(0.005f * (temp - 258.0f), 0.0f, 0.5f);
            effective_moisture[idx] = std::clamp(precip - et, 0.0f, 1.0f);
        }
    }
}

// Toxicity: low-frequency noise squared for hotspots
static void generate_toxicity_field(std::vector<float>& out, uint32_t w, uint32_t h, uint32_t seed,
                                    float global_toxicity) {
    FastNoiseLite noise;
    noise.SetSeed(static_cast<int>(seed + 3000));
    noise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    noise.SetFrequency(1.0f);

    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            float nx = static_cast<float>(x) * 0.01f;
            float ny = static_cast<float>(y) * 0.01f;
            float base = noise.GetNoise(nx, ny);
            float toxicity = std::pow(base * 0.5f + 0.5f, 2.0f);
            out[static_cast<size_t>(y) * w + x] =
                std::min(toxicity * global_toxicity, 1.0f);
        }
    }
}

// Elevation: manual ridged noise for mountain ranges + gentler noise for hills.
// This is purely visual — it doesn't affect biome classification, only the
// rendered terrain relief (flat / hills / mountains).
//
// Approach: sample plain Perlin, take abs() and invert to get ridges (the classic
// "ridged multifractal" technique). This produces connected ridge LINES where
// noise crosses zero — they naturally form elongated mountain ranges.
static void generate_elevation_field(std::vector<float>& out, const std::vector<float>& heights,
                                     uint32_t w, uint32_t h, uint32_t seed, float water_level) {
    // Base noise for ridge detection — where this crosses zero we get ridges
    FastNoiseLite noise;
    noise.SetSeed(static_cast<int>(seed + 8000));
    noise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    noise.SetFrequency(1.0f);

    // Second octave at double frequency for detail along ridges
    FastNoiseLite detail;
    detail.SetSeed(static_cast<int>(seed + 8500));
    detail.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    detail.SetFrequency(1.0f);

    // Broad mask: controls WHERE mountain ranges can form
    FastNoiseLite region;
    region.SetSeed(static_cast<int>(seed + 9000));
    region.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    region.SetFrequency(1.0f);

    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            size_t idx = static_cast<size_t>(y) * w + x;

            // No elevation on water tiles
            if (heights[idx] < water_level) {
                out[idx] = 0.0f;
                continue;
            }

            // Manual ridged multifractal: 1 - |noise|
            // Where noise crosses zero → |noise| ≈ 0 → ridge ≈ 1
            // This creates CONNECTED LINES (zero-crossings of Perlin are contiguous)
            float nx = static_cast<float>(x) * 0.02f;
            float ny = static_cast<float>(y) * 0.02f;
            float n1 = noise.GetNoise(nx, ny);         // [-1, 1]
            float ridge1 = 1.0f - std::abs(n1);        // [0, 1], peaks at zero-crossings

            // Second octave: finer ridges add texture and branching
            float n2 = detail.GetNoise(nx * 2.0f, ny * 2.0f);
            float ridge2 = 1.0f - std::abs(n2);

            // Combine: primary ridge shape + secondary detail (weighted)
            float ridge_val = ridge1 * 0.7f + ridge2 * 0.3f;

            // Sharpen: only the strongest ridges become mountains
            // pow(4) creates very sharp falloff — narrow mountain spines
            ridge_val = ridge_val * ridge_val * ridge_val * ridge_val;

            // Region mask: low-frequency noise creates a few isolated mountain zones.
            // Tight threshold — only ~15-20% of land is eligible, forming 2-3 distinct
            // mountain regions per continent rather than broad coverage.
            float region_val = region.GetNoise(static_cast<float>(x) * 0.01f,
                                               static_cast<float>(y) * 0.01f);
            // Only the peaks of region noise allow mountains (val > ~0.1 after shift)
            float region_mask = std::clamp((region_val - 0.1f) * 3.0f, 0.0f, 1.0f);
            // Sharpen the mask so it's either on or off, not gradual
            region_mask = region_mask * region_mask;

            // Suppress at coastlines — mountains build up inland
            float land_depth = (heights[idx] - water_level) / (1.0f - water_level);
            float coast_suppress = std::clamp(land_depth * 3.0f, 0.0f, 1.0f);

            float elev = ridge_val * region_mask * coast_suppress;

            // Scale so that strong ridges reach mountain threshold
            out[idx] = std::clamp(elev * 3.0f, 0.0f, 1.0f);
        }
    }
}

// ── World generation ────────────────────────────────────────────────────────

World generate_world(uint32_t width, uint32_t height, const EnvParams& env, const Climate& climate,
                     uint32_t seed, GenerationTimings* timings) {
    auto total_start = Clock::now();
    size_t size = static_cast<size_t>(width) * height;

    std::vector<float> height_field(size);
    std::vector<float> temp_field(size);
    std::vector<float> moisture_field(size);
    std::vector<float> effective_moisture_field(size);
    std::vector<float> toxicity_field(size);
    std::vector<float> elevation_field(size);

    // 1. Height (continental noise)
    auto t0 = Clock::now();
    generate_height_field(height_field, width, height, seed, env.noise_scale);
    if (timings) timings->height_ms = elapsed_ms(t0);

    // 2. Elevation (moved earlier — orographic effects need it)
    t0 = Clock::now();
    generate_elevation_field(elevation_field, height_field, width, height, seed, env.water_level);
    if (timings) timings->elevation_ms = elapsed_ms(t0);

    // 3. Temperature
    t0 = Clock::now();
    generate_temperature_field(temp_field, height_field, width, height, seed, climate.mean_temp_K);
    if (timings) timings->temperature_ms = elapsed_ms(t0);

    // 4. Moisture (rainfall noise + Hadley cell, no BFS water bonus)
    t0 = Clock::now();
    generate_moisture_field(moisture_field, height_field, width, height, seed, env.humidity,
                            env.water_level);
    if (timings) timings->moisture_ms = elapsed_ms(t0);

    // 5. Continentality (BFS ocean distance → coastal wet / interior dry)
    t0 = Clock::now();
    compute_continentality(moisture_field, height_field, width, height, env.water_level);
    if (timings) timings->continentality_ms = elapsed_ms(t0);

    // 6. Orographic effects (wind sweep, windward boost, leeward shadow)
    t0 = Clock::now();
    apply_orographic_effects(moisture_field, elevation_field, height_field, width, height,
                             env.wind_direction_deg, env.water_level);
    if (timings) timings->orographic_ms = elapsed_ms(t0);

    // 7. Effective moisture (precip - ET(temp))
    t0 = Clock::now();
    compute_effective_moisture(effective_moisture_field, moisture_field, temp_field, height_field,
                               width, height, env.water_level);
    if (timings) timings->effective_moisture_ms = elapsed_ms(t0);

    // 8. Toxicity
    t0 = Clock::now();
    generate_toxicity_field(toxicity_field, width, height, seed, env.toxicity_level);
    if (timings) timings->toxicity_ms = elapsed_ms(t0);

    // 9. Classification + tile assembly (uses effective_moisture for Whittaker grid)
    t0 = Clock::now();
    std::vector<Tile> tiles(size);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            size_t idx = static_cast<size_t>(y) * width + x;
            float h = height_field[idx];
            float t = temp_field[idx];
            float m = moisture_field[idx];
            float em = effective_moisture_field[idx];
            float tox = toxicity_field[idx];

            Biome biome = classify_biome(h, t, em, env.water_level);
            auto props = biome_properties(biome);

            tiles[idx] = Tile{
                .biome = biome,
                .nutrients = props.base_nutrients * env.nutrient_richness,
                .regen_rate = props.regen_rate_per_sec * env.nutrient_richness,
                .toxins = tox,
                .height = h,
                .temperature_K = t,
                .moisture = m,
                .effective_moisture = em,
                .elevation = elevation_field[idx],
            };
        }
    }

    // 10. Ocean/Lake classification: flood-fill connected water components,
    // then promote large bodies (or edge-connected ones above threshold) to Ocean.
    {
        constexpr uint32_t OCEAN_MIN_TILES = 200;

        std::vector<int> component_id(size, -1);
        std::vector<std::vector<size_t>> components;
        std::vector<bool> touches_edge;

        const int dx[] = {-1, 1, 0, 0};
        const int dy[] = {0, 0, -1, 1};

        // Find all connected water components
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                size_t idx = static_cast<size_t>(y) * width + x;
                if (component_id[idx] >= 0) continue;
                if (tiles[idx].biome != Biome::Lake) continue;

                // BFS for this component
                int cid = static_cast<int>(components.size());
                components.emplace_back();
                touches_edge.push_back(false);

                std::deque<size_t> queue;
                component_id[idx] = cid;
                queue.push_back(idx);

                while (!queue.empty()) {
                    size_t cur = queue.front();
                    queue.pop_front();
                    components[static_cast<size_t>(cid)].push_back(cur);

                    auto cx = static_cast<uint32_t>(cur % width);
                    auto cy = static_cast<uint32_t>(cur / width);
                    if (cx == 0 || cx == width - 1 || cy == 0 || cy == height - 1) {
                        touches_edge[static_cast<size_t>(cid)] = true;
                    }

                    for (int d = 0; d < 4; ++d) {
                        int nx = static_cast<int>(cx) + dx[d];
                        int ny = static_cast<int>(cy) + dy[d];
                        if (nx < 0 || nx >= static_cast<int>(width) || ny < 0 ||
                            ny >= static_cast<int>(height))
                            continue;
                        size_t ni = static_cast<size_t>(ny) * width + static_cast<size_t>(nx);
                        if (component_id[ni] >= 0) continue;
                        if (tiles[ni].biome != Biome::Lake) continue;
                        component_id[ni] = cid;
                        queue.push_back(ni);
                    }
                }
            }
        }

        // Promote large, edge-connected components to Ocean
        for (size_t c = 0; c < components.size(); ++c) {
            bool is_ocean =
                touches_edge[c] && components[c].size() >= OCEAN_MIN_TILES;
            if (is_ocean) {
                for (size_t idx : components[c]) {
                    tiles[idx].biome = Biome::Ocean;
                }
            }
        }
    }

    if (timings) timings->classification_ms = elapsed_ms(t0);
    if (timings) timings->total_ms = elapsed_ms(total_start);

    return World{
        .width = width,
        .height = height,
        .tiles = std::move(tiles),
    };
}

}  // namespace sandbox
