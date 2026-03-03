#pragma once

#include "config.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace sandbox {

// ── Biome types (14, Whittaker classification) ──────────────────────────────

enum class Biome {
    // Water
    Ocean,
    Lake,
    // Cold (T < 268K)
    PolarDesert,
    Tundra,
    // Cool (268K <= T < 278K)
    Steppe,
    BorealForest,
    // Temperate (278K <= T < 293K)
    Grassland,
    TemperateForest,
    TemperateRainforest,
    // Hot (T >= 293K)
    Desert,
    Savanna,
    TropicalForest,

    COUNT
};

const char* biome_name(Biome b);
std::array<uint8_t, 3> biome_color(Biome b);

// ── Biome properties ────────────────────────────────────────────────────────

struct BiomeProperties {
    float base_nutrients;
    float regen_rate_per_sec;
    float movement_cost_mult;
};

BiomeProperties biome_properties(Biome biome);

// ── Tile ────────────────────────────────────────────────────────────────────

struct Tile {
    Biome biome = Biome::Ocean;
    float nutrients = 0.0f;
    float regen_rate = 0.0f;
    float toxins = 0.0f;
    float height = 0.0f;
    float temperature_K = 288.0f;
    float moisture = 0.0f;
    float effective_moisture = 0.0f;
    float elevation = 0.0f;  // Terrain relief: 0=flat, ~0.5=hills, ~0.8+=mountains
};

// ── World ───────────────────────────────────────────────────────────────────

struct World {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<Tile> tiles;

    const Tile& tile_at(uint32_t x, uint32_t y) const { return tiles[y * width + x]; }
    Tile& tile_at(uint32_t x, uint32_t y) { return tiles[y * width + x]; }
};

// ── Generation timings ──────────────────────────────────────────────────────

struct GenerationTimings {
    double height_ms = 0.0;
    double elevation_ms = 0.0;
    double temperature_ms = 0.0;
    double moisture_ms = 0.0;
    double continentality_ms = 0.0;
    double orographic_ms = 0.0;
    double effective_moisture_ms = 0.0;
    double toxicity_ms = 0.0;
    double classification_ms = 0.0;
    double total_ms = 0.0;
};

// ── Classification ──────────────────────────────────────────────────────────

Biome classify_biome(float height, float temperature_K, float effective_moisture, float water_level);

// ── Generation ──────────────────────────────────────────────────────────────

World generate_world(uint32_t width, uint32_t height, const EnvParams& env,
                     const Climate& climate, uint32_t seed, GenerationTimings* timings = nullptr);

}  // namespace sandbox
