#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "terrain_gen.h"  // EnvParams lives here (shared from weather sandbox)

namespace sandbox {

// ── Sandbox configuration ───────────────────────────────────────────────────

struct SandboxConfig {
    uint32_t seed = 42;
    uint32_t world_width = 256;
    uint32_t world_height = 256;
    std::string preset = "earth";
    bool headless = false;    // --headless: generate + export BMP + stats, no window
    bool tile_test = false;   // --tile-test: render representative tiles at high zoom
    int tile_test_ppt = 128;  // pixels per tile for tile test

    EnvParams env;

    // CLI overrides (negative = not overridden)
    float override_water_level = -1.0f;
    float override_continent_threshold = -1.0f;
    float override_ridge_strength = -1.0f;
};

// Preset: curated env params
struct Preset {
    EnvParams env;
};

Preset get_preset(const std::string& name);

// CLI parsing
SandboxConfig parse_args(int argc, char* argv[]);

}  // namespace sandbox
