#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sandbox {

// ── Environment parameters (fed to terrain generation) ──────────────────────

struct EnvParams {
    float water_level = 0.45f;
    float noise_scale = 0.02f;
    float continent_threshold = 0.55f;
    float ridge_strength = 1.0f;
};

// ── Sandbox configuration ───────────────────────────────────────────────────

struct SandboxConfig {
    uint32_t seed = 42;
    uint32_t world_width = 256;
    uint32_t world_height = 256;
    std::string preset = "earth";

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
