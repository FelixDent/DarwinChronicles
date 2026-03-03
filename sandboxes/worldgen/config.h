#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sandbox {

// ── Planetary parameters ────────────────────────────────────────────────────

struct Star {
    float luminosity_rel_sun = 1.0f;
};

struct Atmosphere {
    float pressure_bar = 1.0f;
    float co2_fraction = 0.0004f;
    float o2_fraction = 0.21f;
    float n2_fraction = 0.78f;
    float albedo = 0.3f;
    float ozone_shield = 0.9f;
};

struct PlanetConfig {
    Star star;
    float distance_au = 1.0f;
    Atmosphere atmosphere;
};

// ── Climate (computed from planetary params) ────────────────────────────────

struct Climate {
    float mean_temp_K = 288.0f;
    float temp_range_K = 57.6f;
    float uv_intensity = 0.1f;
    float habitability_factor = 0.87f;
};

Climate compute_climate(const PlanetConfig& planet);

// ── Environment parameters (fed to world generation) ────────────────────────

struct EnvParams {
    float nutrient_richness = 1.0f;
    float humidity = 0.7f;
    float toxicity_level = 0.1f;
    float water_level = 0.45f;
    float noise_scale = 0.02f;
    float wind_direction_deg = 270.0f;  // Prevailing wind direction (degrees, 0=east, 270=west)

    static EnvParams from_climate(const Climate& climate);
};

// ── Sandbox configuration ───────────────────────────────────────────────────

struct SandboxConfig {
    uint32_t seed = 42;
    uint32_t world_width = 256;
    uint32_t world_height = 256;
    std::string preset = "earth";

    PlanetConfig planet;
    Climate climate;
    EnvParams env;

    // CLI overrides (NaN = not overridden)
    float override_humidity = -1.0f;
    float override_toxicity = -1.0f;
    float override_nutrients = -1.0f;
    float override_temp = -1.0f;
    float override_wind = -1.0f;
};

// Preset: planet config + curated env params
struct Preset {
    PlanetConfig planet;
    EnvParams env;
};

Preset get_preset(const std::string& name);

// CLI parsing
SandboxConfig parse_args(int argc, char* argv[]);

}  // namespace sandbox
