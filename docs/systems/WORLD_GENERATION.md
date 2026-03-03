# Procedural World Generation

**Part of:** [Darwin Chronicles](../../README.md)
**Implementation:** [`src/world/`](../../src/world/)

## Overview

The Procedural World Generation system creates diverse, climate-driven terrain using noise-based macro fields. Environmental parameters from the planetary model cascade through multi-octave noise functions to produce realistic biome distributions, nutrient densities, and toxicity patterns. This creates scientifically grounded variety: a hot, humid planet produces dense rainforests, while a cold, dry world generates tundra and deserts.

## Three-Layer Architecture

```
Layer 1: Environmental Parameters
    ↓ (noise field generation)
Layer 2: Macro Fields
    - Height field (multi-octave Perlin noise)
    - Temperature field (latitude gradient + noise)
    - Moisture field (BFS from water sources + noise)
    - Toxicity field (localized hotspots)
    ↓ (biome classification rules)
Layer 3: Tile Properties
    - Biome type (15 categories)
    - Base nutrients
    - Regeneration rate
    - Toxin concentration
```

---

## Layer 1: Environmental Parameters

### Input from Planetary Model

```cpp
struct EnvParams {
    float nutrient_richness;  // Global fertility [0.0, 2.0]
    float humidity;           // Moisture availability [0.0, 1.0]
    float toxicity_level;     // Planetary toxicity [0.0, 1.0]

    static EnvParams from_climate(const Climate& climate, const BioConstraints& bio) {
        return EnvParams{
            .nutrient_richness = bio.base_nutrient_regen,
            .humidity = climate.habitability_factor * 0.7f,
            .toxicity_level = std::min(climate.uv_intensity, 1.0f),
        };
    }
};
```

### Example Configurations

| Planet Type | Nutrient Richness | Humidity | Toxicity |
|-------------|-------------------|----------|----------|
| **Earth-like** | 1.0 | 0.7 | 0.1 |
| **Tropical Paradise** | 1.5 | 0.9 | 0.05 |
| **Arid Desert World** | 0.4 | 0.2 | 0.3 |
| **Toxic Swamp** | 0.8 | 0.85 | 0.7 |

---

## Layer 2: Macro Field Generation

### Height Field (Multi-Octave Perlin Noise)

```cpp
#include <FastNoiseLite.h>

class HeightField {
    FastNoiseLite noise;
    uint32_t octaves = 4;
    float persistence = 0.5f;
    float scale = 0.02f;  // Controls feature size

public:
    HeightField(uint32_t seed) {
        noise.SetSeed(static_cast<int>(seed));
        noise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    }

    float sample(uint32_t x, uint32_t y) const {
        float total = 0.0f;
        float amplitude = 1.0f;
        float frequency = 1.0f;
        float max_value = 0.0f;

        for (uint32_t i = 0; i < octaves; ++i) {
            float nx = static_cast<float>(x) * scale * frequency;
            float ny = static_cast<float>(y) * scale * frequency;

            float value = noise.GetNoise(nx, ny);
            total += value * amplitude;

            max_value += amplitude;
            amplitude *= persistence;
            frequency *= 2.0f;
        }

        // Normalize to [0.0, 1.0]
        return (total / max_value + 1.0f) * 0.5f;
    }
};
```

**Height Categories:**
- `h < 0.3` → Water (Ocean/Lake)
- `0.3 ≤ h < 0.45` → Coastal/Lowlands
- `0.45 ≤ h < 0.7` → Plains/Hills
- `h ≥ 0.7` → Mountains

### Temperature Field (Latitude Gradient + Noise)

```cpp
class TemperatureField {
    FastNoiseLite noise;
    uint32_t world_height;
    float climate_temp_K;  // From planetary climate model

public:
    TemperatureField(uint32_t seed, uint32_t world_height, float climate_temp_K)
        : world_height(world_height), climate_temp_K(climate_temp_K) {
        noise.SetSeed(static_cast<int>(seed + 1000));
        noise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    }

    float sample(uint32_t x, uint32_t y, float height) const {
        // Latitude factor: colder at poles, hotter at equator
        float lat_normalized = (static_cast<float>(y) / world_height) - 0.5f;
        float lat_factor = 1.0f - std::abs(lat_normalized * 2.0f);  // 0 at poles, 1 at equator

        // Elevation cooling: -6.5K per km (simplified to per 0.1 height)
        float elevation_offset = -std::max(height - 0.3f, 0.0f) * 65.0f;

        // Noise variation (±10K)
        float nx = static_cast<float>(x) * 0.03f;
        float ny = static_cast<float>(y) * 0.03f;
        float noise_offset = noise.GetNoise(nx, ny) * 10.0f;

        // Base temp from climate model, modulated by latitude
        float base_temp = climate_temp_K * (0.7f + 0.3f * lat_factor);

        return base_temp + elevation_offset + noise_offset;
    }
};
```

**Temperature Categories:**
- `T < 273K` → Frozen
- `273K ≤ T < 283K` → Cold
- `283K ≤ T < 298K` → Temperate
- `T ≥ 298K` → Hot

### Moisture Field (BFS from Water + Noise)

```cpp
#include <deque>

class MoistureField {
    std::vector<float> values;
    uint32_t width, height;
    FastNoiseLite noise;
    float global_humidity;

public:
    static MoistureField generate(
        uint32_t width,
        uint32_t height,
        const std::vector<float>& height_field,
        float global_humidity,
        uint32_t seed
    ) {
        size_t size = static_cast<size_t>(width) * height;
        std::vector<float> values(size, 0.0f);
        FastNoiseLite noise;
        noise.SetSeed(static_cast<int>(seed + 2000));
        noise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);

        // BFS from water sources
        std::deque<std::tuple<uint32_t, uint32_t, float>> queue;
        std::vector<bool> visited(size, false);

        // Seed queue with water tiles
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                size_t idx = y * width + x;
                if (height_field[idx] < 0.3f) {
                    queue.push_back({x, y, 1.0f});
                    visited[idx] = true;
                    values[idx] = 1.0f;
                }
            }
        }

        // Propagate moisture with exponential decay
        while (!queue.empty()) {
            auto [x, y, moisture] = queue.front();
            queue.pop_front();

            float decay_rate = 0.95f;  // Moisture drops 5% per tile
            float new_moisture = moisture * decay_rate;

            if (new_moisture < 0.1f) continue;  // Stop propagation

            // Check 4-connected neighbors
            int dx_arr[] = {-1, 1, 0, 0};
            int dy_arr[] = {0, 0, -1, 1};
            for (int d = 0; d < 4; ++d) {
                int nx = static_cast<int>(x) + dx_arr[d];
                int ny = static_cast<int>(y) + dy_arr[d];

                if (nx < 0 || ny < 0 || nx >= static_cast<int>(width) || ny >= static_cast<int>(height))
                    continue;

                size_t nidx = static_cast<size_t>(ny) * width + nx;
                if (!visited[nidx]) {
                    visited[nidx] = true;
                    values[nidx] = new_moisture;
                    queue.push_back({static_cast<uint32_t>(nx), static_cast<uint32_t>(ny), new_moisture});
                }
            }
        }

        // Add noise variation and scale by global humidity
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                size_t idx = y * width + x;
                float nx = static_cast<float>(x) * 0.04f;
                float ny = static_cast<float>(y) * 0.04f;
                float noise_offset = noise.GetNoise(nx, ny) * 0.2f;

                values[idx] = std::min(
                    std::max(values[idx] + noise_offset, 0.0f) * global_humidity,
                    1.0f
                );
            }
        }

        MoistureField field;
        field.values = std::move(values);
        field.width = width;
        field.height = height;
        field.noise = noise;
        field.global_humidity = global_humidity;
        return field;
    }

    float get(uint32_t x, uint32_t y) const {
        return values[y * width + x];
    }
};
```

**Moisture Categories:**
- `m < 0.2` → Arid
- `0.2 ≤ m < 0.5` → Dry
- `0.5 ≤ m < 0.7` → Moderate
- `m ≥ 0.7` → Humid

### Toxicity Field (Localized Hotspots)

```cpp
class ToxicityField {
    FastNoiseLite noise;
    float global_toxicity;

public:
    ToxicityField(uint32_t seed, float global_toxicity)
        : global_toxicity(global_toxicity) {
        noise.SetSeed(static_cast<int>(seed + 3000));
        noise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    }

    float sample(uint32_t x, uint32_t y) const {
        // Low-frequency noise for large toxic regions
        float nx = static_cast<float>(x) * 0.01f;
        float ny = static_cast<float>(y) * 0.01f;
        float base = noise.GetNoise(nx, ny);

        // Only positive values create toxicity
        float toxicity = std::pow(base * 0.5f + 0.5f, 2.0f);  // Square to create hotspots

        return std::min(toxicity * global_toxicity, 1.0f);
    }
};
```

---

## Layer 3: Biome Classification

### Biome Types

```cpp
enum class Biome {
    // Water
    Ocean,
    Lake,
    ToxicOcean,

    // Cold (T < 283K)
    Tundra,
    BorealForest,
    ToxicTundra,

    // Temperate (283K ≤ T < 298K)
    ColdDesert,
    Grassland,
    TemperateForest,
    ToxicGrassland,

    // Hot (T ≥ 298K)
    HotDesert,
    Savanna,
    Rainforest,
    ToxicSavanna,
    ToxicRainforest,
};
```

### Classification Rules

```cpp
Biome classify_biome(
    float height,
    float temperature_K,
    float moisture,
    float toxicity
) {
    constexpr float TOXIC_THRESHOLD = 0.5f;

    // Water biomes
    if (height < 0.3f) {
        if (toxicity > TOXIC_THRESHOLD) return Biome::ToxicOcean;
        if (height < 0.25f) return Biome::Ocean;
        return Biome::Lake;
    }

    // Land biomes: temperature zones
    if (temperature_K < 273.0f) {
        // Frozen
        return Biome::Tundra;
    } else if (temperature_K < 283.0f) {
        // Cold
        if (toxicity > TOXIC_THRESHOLD) return Biome::ToxicTundra;
        if (moisture > 0.4f) return Biome::BorealForest;
        return Biome::Tundra;
    } else if (temperature_K < 298.0f) {
        // Temperate
        if (toxicity > TOXIC_THRESHOLD) return Biome::ToxicGrassland;
        if (moisture < 0.3f) return Biome::ColdDesert;
        if (moisture < 0.6f) return Biome::Grassland;
        return Biome::TemperateForest;
    } else {
        // Hot
        if (moisture < 0.3f) return Biome::HotDesert;
        if (moisture < 0.6f) {
            if (toxicity > TOXIC_THRESHOLD) return Biome::ToxicSavanna;
            return Biome::Savanna;
        }
        if (toxicity > TOXIC_THRESHOLD) return Biome::ToxicRainforest;
        return Biome::Rainforest;
    }
}
```

### Biome Properties

```cpp
struct BiomeProperties {
    float base_nutrients;      // Initial nutrient density
    float regen_rate_per_sec;  // Nutrient regeneration
    float movement_cost_mult;  // Terrain difficulty
};

BiomeProperties biome_properties(Biome biome) {
    switch (biome) {
        case Biome::Ocean:
        case Biome::Lake:
            return {0.3f, 0.02f, 2.0f};   // Swimming is hard
        case Biome::ToxicOcean:
            return {0.5f, 0.03f, 2.5f};

        case Biome::Tundra:
            return {0.2f, 0.008f, 1.3f};
        case Biome::BorealForest:
            return {0.5f, 0.025f, 1.2f};
        case Biome::ToxicTundra:
            return {0.4f, 0.015f, 1.5f};

        case Biome::ColdDesert:
            return {0.1f, 0.005f, 1.1f};
        case Biome::Grassland:
            return {0.6f, 0.04f, 1.0f};
        case Biome::TemperateForest:
            return {0.8f, 0.05f, 1.15f};
        case Biome::ToxicGrassland:
            return {0.7f, 0.045f, 1.2f};

        case Biome::HotDesert:
            return {0.05f, 0.005f, 1.4f};
        case Biome::Savanna:
            return {0.5f, 0.035f, 1.0f};
        case Biome::Rainforest:
            return {1.0f, 0.06f, 1.3f};
        case Biome::ToxicSavanna:
            return {0.65f, 0.04f, 1.3f};
        case Biome::ToxicRainforest:
            return {1.2f, 0.065f, 1.5f};
    }
    return {0.0f, 0.0f, 1.0f};  // Unreachable
}
```

---

## Complete Implementation

### Tile Structure

```cpp
struct Tile {
    Biome biome;
    float nutrients;       // Current nutrient level [0.0, 2.0]
    float regen_rate;      // Per-second regeneration
    float toxins;          // Toxicity [0.0, 1.0]
    float height;          // Elevation [0.0, 1.0]
    float temperature_K;   // Temperature in Kelvin
};
```

### ProceduralWorld Generator

```cpp
class ProceduralWorld {
public:
    uint32_t width;
    uint32_t height;
    std::vector<Tile> tiles;

    static ProceduralWorld generate(
        uint32_t width,
        uint32_t height,
        const EnvParams& env_params,
        const Climate& climate,
        uint32_t seed
    ) {
        size_t size = static_cast<size_t>(width) * height;

        // Generate macro fields
        HeightField height_field(seed);
        TemperatureField temp_field(seed, height, climate.mean_temp_K);
        ToxicityField toxicity_field(seed, env_params.toxicity_level);

        // Generate height values first (needed for moisture BFS)
        std::vector<float> height_values(size);
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                size_t idx = y * width + x;
                height_values[idx] = height_field.sample(x, y);
            }
        }

        // Generate moisture field (depends on water locations)
        auto moisture_field = MoistureField::generate(
            width, height, height_values, env_params.humidity, seed
        );

        // Generate tiles
        std::vector<Tile> tiles;
        tiles.reserve(size);
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                size_t idx = y * width + x;
                float h = height_values[idx];
                float t = temp_field.sample(x, y, h);
                float m = moisture_field.get(x, y);
                float tox = toxicity_field.sample(x, y);

                Biome biome = classify_biome(h, t, m, tox);
                auto props = biome_properties(biome);

                tiles.push_back(Tile{
                    .biome = biome,
                    .nutrients = props.base_nutrients * env_params.nutrient_richness,
                    .regen_rate = props.regen_rate_per_sec * env_params.nutrient_richness,
                    .toxins = tox,
                    .height = h,
                    .temperature_K = t,
                });
            }
        }

        return ProceduralWorld{width, height, std::move(tiles)};
    }

    const Tile& tile(uint32_t x, uint32_t y) const {
        return tiles[y * width + x];
    }
};
```

### WorldGrid Conversion

```cpp
class WorldGrid {
public:
    uint32_t width;
    uint32_t height;

    static WorldGrid from_procedural_world(ProceduralWorld world) {
        WorldGrid grid;
        grid.width = world.width;
        grid.height = world.height;
        grid.tiles = std::move(world.tiles);
        return grid;
    }

    const Tile& tile(uint32_t x, uint32_t y) const {
        return tiles[y * width + x];
    }

    Tile& tile_mut(uint32_t x, uint32_t y) {
        return tiles[y * width + x];
    }

private:
    std::vector<Tile> tiles;
};
```

---

## Planetary Scenarios

### Scenario 1: Earth-like World

```cpp
Climate climate{
    .mean_temp_K = 288.0f,
    .temp_range_K = 57.6f,
    .uv_intensity = 0.1f,
    .habitability_factor = 0.87f,
};

EnvParams env_params{
    .nutrient_richness = 1.0f,
    .humidity = 0.7f,
    .toxicity_level = 0.1f,
};

auto world = ProceduralWorld::generate(256, 256, env_params, climate, 42);

// Expected distribution:
// - 30% Ocean
// - 15% Temperate Forest
// - 20% Grassland
// - 10% Savanna
// - 8% Rainforest
// - 12% Tundra/Boreal
// - 5% Desert
```

### Scenario 2: Hot Venus-like World

```cpp
Climate climate{
    .mean_temp_K = 735.0f,
    .temp_range_K = 147.0f,
    .uv_intensity = 1.4f,
    .habitability_factor = 0.0f,
};

EnvParams env_params{
    .nutrient_richness = 0.2f,
    .humidity = 0.05f,
    .toxicity_level = 0.9f,
};

auto world = ProceduralWorld::generate(256, 256, env_params, climate, 43);

// Expected distribution:
// - 85% Hot Desert
// - 10% Toxic Savanna
// - 5% ToxicOcean (lava seas)
// - Near-zero nutrients everywhere
```

### Scenario 3: Frozen Snowball World

```cpp
Climate climate{
    .mean_temp_K = 220.0f,
    .temp_range_K = 44.0f,
    .uv_intensity = 0.3f,
    .habitability_factor = 0.05f,
};

EnvParams env_params{
    .nutrient_richness = 0.3f,
    .humidity = 0.4f,
    .toxicity_level = 0.15f,
};

auto world = ProceduralWorld::generate(256, 256, env_params, climate, 44);

// Expected distribution:
// - 90% Tundra
// - 8% Ocean (ice-covered)
// - 2% Toxic Tundra
// - Very slow nutrient regen
```

### Scenario 4: Tropical Paradise

```cpp
Climate climate{
    .mean_temp_K = 305.0f,
    .temp_range_K = 61.0f,
    .uv_intensity = 0.05f,
    .habitability_factor = 0.92f,
};

EnvParams env_params{
    .nutrient_richness = 1.5f,
    .humidity = 0.9f,
    .toxicity_level = 0.05f,
};

auto world = ProceduralWorld::generate(256, 256, env_params, climate, 45);

// Expected distribution:
// - 40% Rainforest
// - 25% Savanna
// - 20% Ocean/Lake
// - 10% Grassland
// - 5% Temperate Forest
// - High nutrient density everywhere
```

---

## Visualization

### Biome Color Mapping

```cpp
std::array<uint8_t, 3> biome_color(Biome biome) {
    switch (biome) {
        case Biome::Ocean:           return {20, 60, 120};
        case Biome::Lake:            return {40, 100, 180};
        case Biome::ToxicOcean:      return {80, 40, 100};

        case Biome::Tundra:          return {200, 210, 220};
        case Biome::BorealForest:    return {60, 100, 80};
        case Biome::ToxicTundra:     return {150, 120, 160};

        case Biome::ColdDesert:      return {180, 170, 140};
        case Biome::Grassland:       return {100, 160, 80};
        case Biome::TemperateForest: return {50, 120, 60};
        case Biome::ToxicGrassland:  return {120, 140, 60};

        case Biome::HotDesert:       return {220, 200, 150};
        case Biome::Savanna:         return {180, 160, 90};
        case Biome::Rainforest:      return {30, 100, 40};
        case Biome::ToxicSavanna:    return {160, 140, 70};
        case Biome::ToxicRainforest: return {60, 100, 60};
    }
    return {0, 0, 0};
}
```

### Debug Visualization

```cpp
void visualize_fields(const ProceduralWorld& world, const std::string& output_dir) {
    // Height map
    std::vector<float> heights;
    heights.reserve(world.tiles.size());
    for (const auto& t : world.tiles) heights.push_back(t.height);
    save_grayscale_image(heights, world.width, world.height, output_dir + "/height.png");

    // Temperature map (blue = cold, red = hot)
    std::vector<float> temps;
    temps.reserve(world.tiles.size());
    for (const auto& t : world.tiles) temps.push_back(t.temperature_K);
    save_heatmap_image(temps, world.width, world.height, 220.0f, 320.0f, output_dir + "/temperature.png");

    // Biome map
    std::vector<Biome> biomes;
    biomes.reserve(world.tiles.size());
    for (const auto& t : world.tiles) biomes.push_back(t.biome);
    save_biome_image(biomes, world.width, world.height, output_dir + "/biomes.png");
}
```

---

## Integration Points

### With Planetary Model

Environmental parameters flow from climate computation:

```cpp
Planet planet{/* ... */};
Climate climate = planet.compute_climate();
auto bio_constraints = BioConstraints::from_climate(climate, planet);

EnvParams env_params{
    .nutrient_richness = bio_constraints.base_nutrient_regen,
    .humidity = climate.habitability_factor * 0.7f,
    .toxicity_level = std::min(climate.uv_intensity, 1.0f),
};

auto world = ProceduralWorld::generate(512, 512, env_params, climate, seed);
```

See: [Planetary Model](PLANETARY_MODEL.md)

### With Nutrient Regeneration

Biome properties drive dynamic nutrient systems:

```cpp
void regenerate_nutrients(WorldGrid& grid, float dt) {
    for (auto& tile : grid.tiles) {
        float max_nutrients = biome_properties(tile.biome).base_nutrients * 2.0f;
        tile.nutrients = std::min(tile.nutrients + tile.regen_rate * dt, max_nutrients);
    }
}
```

See: [Nutrient Regeneration](NUTRIENT_REGENERATION.md)

### With Organism Simulation

Terrain affects organism behavior:

```cpp
float movement_energy_cost(
    const Organism& organism,
    const WorldGrid& world,
    std::pair<uint32_t, uint32_t> position,
    std::pair<float, float> velocity
) {
    const auto& tile = world.tile(position.first, position.second);
    float terrain_mult = biome_properties(tile.biome).movement_cost_mult;

    float speed = std::hypot(velocity.first, velocity.second);
    float base_cost = 0.002f * organism.body_size * speed * speed;
    return base_cost * terrain_mult;
}
```

See: [Neural Evolution](NEURAL_EVOLUTION.md)

---

## Performance Considerations

### Memory Layout

```cpp
// Flat array: cache-friendly iteration
class WorldGrid {
    std::vector<Tile> tiles;  // 256x256 = 65536 tiles × 24 bytes = 1.5 MB
};

// Access pattern: row-major order
size_t idx = y * width + x;
```

### Generation Benchmarks

| World Size | Generation Time | Memory |
|------------|-----------------|--------|
| 128×128 | ~5 ms | 0.4 MB |
| 256×256 | ~18 ms | 1.5 MB |
| 512×512 | ~75 ms | 6 MB |
| 1024×1024 | ~320 ms | 24 MB |

### Optimization: Parallel Field Generation

```cpp
#include <execution>
#include <algorithm>
#include <numeric>

ProceduralWorld generate_parallel(uint32_t width, uint32_t height, const EnvParams& env_params) {
    size_t size = static_cast<size_t>(width) * height;
    std::vector<Tile> tiles(size);
    std::vector<size_t> indices(size);
    std::iota(indices.begin(), indices.end(), 0);

    std::for_each(std::execution::par, indices.begin(), indices.end(),
        [&](size_t idx) {
            uint32_t x = static_cast<uint32_t>(idx % width);
            uint32_t y = static_cast<uint32_t>(idx / width);
            tiles[idx] = generate_tile(x, y, env_params);
        }
    );

    return ProceduralWorld{width, height, std::move(tiles)};
}

// Speedup: ~3.2× on 8-core CPU for 512×512 world
```

---

## Implementation Checklist

- [ ] Multi-octave Perlin noise implementation
- [ ] Height field generation
- [ ] Temperature field with latitude gradient
- [ ] Moisture field BFS propagation
- [ ] Toxicity field generation
- [ ] Biome classification rules (15 types)
- [ ] Biome property lookup tables
- [ ] ProceduralWorld generator
- [ ] WorldGrid conversion
- [ ] Tile access methods
- [ ] Visualization functions
- [ ] Unit tests for known scenarios
- [ ] Performance benchmarks

---

## References

- Perlin noise: Classic gradient noise algorithm
- BFS moisture propagation: Realistic river/lake influence
- Latitude-based temperature: Earth-like climate zones
- Whittaker biome diagram: Temperature × precipitation classification

**Related Documentation:**
- [Planetary Model](PLANETARY_MODEL.md)
- [Nutrient Regeneration](NUTRIENT_REGENERATION.md)
- [Neural Evolution](NEURAL_EVOLUTION.md)
