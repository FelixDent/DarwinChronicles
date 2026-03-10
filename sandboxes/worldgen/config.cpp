#include "config.h"

#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace sandbox {

// ── Presets (terrain-only parameters) ────────────────────────────────────────

Preset get_preset(const std::string& name) {
    if (name == "earth") {
        return Preset{
            .env = {.water_level = 0.45f,
                    .noise_scale = 0.02f,
                    .continent_threshold = 0.55f,
                    .ridge_strength = 1.0f,
                    .plate_count = 12,
                    .continental_ratio = 0.40f},
        };
    }
    if (name == "venus") {
        return Preset{
            .env = {.water_level = 0.35f,
                    .noise_scale = 0.02f,
                    .continent_threshold = 0.65f,
                    .ridge_strength = 1.4f,
                    .plate_count = 8,
                    .continental_ratio = 0.60f},
        };
    }
    if (name == "mars") {
        return Preset{
            .env = {.water_level = 0.30f,
                    .noise_scale = 0.02f,
                    .continent_threshold = 0.50f,
                    .ridge_strength = 0.8f,
                    .plate_count = 10,
                    .continental_ratio = 0.50f},
        };
    }
    if (name == "snowball") {
        return Preset{
            .env = {.water_level = 0.55f,
                    .noise_scale = 0.02f,
                    .continent_threshold = 0.60f,
                    .ridge_strength = 0.6f,
                    .plate_count = 14,
                    .continental_ratio = 0.30f},
        };
    }
    if (name == "tropical") {
        return Preset{
            .env = {.water_level = 0.45f,
                    .noise_scale = 0.025f,
                    .continent_threshold = 0.50f,
                    .ridge_strength = 1.2f,
                    .plate_count = 10,
                    .continental_ratio = 0.45f},
        };
    }
    if (name == "toxic") {
        return Preset{
            .env = {.water_level = 0.40f,
                    .noise_scale = 0.02f,
                    .continent_threshold = 0.55f,
                    .ridge_strength = 1.5f,
                    .plate_count = 16,
                    .continental_ratio = 0.35f},
        };
    }
    throw std::runtime_error("Unknown preset: " + name);
}

// ── CLI parsing ─────────────────────────────────────────────────────────────

static void print_usage(const char* program) {
    std::cout << "Usage: " << program << " [options]\n"
              << "\nOptions:\n"
              << "  --seed N              World seed (default: 42)\n"
              << "  --width N             World width in tiles (default: 256)\n"
              << "  --height N            World height in tiles (default: 256)\n"
              << "  --preset NAME         Preset: earth, venus, mars, snowball, tropical, toxic\n"
              << "  --water-level F       Override water level [0.0, 0.8]\n"
              << "  --continent-bias F    Override continent threshold [0.0, 1.0]\n"
              << "  --ridge-strength F    Override ridge strength [0.0, 3.0]\n"
              << "  --headless            Generate BMP + stats, no window\n"
              << "  --help                Show this help\n";
}

SandboxConfig parse_args(int argc, char* argv[]) {
    SandboxConfig cfg;

    for (int i = 1; i < argc; ++i) {
        auto arg = std::string(argv[i]);
        auto next = [&]() -> std::string {
            if (i + 1 >= argc)
                throw std::runtime_error("Missing value for " + arg);
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
        } else if (arg == "--headless") {
            cfg.headless = true;
        } else if (arg == "--tile-test") {
            cfg.tile_test = true;
            // Optional: --tile-test 64 sets ppt
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                cfg.tile_test_ppt = std::stoi(next());
            }
        } else if (arg == "--preset") {
            cfg.preset = next();
        } else if (arg == "--water-level") {
            cfg.override_water_level = std::stof(next());
        } else if (arg == "--continent-bias") {
            cfg.override_continent_threshold = std::stof(next());
        } else if (arg == "--ridge-strength") {
            cfg.override_ridge_strength = std::stof(next());
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            print_usage(argv[0]);
            std::exit(1);
        }
    }

    // Resolve preset → env params
    auto preset = get_preset(cfg.preset);
    cfg.env = preset.env;

    // Apply CLI overrides
    if (cfg.override_water_level >= 0.0f)
        cfg.env.water_level = cfg.override_water_level;
    if (cfg.override_continent_threshold >= 0.0f)
        cfg.env.continent_threshold = cfg.override_continent_threshold;
    if (cfg.override_ridge_strength >= 0.0f)
        cfg.env.ridge_strength = cfg.override_ridge_strength;

    return cfg;
}

}  // namespace sandbox
