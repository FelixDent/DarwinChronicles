# World Module

**Documentation:**
- [Procedural World Generation](../../docs/systems/WORLD_GENERATION.md)
- [Nutrient Regeneration](../../docs/systems/NUTRIENT_REGENERATION.md)

## Overview

The world module handles procedural terrain generation, biome classification, and dynamic nutrient regeneration. It uses noise-based macro fields to create realistic, climate-driven worlds stored in an efficient flat-array WorldGrid.

## Key Components

- **`grid.h/.cpp`** — WorldGrid resource, tile access, gradient calculations
- **`generation.h/.cpp`** — Procedural terrain generation from environmental parameters
- **`biomes.h/.cpp`** — Biome classification rules and properties
- **`fields.h/.cpp`** — Macro field generation (height, temperature, moisture, toxicity)
- **`regeneration.h/.cpp`** — Nutrient regeneration systems (frame-based, tick-based, parallel)

## Quick Example

```cpp
#include "world/grid.h"
#include "world/generation.h"
#include "environment/params.h"

// Create environmental parameters
EnvParams env_params{
    .nutrient_richness = 1.0f,
    .humidity = 0.7f,
    .toxicity_level = 0.2f,
};

// Generate procedural world
ProceduralWorld world = ProceduralWorld::generate(
    128,  // width
    128,  // height
    env_params,
    42    // seed
);

// Convert to WorldGrid for simulation
WorldGrid grid = WorldGrid::from_procedural_world(std::move(world));

// Access tiles
const Tile& tile = grid.tile(64, 64);
std::cout << "Biome: " << biome_name(tile.biome) << "\n";
std::cout << "Nutrients: " << tile.nutrients << "\n";
std::cout << "Regen rate: " << tile.regen_rate << "/s\n";
```

## Core Concepts

**Three-Layer Generation:**
1. Global Params → Noise Fields (height, temp, moisture, toxicity)
2. Field Values → Biome Classification (15 types)
3. Biome + Global Params → Tile Properties (nutrients, regen, toxins)

**15 Biome Types:**
- Water: Ocean, Lake, ToxicOcean
- Cold: Tundra, BorealForest, ToxicTundra
- Temperate: ColdDesert, Grassland, TemperateForest, ToxicGrassland
- Hot: HotDesert, Savanna, Rainforest, ToxicSavanna, ToxicRainforest

**Nutrient Regeneration:**
- Biome-specific rates: Rainforest (0.06/s) → Desert (0.005/s)
- Environmental scaling by humidity, richness, toxicity
- Efficient flat-array storage for performance

For complete algorithms and code examples, see the detailed documentation above.
