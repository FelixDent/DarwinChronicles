#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sandbox {

// ── Environment parameters ──────────────────────────────────────────────────

struct EnvParams {
    float water_level = 0.45f;
    float noise_scale = 0.02f;
    float continent_threshold = 0.55f;
    float ridge_strength = 1.0f;
};

// ── Elevation bands ─────────────────────────────────────────────────────────

enum class ElevBand : uint8_t { Water = 0, Lowland = 1, Hills = 2, Mountains = 3 };

const char* elevband_name(ElevBand b);

// ── Terrain tile ────────────────────────────────────────────────────────────

struct TerrainTile {
    float elev01 = 0.0f;  // [0..1] unified height (continent + ridge)
    ElevBand band = ElevBand::Water;
    bool is_ocean = false;
    bool is_lake = false;
    float slope01 = 0.0f;         // [0..1] central difference gradient of unified height
    float aspect = 0.0f;          // [-pi..pi] slope direction (atan2)
    float dist_ocean = 0.0f;      // Euclidean distance to nearest ocean (tiles)
    float dist_water = 0.0f;      // Euclidean distance to any water (ocean+lake)
    float river_flow = 0.0f;      // flow accumulation (log-scaled for display)
    float soil_fertility = 0.0f;  // [0..1] noise - slope + water proximity
    float soil_hold = 0.0f;       // [0..1] material noise + lowland bonus
    float roughness = 0.0f;       // [0..1] local height variance (3x3 stddev)
    float elevation = 0.0f;       // ridge-only component (for renderer decoration)
    int downhill_x = -1;          // neighbor tile for water flow routing
    int downhill_y = -1;
};

// ── Terrain ─────────────────────────────────────────────────────────────────

struct Terrain {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<TerrainTile> tiles;

    const TerrainTile& tile_at(uint32_t x, uint32_t y) const { return tiles[y * width + x]; }
    TerrainTile& tile_at(uint32_t x, uint32_t y) { return tiles[y * width + x]; }
};

// ── Generation timings ──────────────────────────────────────────────────────

struct GenerationTimings {
    double height_ms = 0.0;
    double ridge_ms = 0.0;
    double unify_ms = 0.0;
    double ocean_lake_ms = 0.0;
    double slope_band_ms = 0.0;
    double dist_fields_ms = 0.0;
    double soil_ms = 0.0;
    double roughness_ms = 0.0;
    double downhill_ms = 0.0;
    double river_ms = 0.0;
    double total_ms = 0.0;
};

// ── Generation ──────────────────────────────────────────────────────────────

Terrain generate_terrain(uint32_t width, uint32_t height, const EnvParams& env, uint32_t seed,
                         GenerationTimings* timings = nullptr);

}  // namespace sandbox
