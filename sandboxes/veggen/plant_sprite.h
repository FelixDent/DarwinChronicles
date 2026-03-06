#pragma once

#include <cstdint>
#include <vector>

#include "vegetation.h"

namespace veggen {

struct Sprite {
    int w = 0;
    int h = 0;
    std::vector<uint32_t> pixels;  // RGBA, row-major
};

// Generate a procedural pixel-art sprite for a given plant configuration.
// Sprite is deterministic based on traits.sprite_seed + phase.
Sprite generate_plant_sprite(const PlantTraits& traits, GrowthPhase phase, PlantHealth health);

}  // namespace veggen
