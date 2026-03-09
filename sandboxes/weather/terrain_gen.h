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
    uint32_t plate_count = 12;        // 0 = legacy noise mode, >0 = tectonic plates
    float continental_ratio = 0.40f;  // fraction of plates that are continental
};

// ── Elevation bands ─────────────────────────────────────────────────────────

enum class ElevBand : uint8_t { Water = 0, Lowland = 1, Hills = 2, Mountains = 3 };

const char* elevband_name(ElevBand b);

// ── Geological substrate types ──────────────────────────────────────────────

enum class RockType : uint8_t {
    Granite,     // hard, low permeability, shield cores
    Basalt,      // volcanic, moderate permeability
    Limestone,   // soluble, high permeability (karst)
    Sandstone,   // porous, high permeability
    Shale,       // soft, very low permeability
    Metamorphic  // hard, low permeability, mountain belts
};

enum class SoilTexture : uint8_t {
    Sand,  // fast drainage, low retention
    Loam,  // balanced
    Silt,  // moderate drainage
    Clay,  // slow drainage, high retention
    Peat   // high porosity, wetlands
};

const char* rock_type_name(RockType r);
const char* soil_texture_name(SoilTexture s);

// ── Terrain tile ────────────────────────────────────────────────────────────

struct TerrainTile {
    // --- Geometry / classification ---
    float elev01 = 0.0f;  // [0..1] unified height
    ElevBand band = ElevBand::Water;
    bool is_ocean = false;
    float slope01 = 0.0f;     // [0..1] central difference gradient
    float aspect = 0.0f;      // [-pi..pi] slope direction (atan2)
    float dist_ocean = 0.0f;  // Euclidean distance to nearest ocean (tiles)
    float roughness = 0.0f;   // [0..1] local height variance (3x3 stddev)
    float elevation = 0.0f;   // ridge-only component (for renderer decoration)

    // --- Bedrock geology ---
    RockType rock = RockType::Granite;
    float bedrock_hardness = 0.6f;      // [0..1] resistance to erosion/incision
    float bedrock_permeability = 0.2f;  // [0..1] deep infiltration potential
    float rock_blend = 0.5f;            // [0..1] continuous noise for smooth rock color blending
    float tectonic_activity = 0.0f;     // [0..1] how active/deformed (for texture intensity)

    // --- Regolith/soil (root zone) ---
    SoilTexture soil = SoilTexture::Loam;
    float soil_blend = 0.5f;  // [0..1] continuous noise for smooth soil color blending
    float soil_depth = 0.5f;       // [0..3] meters
    float porosity = 0.45f;        // [0.25..0.65] void fraction
    float ksat = 5e-6f;            // saturated hydraulic conductivity (m/s)
    float field_capacity = 0.30f;  // volumetric water content at field capacity
    float wilting_point = 0.12f;   // volumetric water content at wilting point
    float erodibility = 0.3f;      // [0..1] susceptibility to erosion
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
    double ocean_ms = 0.0;
    double slope_band_ms = 0.0;
    double dist_fields_ms = 0.0;
    double geology_ms = 0.0;
    double roughness_ms = 0.0;
    double total_ms = 0.0;
};

// ── Generation ──────────────────────────────────────────────────────────────

Terrain generate_terrain(uint32_t width, uint32_t height, const EnvParams& env, uint32_t seed,
                         GenerationTimings* timings = nullptr);

}  // namespace sandbox
