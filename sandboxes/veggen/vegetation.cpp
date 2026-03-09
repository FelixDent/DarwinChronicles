#include "vegetation.h"

#include <algorithm>
#include <cmath>

#include "metrics.h"

namespace veggen {

// ── Biome presets ───────────────────────────────────────────────────────────
// Format: name, elev, slope, fertility, hold, water,
//         temp_base, temp_amp, precip_base, precip_amp,
//         archetype_weights[8] {Grass, Shrub, Conifer, Broadleaf, Succulent, Moss, Palm, Tropical},
//         max_plants, ground_r, ground_g, ground_b

const BiomePreset BIOME_PRESETS[NUM_PRESETS] = {
    {"Tropical Rainforest",
     0.4f,
     0.1f,
     0.8f,
     0.7f,
     false,
     27.0f,
     3.0f,
     0.9f,
     0.1f,
     {0.1f, 0.3f, 0.0f, 0.4f, 0.0f, 0.1f, 0.5f, 0.8f},
     6,
     30,
     90,
     20},

    {"Savanna",
     0.35f,
     0.05f,
     0.5f,
     0.4f,
     false,
     25.0f,
     8.0f,
     0.4f,
     0.3f,
     {0.8f, 0.4f, 0.0f, 0.2f, 0.0f, 0.0f, 0.2f, 0.0f},
     4,
     160,
     140,
     60},

    {"Temperate Forest",
     0.45f,
     0.15f,
     0.7f,
     0.6f,
     false,
     12.0f,
     12.0f,
     0.6f,
     0.2f,
     {0.2f, 0.3f, 0.4f, 0.7f, 0.0f, 0.2f, 0.0f, 0.0f},
     4,
     50,
     100,
     30},

    {"Boreal",
     0.5f,
     0.1f,
     0.4f,
     0.5f,
     false,
     0.0f,
     15.0f,
     0.5f,
     0.1f,
     {0.1f, 0.1f, 0.8f, 0.1f, 0.0f, 0.4f, 0.0f, 0.0f},
     3,
     40,
     70,
     40},

    {"Desert",
     0.3f,
     0.05f,
     0.2f,
     0.2f,
     false,
     30.0f,
     10.0f,
     0.1f,
     0.05f,
     {0.1f, 0.0f, 0.0f, 0.0f, 0.6f, 0.0f, 0.0f, 0.0f},
     2,
     200,
     180,
     120},

    {"Tundra",
     0.6f,
     0.05f,
     0.2f,
     0.3f,
     false,
     -5.0f,
     10.0f,
     0.3f,
     0.1f,
     {0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 0.6f, 0.0f, 0.0f},
     3,
     200,
     210,
     220},

    {"Alpine Meadow",
     0.7f,
     0.2f,
     0.5f,
     0.4f,
     false,
     5.0f,
     10.0f,
     0.5f,
     0.2f,
     {0.6f, 0.1f, 0.0f, 0.0f, 0.0f, 0.4f, 0.0f, 0.0f},
     4,
     100,
     140,
     70},

    {"Wetland",
     0.2f,
     0.02f,
     0.6f,
     0.8f,
     false,
     15.0f,
     10.0f,
     0.8f,
     0.1f,
     {0.5f, 0.3f, 0.0f, 0.0f, 0.0f, 0.4f, 0.0f, 0.0f},
     4,
     50,
     80,
     50},
};

// ── Helpers ─────────────────────────────────────────────────────────────────

const char* archetype_name(PlantArchetype a) {
    switch (a) {
        case PlantArchetype::Grass:
            return "Grass";
        case PlantArchetype::Shrub:
            return "Shrub";
        case PlantArchetype::Conifer:
            return "Conifer";
        case PlantArchetype::BroadleafTree:
            return "Broadleaf";
        case PlantArchetype::Succulent:
            return "Succulent";
        case PlantArchetype::Moss:
            return "Moss";
        case PlantArchetype::Palm:
            return "Palm";
        case PlantArchetype::TropicalTree:
            return "Tropical";
    }
    return "Unknown";
}

const char* phase_name(GrowthPhase p) {
    switch (p) {
        case GrowthPhase::Small:
            return "Small";
        case GrowthPhase::Medium:
            return "Medium";
        case GrowthPhase::Large:
            return "Large";
    }
    return "Unknown";
}

const char* health_name(PlantHealth h) {
    switch (h) {
        case PlantHealth::Healthy:
            return "Healthy";
        case PlantHealth::Stressed:
            return "Stressed";
        case PlantHealth::Brown:
            return "Brown";
        case PlantHealth::Dead:
            return "Dead";
    }
    return "Unknown";
}

const char* season_name(float elapsed_days) {
    float season_t = std::fmod(elapsed_days, DAYS_PER_YEAR) / DAYS_PER_YEAR;
    if (season_t < 0.25f)
        return "Spring";
    if (season_t < 0.5f)
        return "Summer";
    if (season_t < 0.75f)
        return "Autumn";
    return "Winter";
}

// ── SimState ────────────────────────────────────────────────────────────────

void SimState::pick_dominant_species() {
    const auto& preset = BIOME_PRESETS[preset_index];

    // Build list of viable archetypes sorted by weight (descending)
    struct Entry {
        PlantArchetype arch;
        float weight;
    };
    Entry viable[NUM_ARCHETYPES];
    int count = 0;
    for (int i = 0; i < NUM_ARCHETYPES; ++i) {
        if (preset.archetype_weights[i] > 0.01f) {
            viable[count++] = {static_cast<PlantArchetype>(i), preset.archetype_weights[i]};
        }
    }

    // Sort by weight descending
    for (int i = 0; i < count - 1; ++i) {
        for (int j = i + 1; j < count; ++j) {
            if (viable[j].weight > viable[i].weight)
                std::swap(viable[i], viable[j]);
        }
    }

    // Pick top 1-2 as dominant, with small random chance to swap in a lower one
    dominant_species[0] = (count > 0) ? viable[0].arch : PlantArchetype::Grass;
    dominant_species[1] = (count > 1) ? viable[1].arch : dominant_species[0];

    // Small chance (20%) to promote a rarer species for variety across resets
    std::uniform_int_distribution<int> roll(0, 4);
    if (count > 2 && roll(rng) == 0) {
        std::uniform_int_distribution<int> pick(2, count - 1);
        dominant_species[1] = viable[pick(rng)].arch;
    }
}

void SimState::init(int preset_idx, uint32_t seed) {
    preset_index = preset_idx;
    elapsed_days = 0;
    spawn_accumulator_ = 0;
    rng.seed(seed);
    plants.clear();

    generate_terrain();
    pick_dominant_species();
    update_weather();
    spawn_plants();
}

void SimState::generate_terrain() {
    const auto& preset = BIOME_PRESETS[preset_index];

    // Simple hash for deterministic per-tile noise
    auto tile_hash = [](int x, int y, uint32_t seed) -> float {
        uint32_t h = static_cast<uint32_t>(x * 374761393 + y * 668265263) + seed;
        h = (h ^ (h >> 13)) * 1274126177u;
        h ^= h >> 16;
        return static_cast<float>(h & 0xFFFF) / 65535.0f;  // 0..1
    };

    uint32_t terrain_seed = static_cast<uint32_t>(rng());

    // Generate elevation with gradient + noise
    // Gradient: higher toward top-left, lower toward bottom-right (creates drainage pattern)
    for (int y = 0; y < GRID_SIZE; ++y) {
        for (int x = 0; x < GRID_SIZE; ++x) {
            auto& tile = tiles[y][x];

            // Elevation: preset base + gradient + noise
            float gradient =
                (static_cast<float>(GRID_SIZE - 1 - y) + static_cast<float>(GRID_SIZE - 1 - x)) /
                static_cast<float>(GRID_SIZE * 2 - 2);  // 0..1, high at top-left
            float noise = (tile_hash(x, y, terrain_seed) - 0.5f) * 0.15f;
            tile.elev01 = std::clamp(preset.elev01 + (gradient - 0.5f) * 0.3f + noise, 0.0f, 1.0f);

            // Slope from elevation differences with neighbors
            float max_diff = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0)
                        continue;
                    int nx = x + dx, ny = y + dy;
                    if (nx < 0 || nx >= GRID_SIZE || ny < 0 || ny >= GRID_SIZE)
                        continue;
                    float ne = preset.elev01 +
                               ((static_cast<float>(GRID_SIZE - 1 - ny) +
                                 static_cast<float>(GRID_SIZE - 1 - nx)) /
                                    static_cast<float>(GRID_SIZE * 2 - 2) -
                                0.5f) *
                                   0.3f +
                               (tile_hash(nx, ny, terrain_seed) - 0.5f) * 0.15f;
                    max_diff = std::max(max_diff, std::abs(tile.elev01 - ne));
                }
            }
            tile.slope01 = std::clamp(preset.slope01 + max_diff * 2.0f, 0.0f, 1.0f);

            tile.soil_fertility = std::clamp(
                preset.soil_fertility + (tile_hash(x, y, terrain_seed + 100) - 0.5f) * 0.15f, 0.0f,
                1.0f);
            tile.soil_hold = std::clamp(
                preset.soil_hold + (tile_hash(x, y, terrain_seed + 200) - 0.5f) * 0.1f, 0.1f, 1.0f);
            tile.is_water = preset.is_water;

            // Temperature offset: lapse rate (−6.5°C per 1000m, scaled to 0..1 elev)
            // + micro-noise for local variation
            float lapse = -(tile.elev01 - preset.elev01) * 6.5f;
            float micro = (tile_hash(x, y, terrain_seed + 300) - 0.5f) * 2.0f;  // ±1°C
            tile.temp_offset = lapse + micro;

            // Orographic precipitation factor
            // Wind from west (left): tiles on windward side (low x) get more rain
            // Higher elevation = more orographic lift = more rain on windward, less on lee
            float windward = 1.0f - static_cast<float>(x) / static_cast<float>(GRID_SIZE - 1);
            float uplift = tile.elev01 * 0.5f;
            float rain_shadow = (1.0f - windward) * tile.elev01 * 0.3f;
            tile.orographic_factor = std::clamp(1.0f + windward * uplift - rain_shadow, 0.5f, 1.5f);

            // Initialize moisture
            tile.set_moisture(0.5f);

            // Find downhill neighbor
            tile.downhill_x = -1;
            tile.downhill_y = -1;
            float lowest = tile.elev01;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0)
                        continue;
                    int nx = x + dx, ny = y + dy;
                    if (nx < 0 || nx >= GRID_SIZE || ny < 0 || ny >= GRID_SIZE)
                        continue;
                    // Use same formula as above to get neighbor elev before tiles array is filled
                    float ne = preset.elev01 +
                               ((static_cast<float>(GRID_SIZE - 1 - ny) +
                                 static_cast<float>(GRID_SIZE - 1 - nx)) /
                                    static_cast<float>(GRID_SIZE * 2 - 2) -
                                0.5f) *
                                   0.3f +
                               (tile_hash(nx, ny, terrain_seed) - 0.5f) * 0.15f;
                    if (ne < lowest) {
                        lowest = ne;
                        tile.downhill_x = nx;
                        tile.downhill_y = ny;
                    }
                }
            }
        }
    }
}

void SimState::reset_plants() {
    plants.clear();
    spawn_plants();
}

void SimState::update(float dt_seconds) {
    if (paused)
        return;

    // Convert real seconds to sim days based on time scale
    // Default: 1 year = ~30 seconds at 1x, so 365 days / 30 sec = ~12.17 days/sec
    constexpr float DAYS_PER_SECOND = DAYS_PER_YEAR / 30.0f;
    float dt_days = dt_seconds * time_scale * DAYS_PER_SECOND;

    elapsed_days += dt_days;
    update_weather();
    update_canopy_cover();
    update_hydrology(dt_days);

    // Spawn plants periodically, not every frame — avoids flooding with fast-growers
    spawn_accumulator_ += dt_days;
    constexpr float SPAWN_INTERVAL_DAYS = 5.0f;
    if (spawn_accumulator_ >= SPAWN_INTERVAL_DAYS) {
        spawn_accumulator_ -= SPAWN_INTERVAL_DAYS;
        spawn_plants();
        reproduce_plants();
    }

    update_plants(dt_days);

    if (metrics)
        metrics->on_update(*this, dt_days);
}

void SimState::update_weather() {
    const auto& preset = BIOME_PRESETS[preset_index];
    float season_t = std::fmod(elapsed_days, DAYS_PER_YEAR) / DAYS_PER_YEAR;
    constexpr float PI2 = 6.2831853f;

    // Seasonal baselines
    float base_temp = preset.temp_base + preset.temp_amplitude * std::sin(PI2 * season_t);
    float base_precip =
        preset.precip_base + preset.precip_amplitude * std::sin(PI2 * (season_t - 0.25f));
    base_precip = std::clamp(base_precip, 0.0f, 1.0f);

    for (int y = 0; y < GRID_SIZE; ++y) {
        for (int x = 0; x < GRID_SIZE; ++x) {
            auto& tile = tiles[y][x];

            // Per-tile temperature: seasonal base + elevation lapse + micro-noise
            tile.temperature = base_temp + tile.temp_offset;

            // Per-tile precipitation: seasonal base * orographic factor
            tile.precipitation = std::clamp(base_precip * tile.orographic_factor, 0.0f, 1.0f);

            // Evap: driven by temperature, suppressed by humidity, reduced by canopy shade
            float humidity_suppression = 1.0f - tile.precipitation * 0.7f;
            float shade_reduction =
                1.0f - tile.canopy_cover * 0.4f;  // canopy reduces evap up to 40%
            tile.evap_demand = std::clamp(
                0.02f * std::max(0.0f, tile.temperature) * humidity_suppression * shade_reduction,
                0.0f, 1.0f);
        }
    }
}

void SimState::update_canopy_cover() {
    // Compute canopy cover per tile from living plants
    for (int y = 0; y < GRID_SIZE; ++y)
        for (int x = 0; x < GRID_SIZE; ++x)
            tiles[y][x].canopy_cover = 0;

    for (const auto& plant : plants) {
        if (plant.health == PlantHealth::Dead)
            continue;
        auto& tile = tiles[plant.tile_y][plant.tile_x];
        // Canopy contribution: trees contribute more than grass/moss
        float contribution;
        switch (plant.traits.archetype) {
            case PlantArchetype::BroadleafTree:
            case PlantArchetype::Conifer:
            case PlantArchetype::TropicalTree:
                contribution = plant.biomass * 0.25f;
                break;
            case PlantArchetype::Palm:
                contribution = plant.biomass * 0.15f;
                break;
            case PlantArchetype::Shrub:
                contribution = plant.biomass * 0.10f;
                break;
            default:
                contribution = plant.biomass * 0.03f;
                break;
        }
        tile.canopy_cover += contribution;
    }

    for (int y = 0; y < GRID_SIZE; ++y)
        for (int x = 0; x < GRID_SIZE; ++x)
            tiles[y][x].canopy_cover = std::clamp(tiles[y][x].canopy_cover, 0.0f, 1.0f);
}

void SimState::update_hydrology(float dt_days) {
    // Normalize rate: dt_days can vary, scale consistently
    float rate = dt_days * 0.05f;

    for (int y = 0; y < GRID_SIZE; ++y) {
        for (int x = 0; x < GRID_SIZE; ++x) {
            auto& tile = tiles[y][x];

            // Rain adds to surface water
            tile.surface_water += tile.precipitation * rate * 2.0f;

            // Infiltration: surface → root, depends on soil_hold and slope
            // Steeper slope = less infiltration (water runs off before soaking in)
            float infiltration_rate = tile.soil_hold * (1.0f - tile.slope01 * 0.6f);
            float infiltration = std::min(tile.surface_water, infiltration_rate * rate);
            tile.surface_water -= infiltration;
            tile.root_moisture += infiltration;

            // Evaporation removes from surface first, then root
            float evap_amount = tile.evap_demand * rate;
            float from_surface = std::min(tile.surface_water, evap_amount * 0.7f);
            tile.surface_water -= from_surface;
            float remaining_evap = evap_amount - from_surface;
            tile.root_moisture -= std::min(tile.root_moisture, remaining_evap);

            // Plant water uptake from root moisture
            float plant_uptake = 0.0f;
            for (const auto& plant : plants) {
                if (plant.tile_x == x && plant.tile_y == y && plant.health != PlantHealth::Dead) {
                    plant_uptake += plant.biomass * plant.traits.water_need * 0.1f;
                }
            }
            tile.root_moisture -= std::min(tile.root_moisture, plant_uptake * rate);

            // Clamp
            tile.surface_water = std::max(tile.surface_water, 0.0f);
            tile.root_moisture = std::clamp(tile.root_moisture, 0.0f, 1.0f);
        }
    }

    // Runoff pass: surface water flows downhill
    // Use a temporary buffer to avoid order-dependent artifacts
    float runoff_out[GRID_SIZE][GRID_SIZE] = {};
    float runoff_in[GRID_SIZE][GRID_SIZE] = {};

    for (int y = 0; y < GRID_SIZE; ++y) {
        for (int x = 0; x < GRID_SIZE; ++x) {
            auto& tile = tiles[y][x];
            // Runoff rate: steeper slope = more runoff
            float runoff_frac = std::clamp(tile.slope01 * 0.5f + 0.1f, 0.0f, 0.8f);
            float runoff = tile.surface_water * runoff_frac * rate * 3.0f;
            runoff = std::min(runoff, tile.surface_water);

            if (runoff > 0 && tile.downhill_x >= 0) {
                runoff_out[y][x] += runoff;
                runoff_in[tile.downhill_y][tile.downhill_x] += runoff;
            }
        }
    }

    // Apply runoff
    for (int y = 0; y < GRID_SIZE; ++y) {
        for (int x = 0; x < GRID_SIZE; ++x) {
            tiles[y][x].surface_water -= runoff_out[y][x];
            tiles[y][x].surface_water += runoff_in[y][x];
            tiles[y][x].surface_water = std::max(tiles[y][x].surface_water, 0.0f);
        }
    }
}

int SimState::count_plants_on_tile(int tx, int ty) const {
    int count = 0;
    for (const auto& p : plants) {
        if (p.tile_x == tx && p.tile_y == ty && p.health != PlantHealth::Dead)
            ++count;
    }
    return count;
}

float SimState::compute_suitability(const PlantTraits& traits, const TileState& tile) const {
    // Temperature suitability
    float temp_suit = 0.0f;
    if (tile.temperature >= traits.temp_min_c && tile.temperature <= traits.temp_max_c) {
        float dist_to_opt = std::abs(tile.temperature - traits.temp_opt_c);
        float range = std::max(traits.temp_max_c - traits.temp_min_c, 1.0f);
        temp_suit = 1.0f - (dist_to_opt / range);
        temp_suit = std::clamp(temp_suit, 0.0f, 1.0f);
    }

    // Moisture suitability
    float moist_suit =
        std::clamp(tile.soil_moisture() / std::max(traits.water_need, 0.1f), 0.0f, 1.0f);

    // Fertility suitability
    float fert_suit =
        std::clamp(tile.soil_fertility / std::max(traits.fertility_need, 0.1f), 0.0f, 1.0f);

    return temp_suit * 0.4f + moist_suit * 0.35f + fert_suit * 0.25f;
}

PlantTraits SimState::generate_traits(PlantArchetype archetype) {
    PlantTraits traits{};
    traits.archetype = archetype;
    traits.sprite_seed = static_cast<uint32_t>(rng());

    std::uniform_real_distribution<float> var(-0.1f, 0.1f);
    std::uniform_int_distribution<int> hue_var(-10, 10);
    std::uniform_int_distribution<int> byte_dist(100, 255);

    // Climate-driven hue bias:
    //   Hot+wet → rich dark green (low hue ~70-85)
    //   Cold → blue-green (high hue ~110-130)
    //   Hot+dry → yellow-green/olive (low hue ~55-70)
    //   Temperate → standard green (~85-100)
    const auto& preset = BIOME_PRESETS[preset_index];
    float temp_norm = std::clamp((preset.temp_base + 10.0f) / 50.0f, 0.0f, 1.0f);  // 0=cold, 1=hot
    float wet_norm = std::clamp(preset.precip_base, 0.0f, 1.0f);                   // 0=dry, 1=wet

    // Base hue center per archetype, then shifted by climate
    // Cold → +30 (bluer), hot+dry → -20 (yellower), hot+wet → -5 (rich green)
    int climate_hue_shift = static_cast<int>((1.0f - temp_norm) * 25.0f - wet_norm * 5.0f +
                                             (1.0f - wet_norm) * temp_norm * -15.0f);

    // Leafiness biased by precipitation (wetter = denser foliage)
    int leaf_bias = static_cast<int>(wet_norm * 80.0f);

    // growth_speed = biomass gained per day at perfect conditions (no 0.01 multiplier).
    // Realistic maturation times:
    //   Grass: ~60 days    → speed ≈ 1/60 = 0.017
    //   Moss:  ~120 days   → speed ≈ 1/120 = 0.008
    //   Shrub: ~2 years    → speed ≈ 1/730 = 0.0014
    //   Succulent: ~5 yrs  → speed ≈ 1/1825 = 0.00055
    //   Broadleaf: ~15 yrs → speed ≈ 1/5475 = 0.00018
    //   Conifer: ~20 yrs   → speed ≈ 1/7300 = 0.00014
    std::uniform_real_distribution<float> speed_var(0.8f, 1.2f);  // ±20% variation

    switch (archetype) {
        case PlantArchetype::Grass:
            traits.growth_speed = 0.017f * speed_var(rng);
            traits.water_need = 0.4f + var(rng);
            traits.drought_tol = 0.5f + var(rng);
            traits.temp_min_c = -5.0f;
            traits.temp_opt_c = 20.0f;
            traits.temp_max_c = 40.0f;
            traits.fertility_need = 0.3f;
            traits.cluster_affinity = 0.8f;  // highly gregarious — forms meadows
            traits.base_hue =
                static_cast<uint8_t>(std::clamp(85 + climate_hue_shift + hue_var(rng), 30, 180));
            traits.leafiness =
                static_cast<uint8_t>(std::clamp(120 + leaf_bias + hue_var(rng), 60, 255));
            traits.branchiness = 0;
            break;
        case PlantArchetype::Shrub:
            traits.growth_speed = 0.0014f * speed_var(rng);
            traits.water_need = 0.5f + var(rng);
            traits.drought_tol = 0.4f + var(rng);
            traits.temp_min_c = -2.0f;
            traits.temp_opt_c = 22.0f;
            traits.temp_max_c = 38.0f;
            traits.fertility_need = 0.4f;
            traits.cluster_affinity = 0.3f;  // moderate — forms thickets
            traits.base_hue =
                static_cast<uint8_t>(std::clamp(80 + climate_hue_shift + hue_var(rng), 30, 180));
            traits.leafiness =
                static_cast<uint8_t>(std::clamp(130 + leaf_bias + hue_var(rng), 80, 255));
            traits.branchiness = static_cast<uint8_t>(std::clamp(byte_dist(rng), 0, 255));
            break;
        case PlantArchetype::Conifer:
            traits.growth_speed = 0.00014f * speed_var(rng);
            traits.water_need = 0.4f + var(rng);
            traits.drought_tol = 0.5f + var(rng);
            traits.temp_min_c = -20.0f;
            traits.temp_opt_c = 10.0f;
            traits.temp_max_c = 30.0f;
            traits.fertility_need = 0.3f;
            traits.cluster_affinity = 0.5f;  // gregarious — forms dense stands
            // Conifers are always blue-green, climate shifts them further
            traits.base_hue = static_cast<uint8_t>(
                std::clamp(110 + climate_hue_shift / 2 + hue_var(rng), 80, 180));
            traits.leafiness = static_cast<uint8_t>(std::clamp(150 + leaf_bias / 2, 100, 255));
            traits.branchiness = static_cast<uint8_t>(std::clamp(byte_dist(rng), 0, 255));
            break;
        case PlantArchetype::BroadleafTree:
            traits.growth_speed = 0.00018f * speed_var(rng);
            traits.water_need = 0.5f + var(rng);
            traits.drought_tol = 0.4f + var(rng);
            traits.temp_min_c = 0.0f;
            traits.temp_opt_c = 22.0f;
            traits.temp_max_c = 38.0f;
            traits.fertility_need = 0.5f;
            traits.cluster_affinity = -0.3f;  // territorial — large canopy needs space
            traits.base_hue =
                static_cast<uint8_t>(std::clamp(85 + climate_hue_shift + hue_var(rng), 30, 170));
            traits.leafiness =
                static_cast<uint8_t>(std::clamp(140 + leaf_bias + hue_var(rng), 100, 255));
            traits.branchiness = static_cast<uint8_t>(byte_dist(rng));
            break;
        case PlantArchetype::Succulent:
            traits.growth_speed = 0.00055f * speed_var(rng);
            traits.water_need = 0.1f + var(rng);
            traits.drought_tol = 0.9f + var(rng) * 0.5f;
            traits.temp_min_c = 5.0f;
            traits.temp_opt_c = 30.0f;
            traits.temp_max_c = 50.0f;
            traits.fertility_need = 0.1f;
            traits.cluster_affinity = -0.6f;  // very territorial — spaced apart in desert
            // Succulents: gray-green to teal, barely shifted by climate
            traits.base_hue = static_cast<uint8_t>(std::clamp(125 + hue_var(rng), 100, 170));
            traits.leafiness = static_cast<uint8_t>(std::clamp(byte_dist(rng) / 2, 0, 255));
            traits.branchiness = 0;
            break;
        case PlantArchetype::Moss:
            traits.growth_speed = 0.008f * speed_var(rng);
            traits.water_need = 0.6f + var(rng);
            traits.drought_tol = 0.2f + var(rng);
            traits.temp_min_c = -15.0f;
            traits.temp_opt_c = 10.0f;
            traits.temp_max_c = 25.0f;
            traits.fertility_need = 0.2f;
            traits.cluster_affinity = 0.9f;  // extremely gregarious — spreads in dense mats
            // Cold moss: gray-olive; wet moss: rich green
            traits.base_hue =
                static_cast<uint8_t>(std::clamp(90 + climate_hue_shift + hue_var(rng), 40, 160));
            traits.leafiness = static_cast<uint8_t>(std::clamp(120 + leaf_bias, 80, 255));
            traits.branchiness = 0;
            break;
        case PlantArchetype::Palm:
            traits.growth_speed = 0.00020f * speed_var(rng);
            traits.water_need = 0.4f + var(rng);
            traits.drought_tol = 0.5f + var(rng);
            traits.temp_min_c = 5.0f;
            traits.temp_opt_c = 28.0f;
            traits.temp_max_c = 45.0f;
            traits.fertility_need = 0.3f;
            traits.cluster_affinity = 0.1f;  // loosely grouped
            traits.base_hue =
                static_cast<uint8_t>(std::clamp(80 + climate_hue_shift + hue_var(rng), 40, 160));
            traits.leafiness =
                static_cast<uint8_t>(std::clamp(160 + leaf_bias + hue_var(rng), 100, 255));
            traits.branchiness = static_cast<uint8_t>(std::clamp(byte_dist(rng) / 2, 0, 255));
            break;
        case PlantArchetype::TropicalTree:
            traits.growth_speed = 0.00015f * speed_var(rng);
            traits.water_need = 0.8f + var(rng) * 0.5f;
            traits.drought_tol = 0.1f + var(rng);
            traits.temp_min_c = 8.0f;
            traits.temp_opt_c = 28.0f;
            traits.temp_max_c = 40.0f;
            traits.fertility_need = 0.6f;
            traits.cluster_affinity = -0.4f;  // territorial — needs canopy space
            traits.base_hue =
                static_cast<uint8_t>(std::clamp(70 + climate_hue_shift + hue_var(rng), 30, 140));
            traits.leafiness =
                static_cast<uint8_t>(std::clamp(180 + leaf_bias + hue_var(rng), 120, 255));
            traits.branchiness = static_cast<uint8_t>(byte_dist(rng));
            break;
    }

    return traits;
}

float SimState::generate_max_age(PlantArchetype arch) {
    // Realistic lifespans (in sim days) with ±30% variation
    std::uniform_real_distribution<float> var(0.7f, 1.3f);
    float base_years;
    switch (arch) {
        case PlantArchetype::Grass:
            base_years = 2.0f;
            break;
        case PlantArchetype::Moss:
            base_years = 5.0f;
            break;
        case PlantArchetype::Shrub:
            base_years = 15.0f;
            break;
        case PlantArchetype::Succulent:
            base_years = 25.0f;
            break;
        case PlantArchetype::BroadleafTree:
            base_years = 80.0f;
            break;
        case PlantArchetype::Conifer:
            base_years = 100.0f;
            break;
        case PlantArchetype::Palm:
            base_years = 60.0f;
            break;
        case PlantArchetype::TropicalTree:
            base_years = 70.0f;
            break;
    }
    return base_years * DAYS_PER_YEAR * var(rng);
}

void SimState::spawn_plants() {
    const auto& preset = BIOME_PRESETS[preset_index];

    // Compute total weight for weighted selection
    float total_weight = 0;
    for (int i = 0; i < NUM_ARCHETYPES; ++i) {
        total_weight += preset.archetype_weights[i];
    }
    if (total_weight <= 0)
        return;

    std::uniform_real_distribution<float> unit(0.0f, 1.0f);

    // Try multiple spawn attempts per tile so populations can fill up
    int attempts_per_tile = (elapsed_days < 1.0f) ? 4 : 2;

    for (int ty = 0; ty < GRID_SIZE; ++ty) {
        for (int tx = 0; tx < GRID_SIZE; ++tx) {
            const auto& tile = tiles[ty][tx];
            if (tile.is_water)
                continue;

            for (int attempt = 0; attempt < attempts_per_tile; ++attempt) {
                int current = count_plants_on_tile(tx, ty);
                // Soft cap: probability of establishment decreases as density approaches max
                // At max, still 10% chance (allows slight overshoot then natural die-back)
                float density_ratio =
                    static_cast<float>(current) /
                    std::max(static_cast<float>(preset.max_plants_per_tile), 1.0f);
                float establish_prob = std::clamp(1.0f - density_ratio * 0.9f, 0.1f, 1.0f);
                if (current >= preset.max_plants_per_tile * 2)
                    break;  // hard cap at 2x to prevent runaway
                if (unit(rng) > establish_prob)
                    continue;

                // Species selection: 60% dominant species, 40% from full weighted pool
                PlantArchetype chosen;
                if (unit(rng) < 0.60f) {
                    // Pick one of the two dominant species (60/40 split)
                    chosen = (unit(rng) < 0.6f) ? dominant_species[0] : dominant_species[1];
                } else {
                    // Normal weighted selection from all archetypes
                    float roll = unit(rng) * total_weight;
                    float accum = 0;
                    chosen = PlantArchetype::Grass;
                    for (int i = 0; i < NUM_ARCHETYPES; ++i) {
                        accum += preset.archetype_weights[i];
                        if (roll <= accum) {
                            chosen = static_cast<PlantArchetype>(i);
                            break;
                        }
                    }
                }

                PlantTraits traits = generate_traits(chosen);
                float suit = compute_suitability(traits, tile);
                if (suit < 0.3f)
                    continue;

                // Neighborhood interaction based on cluster_affinity:
                //   Gregarious species (affinity > 0): same-species neighbors help, others hurt
                //   Territorial species (affinity < 0): ALL neighbors hurt, even same-species
                float neighbor_effect = 0.0f;
                for (const auto& p : plants) {
                    if (p.tile_x == tx && p.tile_y == ty && p.health != PlantHealth::Dead) {
                        if (p.traits.archetype == chosen) {
                            // Same species: gregarious → bonus, territorial → penalty
                            // affinity  0.8 → -0.8 * 0.15 = -0.12 (bonus, lowers competition)
                            // affinity -0.6 → +0.6 * 0.15 = +0.09 (penalty)
                            neighbor_effect -= traits.cluster_affinity * 0.15f;
                        } else {
                            // Different species: always competition, scaled by how territorial
                            // More territorial = stronger inter-species competition
                            float territorial = std::max(0.0f, -traits.cluster_affinity);
                            neighbor_effect += 0.1f + territorial * 0.15f;
                        }
                    }
                }
                // High competition makes it harder to establish
                if (suit - neighbor_effect < 0.3f)
                    continue;

                PlantInstance plant;
                plant.traits = traits;
                plant.tile_x = tx;
                plant.tile_y = ty;
                plant.offset_x = unit(rng) * 0.7f + 0.15f;  // 0.15..0.85
                plant.offset_y = unit(rng) * 0.7f + 0.15f;
                plant.water_reserve = tile.soil_moisture();
                plant.max_age_days = generate_max_age(chosen);

                // Stagger initial biomass so plants don't all grow/die in sync.
                // On first spawn (day 0), give varied starting biomass to simulate
                // an established ecosystem. Later spawns start from scratch.
                if (elapsed_days < 1.0f) {
                    // Slow growers (trees) should start more established:
                    // use max of 2 random rolls so distribution skews toward higher biomass
                    float r1 = unit(rng);
                    float r2 = unit(rng);
                    plant.biomass = std::max(r1, r2) * 0.85f + 0.1f;  // 0.10..0.95, skewed high
                    // Set age proportional to biomass / growth_speed
                    plant.age_days =
                        plant.biomass / std::max(plant.traits.growth_speed, 0.0001f) * unit(rng);
                }
                // Update phase from initial biomass
                if (plant.biomass >= 0.66f)
                    plant.phase = GrowthPhase::Large;
                else if (plant.biomass >= 0.33f)
                    plant.phase = GrowthPhase::Medium;

                plants.push_back(plant);
                if (metrics)
                    metrics->on_birth(chosen);

            }  // end attempt loop
        }
    }
}

void SimState::reproduce_plants() {
    const auto& preset = BIOME_PRESETS[preset_index];
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);

    // Only mature (Large phase) healthy plants can reproduce
    // Collect parent candidates first to avoid modifying the vector while iterating
    struct Candidate {
        PlantArchetype arch;
        PlantTraits traits;
        int tile_x, tile_y;
    };
    std::vector<Candidate> parents;
    for (const auto& p : plants) {
        if (p.phase != GrowthPhase::Large || p.health != PlantHealth::Healthy)
            continue;
        // Reproduction probability per spawn cycle: higher for fast-growers
        float repro_prob = std::clamp(p.traits.growth_speed * 200.0f, 0.02f, 0.3f);
        if (unit(rng) < repro_prob) {
            parents.push_back({p.traits.archetype, p.traits, p.tile_x, p.tile_y});
        }
    }

    for (const auto& parent : parents) {
        // Offspring can land on same tile or adjacent (3x3 neighborhood)
        int dx = static_cast<int>(unit(rng) * 3.0f) - 1;
        int dy = static_cast<int>(unit(rng) * 3.0f) - 1;
        int tx = std::clamp(parent.tile_x + dx, 0, GRID_SIZE - 1);
        int ty = std::clamp(parent.tile_y + dy, 0, GRID_SIZE - 1);

        int current = count_plants_on_tile(tx, ty);
        float density_ratio = static_cast<float>(current) /
                              std::max(static_cast<float>(preset.max_plants_per_tile), 1.0f);
        float establish_prob = std::clamp(1.0f - density_ratio * 0.9f, 0.1f, 1.0f);
        if (current >= preset.max_plants_per_tile * 2)
            continue;

        std::uniform_real_distribution<float> unit01(0.0f, 1.0f);
        if (unit01(rng) > establish_prob)
            continue;

        const auto& tile = tiles[ty][tx];

        // Generate offspring with slight trait variation from parent
        PlantTraits traits = generate_traits(parent.arch);
        // Inherit sprite seed variation from parent
        traits.sprite_seed = static_cast<uint32_t>(rng());

        float suit = compute_suitability(traits, tile);
        if (suit < 0.3f)
            continue;

        PlantInstance offspring;
        offspring.traits = traits;
        offspring.tile_x = tx;
        offspring.tile_y = ty;
        offspring.offset_x = unit(rng) * 0.7f + 0.15f;
        offspring.offset_y = unit(rng) * 0.7f + 0.15f;
        offspring.water_reserve = tile.soil_moisture();
        offspring.max_age_days = generate_max_age(parent.arch);

        plants.push_back(offspring);
        if (metrics)
            metrics->on_birth(parent.arch);
    }
}

void SimState::update_plants(float dt_days) {
    for (auto& plant : plants) {
        if (plant.health == PlantHealth::Dead) {
            plant.dead_timer += dt_days;
            // Biomass decays proportional to growth_speed (slow growers decay slowly)
            // Minimum decay rate ensures even trees eventually decompose visually
            float decay_rate = std::max(plant.traits.growth_speed * 5.0f, 0.001f);
            plant.biomass -= decay_rate * dt_days;
            plant.biomass = std::max(plant.biomass, 0.0f);
            if (plant.biomass < 0.33f)
                plant.phase = GrowthPhase::Small;
            else if (plant.biomass < 0.66f)
                plant.phase = GrowthPhase::Medium;
            continue;
        }

        plant.age_days += dt_days;
        const auto& tile = tiles[plant.tile_y][plant.tile_x];

        // Growth — growth_speed is biomass/day at ideal conditions
        float suit = compute_suitability(plant.traits, tile);
        float water_factor =
            std::clamp(plant.water_reserve / std::max(plant.traits.water_need, 0.1f), 0.0f, 1.0f);

        // Density-dependent growth: more neighbors = less light/nutrients = slower growth
        int neighbors = 0;
        for (const auto& other : plants) {
            if (&other == &plant || other.health == PlantHealth::Dead)
                continue;
            if (other.tile_x == plant.tile_x && other.tile_y == plant.tile_y)
                ++neighbors;
        }
        // Growth suppression: each neighbor reduces growth by ~15%
        float density_factor = 1.0f / (1.0f + static_cast<float>(neighbors) * 0.15f);

        plant.biomass += plant.traits.growth_speed * suit * water_factor * density_factor *
                         dt_days * (1.0f - plant.stress);
        plant.biomass = std::clamp(plant.biomass, 0.0f, 1.0f);

        // Update phase
        if (plant.biomass < 0.33f)
            plant.phase = GrowthPhase::Small;
        else if (plant.biomass < 0.66f)
            plant.phase = GrowthPhase::Medium;
        else
            plant.phase = GrowthPhase::Large;

        // Water reserve
        float size_factor = 0.3f + plant.biomass * 0.7f;
        plant.water_reserve +=
            (tile.soil_moisture() * tile.soil_hold * 0.5f - tile.evap_demand * size_factor) *
            dt_days * 0.05f;
        plant.water_reserve = std::clamp(plant.water_reserve, 0.0f, 1.0f);

        // Stress — mature plants resist stress better (up to 4x for fully grown)
        // Also factor in drought tolerance for water stress
        float maturity_resistance = 1.0f / (1.0f + plant.biomass * 3.0f);
        float env_stress = 0.0f;  // environmental stress (temp + drought)
        if (plant.water_reserve < 0.2f) {
            float drought_factor = 1.0f - std::clamp(plant.traits.drought_tol, 0.0f, 0.9f);
            env_stress += (0.2f - plant.water_reserve) * 2.0f * dt_days * 0.04f *
                          maturity_resistance * drought_factor;
        }
        if (tile.temperature < plant.traits.temp_min_c ||
            tile.temperature > plant.traits.temp_max_c) {
            float temp_excess = 0.0f;
            if (tile.temperature < plant.traits.temp_min_c)
                temp_excess = plant.traits.temp_min_c - tile.temperature;
            else
                temp_excess = tile.temperature - plant.traits.temp_max_c;
            float severity = std::clamp(temp_excess / 15.0f, 0.1f, 1.0f);
            env_stress += severity * dt_days * 0.06f * maturity_resistance;
        }

        // Crowding stress (separate — doesn't block recovery)
        float crowd_stress = 0.0f;
        {
            int same_count = 0;
            int diff_count = 0;
            for (const auto& other : plants) {
                if (&other == &plant || other.health == PlantHealth::Dead)
                    continue;
                if (other.tile_x == plant.tile_x && other.tile_y == plant.tile_y) {
                    if (other.traits.archetype == plant.traits.archetype)
                        ++same_count;
                    else
                        ++diff_count;
                }
            }
            float same_effect =
                static_cast<float>(same_count) * -plant.traits.cluster_affinity * 0.02f;
            float diff_effect = static_cast<float>(diff_count) * 0.03f;
            crowd_stress = (same_effect + diff_effect) * dt_days * 0.02f * maturity_resistance;
        }

        // Recovery always runs (even during stress) — net stress = accumulation minus recovery
        // This prevents seasonal stress from ratcheting up irreversibly over multiple cycles
        float recovery = dt_days * 0.03f * std::max(suit, 0.1f) *
                         (1.0f + plant.biomass * 2.0f);  // mature plants recover faster
        plant.stress += env_stress + crowd_stress - recovery;
        plant.stress = std::clamp(plant.stress, 0.0f, 1.0f);

        // Senescence: old plants accumulate stress faster, eventually die of old age
        if (plant.max_age_days > 0 && plant.age_days > plant.max_age_days * 0.75f) {
            float age_frac =
                (plant.age_days - plant.max_age_days * 0.75f) / (plant.max_age_days * 0.25f);
            age_frac = std::clamp(age_frac, 0.0f, 1.0f);
            plant.stress += age_frac * dt_days * 0.05f;
        }

        // Health from stress — track death cause
        PlantHealth prev_health = plant.health;
        if (plant.stress < 0.3f)
            plant.health = PlantHealth::Healthy;
        else if (plant.stress < 0.6f)
            plant.health = PlantHealth::Stressed;
        else if (plant.stress < 0.9f)
            plant.health = PlantHealth::Brown;
        else
            plant.health = PlantHealth::Dead;

        // If just died, determine cause and report to metrics
        if (plant.health == PlantHealth::Dead && prev_health != PlantHealth::Dead) {
            // Determine primary death cause
            DeathCause cause = DeathCause::Competition;  // default
            if (plant.max_age_days > 0 && plant.age_days > plant.max_age_days * 0.9f)
                cause = DeathCause::Senescence;
            else if (plant.water_reserve < 0.1f)
                cause = DeathCause::Drought;
            else if (tile.temperature < plant.traits.temp_min_c)
                cause = DeathCause::Cold;
            else if (tile.temperature > plant.traits.temp_max_c)
                cause = DeathCause::Heat;
            plant.death_cause = static_cast<uint8_t>(cause);
            if (metrics)
                metrics->on_death(plant.traits.archetype, cause);
        }

        // Biomass decays when health is poor (plant shrinks visually)
        // Scaled by growth_speed so slow-growing trees don't shrink unrealistically fast
        if (plant.health == PlantHealth::Brown) {
            float brown_decay = std::max(plant.traits.growth_speed * 3.0f, 0.0005f);
            plant.biomass -= brown_decay * dt_days;
            plant.biomass = std::max(plant.biomass, 0.0f);
        }

        // Re-evaluate phase after potential decay
        if (plant.biomass < 0.33f)
            plant.phase = GrowthPhase::Small;
        else if (plant.biomass < 0.66f)
            plant.phase = GrowthPhase::Medium;
        else
            plant.phase = GrowthPhase::Large;
    }

    // Remove dead plants past their timer
    auto new_end = std::remove_if(plants.begin(), plants.end(), [](const PlantInstance& p) {
        return p.health == PlantHealth::Dead && p.dead_timer >= DEAD_REMOVAL_DAYS;
    });
    int removed = static_cast<int>(plants.end() - new_end);
    plants.erase(new_end, plants.end());
    if (metrics) {
        for (int i = 0; i < removed; ++i)
            metrics->on_removal();
    }
}

}  // namespace veggen
