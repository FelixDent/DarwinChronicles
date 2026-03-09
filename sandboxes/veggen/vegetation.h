#pragma once

#include <cstdint>
#include <random>
#include <vector>

namespace veggen {

// Forward declaration for metrics integration
class MetricsCollector;

// ── Enums ───────────────────────────────────────────────────────────────────

enum class PlantArchetype : uint8_t {
    Grass = 0,
    Shrub,
    Conifer,
    BroadleafTree,
    Succulent,
    Moss,
    Palm,
    TropicalTree,  // tall emergent rainforest canopy — wide flat crown, dense top foliage
};
constexpr int NUM_ARCHETYPES = 8;

enum class GrowthPhase : uint8_t { Small = 0, Medium = 1, Large = 2 };

enum class PlantHealth : uint8_t { Healthy = 0, Stressed = 1, Brown = 2, Dead = 3 };

// ── Data Structures ─────────────────────────────────────────────────────────

struct PlantTraits {
    PlantArchetype archetype;
    float growth_speed;    // biomass/day at ideal conditions
    float water_need;      // 0..1
    float drought_tol;     // 0..1
    float temp_min_c;      // minimum viable temperature
    float temp_opt_c;      // optimal temperature
    float temp_max_c;      // maximum viable temperature
    float fertility_need;  // 0..1
    float
        cluster_affinity;  // -1..1: positive = gregarious (likes own kind), negative = territorial
    uint32_t sprite_seed;
    uint8_t base_hue;     // 0..255
    uint8_t leafiness;    // 0..255
    uint8_t branchiness;  // 0..255
};

struct PlantInstance {
    PlantTraits traits;
    float age_days = 0;
    float biomass = 0;  // 0..1 -> determines phase
    GrowthPhase phase = GrowthPhase::Small;
    float water_reserve = 0.5f;
    float stress = 0;
    PlantHealth health = PlantHealth::Healthy;
    float dead_timer = 0;     // days since death
    float max_age_days = 0;   // lifespan — dies of senescence when exceeded
    uint8_t death_cause = 0;  // DeathCause enum stored as uint8_t
    int tile_x = 0;
    int tile_y = 0;
    float offset_x = 0;  // sub-tile position for rendering variety
    float offset_y = 0;
};

struct TileState {
    // Terrain (generated at init, static after)
    float elev01 = 0.5f;
    float slope01 = 0.0f;
    float soil_fertility = 0.5f;
    float soil_hold = 0.5f;
    bool is_water = false;

    // Spatial offsets (generated at init from noise + elevation)
    float temp_offset = 0;        // lapse rate + micro-noise (°C)
    float orographic_factor = 1;  // precip multiplier from elevation/slope/windward
    int downhill_x = -1;          // neighbor tile for runoff routing (-1 = none/edge)
    int downhill_y = -1;

    // Climate (varies with season, now per-tile)
    float temperature = 15.0f;
    float precipitation = 0.5f;
    float evap_demand = 0.0f;

    // Two-layer moisture model
    float surface_water = 0;     // 0..1+: fills from rain, drains via runoff + infiltration
    float root_moisture = 0.5f;  // 0..1: what plants actually use, fills from infiltration

    // Convenience: total available moisture seen by external code (renderer, metrics)
    float soil_moisture() const { return root_moisture; }
    // Settable alias for init
    void set_moisture(float m) {
        root_moisture = m;
        surface_water = m * 0.2f;
    }

    // Plant feedback (recomputed each tick)
    float canopy_cover = 0;  // 0..1: fraction of tile shaded by plants
};

struct BiomePreset {
    const char* name;
    // Terrain
    float elev01;
    float slope01;
    float soil_fertility;
    float soil_hold;
    bool is_water;
    // Climate base + seasonal amplitude
    float temp_base;
    float temp_amplitude;
    float precip_base;
    float precip_amplitude;
    // Which archetypes are viable (weights)
    float archetype_weights[NUM_ARCHETYPES];  // indexed by PlantArchetype
    int max_plants_per_tile;
    // Ground color (RGB)
    uint8_t ground_r, ground_g, ground_b;
};

// ── Constants ───────────────────────────────────────────────────────────────

constexpr int GRID_SIZE = 3;
constexpr int NUM_PRESETS = 8;
constexpr float DAYS_PER_YEAR = 365.0f;
constexpr float DEAD_REMOVAL_DAYS = 15.0f;

// ── Biome presets ───────────────────────────────────────────────────────────

extern const BiomePreset BIOME_PRESETS[NUM_PRESETS];

// ── Simulation state ────────────────────────────────────────────────────────

struct SimState {
    TileState tiles[GRID_SIZE][GRID_SIZE];
    std::vector<PlantInstance> plants;
    int preset_index = 0;
    float elapsed_days = 0;
    float time_scale = 1.0f;
    bool paused = false;
    std::mt19937 rng;

    // Dominant species for clustering — picked on init from preset weights
    PlantArchetype dominant_species[2] = {PlantArchetype::Grass, PlantArchetype::Grass};
    float spawn_accumulator_ = 0;

    // Metrics integration (optional — set externally, not owned)
    MetricsCollector* metrics = nullptr;

    void init(int preset_idx, uint32_t seed);
    void reset_plants();
    void update(float dt_seconds);

private:
    void generate_terrain();  // elevation, slope, spatial offsets, downhill routing
    void update_weather();
    void update_hydrology(float dt_days);  // surface water, infiltration, runoff
    void update_canopy_cover();            // plant→environment feedback
    void spawn_plants();
    void reproduce_plants();  // mature plants produce offspring
    void update_plants(float dt_days);
    float compute_suitability(const PlantTraits& traits, const TileState& tile) const;
    PlantTraits generate_traits(PlantArchetype archetype);
    int count_plants_on_tile(int tx, int ty) const;
    void pick_dominant_species();
    float generate_max_age(PlantArchetype arch);
};

// ── Helpers ─────────────────────────────────────────────────────────────────

const char* archetype_name(PlantArchetype a);
const char* phase_name(GrowthPhase p);
const char* health_name(PlantHealth h);
const char* season_name(float elapsed_days);

}  // namespace veggen
