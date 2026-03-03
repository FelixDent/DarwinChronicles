#include "config.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace sandbox {

// ── Climate computation (from docs/systems/PLANETARY_MODEL.md) ──────────────

static float stellar_flux(const Star& star, float distance_au) {
    return star.luminosity_rel_sun / (distance_au * distance_au);
}

static float equilibrium_temperature(float flux, float albedo) {
    constexpr float EARTH_MEAN_TEMP = 288.0f;
    constexpr float EARTH_ALBEDO = 0.3f;
    float ratio = (flux * (1.0f - albedo)) / (1.0f - EARTH_ALBEDO);
    return EARTH_MEAN_TEMP * std::pow(ratio, 0.25f);
}

static float greenhouse_offset(const Atmosphere& atm) {
    float strength = (atm.co2_fraction * atm.pressure_bar) / 0.0004f;
    float offset = 33.0f * std::log10(1.0f + strength);
    return std::clamp(offset, 0.0f, 100.0f);
}

static float temperature_habitability(float temp_K) {
    constexpr float OPTIMAL_TEMP = 295.0f;
    constexpr float SIGMA = 20.0f;
    float d = temp_K - OPTIMAL_TEMP;
    return std::exp(-d * d / (2.0f * SIGMA * SIGMA));
}

Climate compute_climate(const PlanetConfig& planet) {
    float flux = stellar_flux(planet.star, planet.distance_au);
    float t_eq = equilibrium_temperature(flux, planet.atmosphere.albedo);
    float gh = greenhouse_offset(planet.atmosphere);
    float mean_temp = t_eq + gh;

    float temp_range = 0.2f * mean_temp;

    float uv_raw = flux;
    float uv_surface = uv_raw * (1.0f - planet.atmosphere.ozone_shield);
    float uv = std::max(uv_surface, 0.0f);

    float temp_score = temperature_habitability(mean_temp);
    float uv_penalty = std::exp(-uv);
    float habitability = std::clamp(temp_score * uv_penalty, 0.0f, 1.0f);

    return Climate{
        .mean_temp_K = mean_temp,
        .temp_range_K = temp_range,
        .uv_intensity = uv,
        .habitability_factor = habitability,
    };
}

// ── EnvParams from Climate ──────────────────────────────────────────────────

EnvParams EnvParams::from_climate(const Climate& climate) {
    return EnvParams{
        .nutrient_richness = climate.habitability_factor,
        .humidity = climate.habitability_factor * 0.7f,
        .toxicity_level = std::min(climate.uv_intensity, 1.0f),
    };
}

// ── Presets (planet config + curated env params) ────────────────────────────
// Each preset has hand-tuned env params matching the expected biome distribution
// from docs/systems/WORLD_GENERATION.md rather than auto-deriving from climate.

Preset get_preset(const std::string& name) {
    if (name == "earth") {
        return Preset{
            .planet = {.star = {1.0f}, .distance_au = 1.0f,
                       .atmosphere = {1.0f, 0.0004f, 0.21f, 0.78f, 0.3f, 0.9f}},
            .env = {.nutrient_richness = 1.0f, .humidity = 0.7f, .toxicity_level = 0.1f,
                    .water_level = 0.45f, .noise_scale = 0.02f, .wind_direction_deg = 270.0f},
        };
    }
    if (name == "venus") {
        return Preset{
            .planet = {.star = {1.0f}, .distance_au = 0.72f,
                       .atmosphere = {92.0f, 0.965f, 0.0f, 0.035f, 0.75f, 0.0f}},
            .env = {.nutrient_richness = 0.2f, .humidity = 0.05f, .toxicity_level = 0.9f,
                    .water_level = 0.35f, .noise_scale = 0.02f, .wind_direction_deg = 180.0f},
        };
    }
    if (name == "mars") {
        return Preset{
            .planet = {.star = {1.0f}, .distance_au = 1.52f,
                       .atmosphere = {0.006f, 0.95f, 0.0013f, 0.027f, 0.25f, 0.0f}},
            .env = {.nutrient_richness = 0.4f, .humidity = 0.2f, .toxicity_level = 0.3f,
                    .water_level = 0.35f, .noise_scale = 0.02f, .wind_direction_deg = 315.0f},
        };
    }
    if (name == "snowball") {
        return Preset{
            .planet = {.star = {0.8f}, .distance_au = 1.8f,
                       .atmosphere = {0.5f, 0.001f, 0.18f, 0.78f, 0.6f, 0.5f}},
            .env = {.nutrient_richness = 0.3f, .humidity = 0.4f, .toxicity_level = 0.15f,
                    .water_level = 0.45f, .noise_scale = 0.02f, .wind_direction_deg = 240.0f},
        };
    }
    if (name == "tropical") {
        return Preset{
            .planet = {.star = {1.0f}, .distance_au = 0.95f,
                       .atmosphere = {2.5f, 0.002f, 0.25f, 0.73f, 0.25f, 0.95f}},
            .env = {.nutrient_richness = 1.5f, .humidity = 0.9f, .toxicity_level = 0.05f,
                    .water_level = 0.45f, .noise_scale = 0.02f, .wind_direction_deg = 90.0f},
        };
    }
    if (name == "toxic") {
        return Preset{
            .planet = {.star = {1.2f}, .distance_au = 0.9f,
                       .atmosphere = {3.0f, 0.01f, 0.10f, 0.50f, 0.2f, 0.1f}},
            .env = {.nutrient_richness = 0.8f, .humidity = 0.6f, .toxicity_level = 0.7f,
                    .water_level = 0.45f, .noise_scale = 0.02f, .wind_direction_deg = 200.0f},
        };
    }
    throw std::runtime_error("Unknown preset: " + name);
}

// ── CLI parsing ─────────────────────────────────────────────────────────────

static void print_usage(const char* program) {
    std::cout << "Usage: " << program << " [options]\n"
              << "\nOptions:\n"
              << "  --seed N         World seed (default: 42)\n"
              << "  --width N        World width in tiles (default: 256)\n"
              << "  --height N       World height in tiles (default: 256)\n"
              << "  --preset NAME    Planet preset: earth, venus, mars, snowball, tropical, toxic\n"
              << "  --humidity F     Override humidity [0.0, 1.0]\n"
              << "  --toxicity F     Override toxicity [0.0, 1.0]\n"
              << "  --nutrients F    Override nutrient richness [0.0, 2.0]\n"
              << "  --temp F         Override mean temperature (Kelvin)\n"
              << "  --wind F         Override wind direction (degrees, 0=east, 270=west)\n"
              << "  --help           Show this help\n";
}

SandboxConfig parse_args(int argc, char* argv[]) {
    SandboxConfig cfg;

    for (int i = 1; i < argc; ++i) {
        auto arg = std::string(argv[i]);
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) throw std::runtime_error("Missing value for " + arg);
            return argv[++i];
        };

        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            std::exit(0);
        } else if (arg == "--seed") {
            cfg.seed = static_cast<uint32_t>(std::stoul(next()));
        } else if (arg == "--width") {
            cfg.world_width = static_cast<uint32_t>(std::stoul(next()));
        } else if (arg == "--height") {
            cfg.world_height = static_cast<uint32_t>(std::stoul(next()));
        } else if (arg == "--preset") {
            cfg.preset = next();
        } else if (arg == "--humidity") {
            cfg.override_humidity = std::stof(next());
        } else if (arg == "--toxicity") {
            cfg.override_toxicity = std::stof(next());
        } else if (arg == "--nutrients") {
            cfg.override_nutrients = std::stof(next());
        } else if (arg == "--temp") {
            cfg.override_temp = std::stof(next());
        } else if (arg == "--wind") {
            cfg.override_wind = std::stof(next());
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            print_usage(argv[0]);
            std::exit(1);
        }
    }

    // Resolve preset → planet + curated env, compute climate from planet
    auto preset = get_preset(cfg.preset);
    cfg.planet = preset.planet;
    cfg.env = preset.env;
    cfg.climate = compute_climate(cfg.planet);

    // Apply CLI overrides
    if (cfg.override_humidity >= 0.0f) cfg.env.humidity = cfg.override_humidity;
    if (cfg.override_toxicity >= 0.0f) cfg.env.toxicity_level = cfg.override_toxicity;
    if (cfg.override_nutrients >= 0.0f) cfg.env.nutrient_richness = cfg.override_nutrients;
    if (cfg.override_temp >= 0.0f) cfg.climate.mean_temp_K = cfg.override_temp;
    if (cfg.override_wind >= 0.0f) cfg.env.wind_direction_deg = cfg.override_wind;

    return cfg;
}

}  // namespace sandbox
