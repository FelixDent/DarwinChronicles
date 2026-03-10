#include <SDL.h>

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>

#include "config.h"
#include "renderer.h"
#include "telemetry.h"
#include "terrain_gen.h"

using namespace sandbox;

// ── Tweak parameters ────────────────────────────────────────────────────────

enum class TweakParam {
    Seed,
    WaterLevel,
    NoiseScale,
    ContinentThreshold,
    RidgeStrength,

    COUNT
};

static const char* tweak_name(TweakParam p) {
    switch (p) {
        case TweakParam::Seed:
            return "Seed";
        case TweakParam::WaterLevel:
            return "WaterLevel";
        case TweakParam::NoiseScale:
            return "NoiseScale";
        case TweakParam::ContinentThreshold:
            return "ContinentThreshold";
        case TweakParam::RidgeStrength:
            return "RidgeStrength";
        default:
            return "Unknown";
    }
}

static float tweak_value(TweakParam p, const SandboxConfig& cfg) {
    switch (p) {
        case TweakParam::Seed:
            return static_cast<float>(cfg.seed);
        case TweakParam::WaterLevel:
            return cfg.env.water_level;
        case TweakParam::NoiseScale:
            return cfg.env.noise_scale;
        case TweakParam::ContinentThreshold:
            return cfg.env.continent_threshold;
        case TweakParam::RidgeStrength:
            return cfg.env.ridge_strength;
        default:
            return 0.0f;
    }
}

static float tweak_step(TweakParam p) {
    switch (p) {
        case TweakParam::Seed:
            return 1.0f;
        case TweakParam::WaterLevel:
            return 0.05f;
        case TweakParam::NoiseScale:
            return 0.005f;
        case TweakParam::ContinentThreshold:
            return 0.05f;
        case TweakParam::RidgeStrength:
            return 0.1f;
        default:
            return 0.01f;
    }
}

static void tweak_apply(TweakParam p, SandboxConfig& cfg, float delta) {
    float old_val = tweak_value(p, cfg);
    switch (p) {
        case TweakParam::Seed:
            cfg.seed = static_cast<uint32_t>(std::max(0.0f, static_cast<float>(cfg.seed) + delta));
            break;
        case TweakParam::WaterLevel:
            cfg.env.water_level = std::clamp(cfg.env.water_level + delta, 0.0f, 0.8f);
            break;
        case TweakParam::NoiseScale:
            cfg.env.noise_scale = std::clamp(cfg.env.noise_scale + delta, 0.001f, 0.1f);
            break;
        case TweakParam::ContinentThreshold:
            cfg.env.continent_threshold =
                std::clamp(cfg.env.continent_threshold + delta, 0.0f, 1.0f);
            break;
        case TweakParam::RidgeStrength:
            cfg.env.ridge_strength = std::clamp(cfg.env.ridge_strength + delta, 0.0f, 3.0f);
            break;
        default:
            break;
    }
    float new_val = tweak_value(p, cfg);
    std::cout << "[TWEAK] " << tweak_name(p) << ": " << std::fixed << std::setprecision(3)
              << old_val << " -> " << new_val << "\n";
}

// ── Log export ──────────────────────────────────────────────────────────────

static void export_log(const SandboxConfig& cfg, const GenerationTimings& timings,
                       const TerrainStats& stats) {
    std::string filename = "terraingen_log_" + std::to_string(cfg.seed) + ".txt";
    std::ofstream out(filename);
    if (!out) {
        std::cerr << "Failed to write " << filename << "\n";
        return;
    }

    out << "=== Terrain Generation Log ===\n";
    out << "Preset: " << cfg.preset << "\n";
    out << "Seed: " << cfg.seed << "\n";
    out << "Size: " << cfg.world_width << "x" << cfg.world_height << "\n\n";

    out << "Parameters:\n";
    out << "  Water Level:          " << cfg.env.water_level << "\n";
    out << "  Noise Scale:          " << cfg.env.noise_scale << "\n";
    out << "  Continent Threshold:  " << cfg.env.continent_threshold << "\n";
    out << "  Ridge Strength:       " << cfg.env.ridge_strength << "\n\n";

    out << "Generation Timings:\n";
    out << "  Height:       " << timings.height_ms << " ms\n";
    out << "  Ridge:        " << timings.ridge_ms << " ms\n";
    out << "  Unify:        " << timings.unify_ms << " ms\n";
    out << "  Ocean:        " << timings.ocean_ms << " ms\n";
    out << "  Slope/Band:   " << timings.slope_band_ms << " ms\n";
    out << "  Dist Fields:  " << timings.dist_fields_ms << " ms\n";
    out << "  Geology:      " << timings.geology_ms << " ms\n";
    out << "  Roughness:    " << timings.roughness_ms << " ms\n";
    out << "  Total:        " << timings.total_ms << " ms\n\n";

    out << "Tile Counts:\n";
    out << "  Ocean: " << stats.ocean_tiles << "\n";
    out << "  Land:  " << stats.land_tiles << "\n";

    const char* band_names[] = {"Water", "Lowland", "Hills", "Mountains"};
    out << "\nElevation Bands:\n";
    for (int i = 0; i < 4; ++i) {
        float pct = 100.0f * static_cast<float>(stats.band_counts[static_cast<size_t>(i)]) /
                    static_cast<float>(stats.total_tiles);
        out << "  " << std::left << std::setw(12) << band_names[i]
            << stats.band_counts[static_cast<size_t>(i)] << " (" << std::fixed
            << std::setprecision(1) << pct << "%)\n";
    }

    out.close();
    std::cout << "[EXPORT] Log written to " << filename << "\n";
}

// ── Screenshot ──────────────────────────────────────────────────────────────

static void take_screenshot(SDL_Renderer* renderer, int w, int h, const std::string& filename) {
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surf) {
        std::cerr << "Screenshot failed: " << SDL_GetError() << "\n";
        return;
    }
    SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGBA32, surf->pixels, surf->pitch);
    SDL_SaveBMP(surf, filename.c_str());
    SDL_FreeSurface(surf);
    std::cout << "[EXPORT] Screenshot saved to " << filename << "\n";
}

// ── Signal handling ──────────────────────────────────────────────────────────

static volatile sig_atomic_t g_quit = 0;

static void signal_handler(int /*sig*/) {
    g_quit = 1;
}

// ── Main ────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    SandboxConfig cfg = parse_args(argc, argv);

    std::cout << "Darwin Chronicles — Terrain Generation Sandbox\n";
    std::cout << "Preset: " << cfg.preset << " | Seed: " << cfg.seed
              << " | Size: " << cfg.world_width << "x" << cfg.world_height << "\n";
    std::cout << "Env: water_level=" << cfg.env.water_level
              << ", noise_scale=" << cfg.env.noise_scale
              << ", continent=" << cfg.env.continent_threshold
              << ", ridge=" << cfg.env.ridge_strength << "\n\n";

    // Generate terrain
    GenerationTimings timings;
    Terrain terrain =
        generate_terrain(cfg.world_width, cfg.world_height, cfg.env, cfg.seed, &timings);

    std::cout << "Generation timings:\n";
    std::cout << "  Height:       " << std::fixed << std::setprecision(1) << timings.height_ms
              << " ms\n";
    std::cout << "  Ridge:        " << timings.ridge_ms << " ms\n";
    std::cout << "  Unify:        " << timings.unify_ms << " ms\n";
    std::cout << "  Ocean:        " << timings.ocean_ms << " ms\n";
    std::cout << "  Slope/Band:   " << timings.slope_band_ms << " ms\n";
    std::cout << "  Dist Fields:  " << timings.dist_fields_ms << " ms\n";
    std::cout << "  Geology:      " << timings.geology_ms << " ms\n";
    std::cout << "  Roughness:    " << timings.roughness_ms << " ms\n";
    std::cout << "  Total:        " << timings.total_ms << " ms\n";

    TerrainStats stats = compute_stats(terrain);
    print_stats(stats);
    std::cout << std::flush;

    // ── Headless mode: render BMP and exit ──────────────────────────────
    if (cfg.headless) {
        if (SDL_Init(SDL_INIT_VIDEO) < 0) {
            std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
            return 1;
        }
        // Render terrain to a surface (1 pixel per tile)
        uint32_t tw = cfg.world_width;
        uint32_t th = cfg.world_height;
        SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(
            0, static_cast<int>(tw), static_cast<int>(th), 32, SDL_PIXELFORMAT_RGBA32);
        if (!surf) {
            std::cerr << "Failed to create surface: " << SDL_GetError() << "\n";
            return 1;
        }
        SDL_LockSurface(surf);
        auto* pixels = static_cast<uint32_t*>(surf->pixels);
        for (uint32_t y = 0; y < th; ++y) {
            for (uint32_t x = 0; x < tw; ++x) {
                const auto& tile = terrain.tile_at(x, y);
                // Compute terrain color (same logic as renderer.cpp)
                uint8_t r, g, b;
                if (tile.is_ocean) {
                    // Continuous depth-based ocean coloring with smooth gradient
                    float depth = cfg.env.water_level - tile.elev01;
                    float max_depth = cfg.env.water_level;
                    float depth_norm = std::clamp(depth / std::max(max_depth, 0.01f), 0.0f, 1.0f);

                    // Compute local depth gradient for shelf detection
                    float depth_slope = 0.0f;
                    if (x > 0 && x < tw - 1 && y > 0 && y < th - 1) {
                        float dx =
                            terrain.tile_at(x + 1, y).elev01 - terrain.tile_at(x - 1, y).elev01;
                        float dy =
                            terrain.tile_at(x, y + 1).elev01 - terrain.tile_at(x, y - 1).elev01;
                        depth_slope = std::sqrt(dx * dx + dy * dy);
                    }

                    // Continuous depth-based ocean coloring — subtle gradient
                    // Colors chosen to avoid a bright "halo" around coasts
                    float d = depth_norm;
                    // Non-linear depth mapping: gamma compress shallow depths
                    // so the shelf doesn't create a bright "ring" — it merges quickly to mid-ocean
                    float dg = std::pow(d, 0.6f);  // gamma compress: shallow jumps to mid fast
                    float cr = 25.0f * (1.0f - dg) * (1.0f - dg) + 16.0f * dg * (1.0f - dg) +
                               8.0f * dg * dg;
                    float cg = 52.0f * (1.0f - dg) * (1.0f - dg) + 42.0f * dg * (1.0f - dg) +
                               22.0f * dg * dg;
                    float cb = 95.0f * (1.0f - dg) * (1.0f - dg) + 92.0f * dg * (1.0f - dg) +
                               78.0f * dg * dg;

                    // Very subtle depth-slope shading (no bright shelf highlight)
                    float depth_shade = std::clamp(1.0f - depth_slope * 15.0f, 0.85f, 1.0f);
                    cr *= depth_shade;
                    cg *= depth_shade;
                    cb *= depth_shade;

                    r = static_cast<uint8_t>(std::clamp(cr, 0.0f, 255.0f));
                    g = static_cast<uint8_t>(std::clamp(cg, 0.0f, 255.0f));
                    b = static_cast<uint8_t>(std::clamp(cb, 0.0f, 255.0f));
                } else {
                    // Hillshade: directional lighting from NW with enhanced contrast
                    float shade = 1.0f;
                    if (x > 0 && x < tw - 1 && y > 0 && y < th - 1) {
                        float dzdx =
                            terrain.tile_at(x + 1, y).elev01 - terrain.tile_at(x - 1, y).elev01;
                        float dzdy =
                            terrain.tile_at(x, y + 1).elev01 - terrain.tile_at(x, y - 1).elev01;
                        // Light from NW (azimuth 315°, altitude 40° for more shadow)
                        constexpr float az = 5.497787f;  // 315° in rad
                        constexpr float alt = 0.6981f;   // 40° in rad
                        float slope_mag = std::sqrt(dzdx * dzdx + dzdy * dzdy);
                        float slope_angle = std::atan(
                            slope_mag * 6.0f);  // moderate exaggeration (photoreal, not metallic)
                        float aspect = std::atan2(-dzdy, -dzdx);
                        shade = std::cos(slope_angle) * std::sin(alt) +
                                std::sin(slope_angle) * std::cos(alt) * std::cos(az - aspect);
                        shade = std::clamp(shade * 0.7f + 0.3f, 0.2f, 1.05f);
                    }
                    // Climate-driven biome coloring with soft transitions
                    float norm_elev = std::clamp(
                        (tile.elev01 - cfg.env.water_level) / (1.0f - cfg.env.water_level), 0.0f,
                        1.0f);

                    // Climate model: temperature from latitude + lapse rate
                    float lat_t = static_cast<float>(y) / static_cast<float>(th);
                    float lat_factor = 1.0f - 2.0f * std::abs(lat_t - 0.5f);
                    float temp = lat_factor * 30.0f - norm_elev * 40.0f;

                    // Moisture: multiple factors so it doesn't look like a coastal outline
                    float base_moisture = std::clamp(1.0f - tile.dist_ocean * 0.02f, 0.15f, 0.85f);
                    float lat_moist = 0.65f + 0.35f * lat_factor;
                    // Steep slopes: exposed rock = less green
                    float slope_moisture = std::clamp(1.0f - tile.slope01 * 2.0f, 0.3f, 1.0f);
                    // Continental drying
                    float inland_dry =
                        (tile.dist_ocean > 25.0f)
                            ? std::clamp(1.0f - (tile.dist_ocean - 25.0f) * 0.01f, 0.45f, 1.0f)
                            : 1.0f;
                    // Mesoscale variation from terrain roughness + aspect to break uniformity
                    float meso_noise =
                        (tile.roughness - 0.5f) * 0.25f + std::cos(tile.aspect * 2.0f) * 0.06f;
                    // Soil proxy: ridges/high-elev bare, valleys/low-elev have soil
                    float soil_proxy =
                        std::clamp(1.0f - norm_elev * 0.5f - tile.slope01 * 1.5f, 0.25f, 1.0f);
                    float moisture = std::clamp(
                        base_moisture * lat_moist * slope_moisture * inland_dry * soil_proxy +
                            meso_noise,
                        0.08f, 1.0f);
                    // River moisture contribution removed (rivers now dynamic)

                    // Soft logistic weighting for each biome (smooth ecotone transitions)
                    auto logistic = [](float x, float center, float width) -> float {
                        return 1.0f / (1.0f + std::exp(-(x - center) / std::max(width, 0.01f)));
                    };

                    // Biome weights (sharper logistic transitions — visible ecotones, not mush)
                    float w_tundra = (1.0f - logistic(temp, -5.0f, 1.2f));
                    float w_boreal =
                        logistic(temp, -5.0f, 1.2f) * (1.0f - logistic(temp, 5.0f, 1.5f));
                    float w_alpine =
                        logistic(norm_elev, 0.55f, 0.04f) * logistic(temp, -3.0f, 2.0f);
                    float w_arid =
                        (1.0f - logistic(moisture, 0.30f, 0.04f)) * logistic(temp, 12.0f, 2.0f);
                    float w_steppe = logistic(moisture, 0.25f, 0.04f) *
                                     (1.0f - logistic(moisture, 0.50f, 0.04f)) *
                                     logistic(temp, 10.0f, 2.0f);
                    float w_tropical =
                        logistic(temp, 20.0f, 1.5f) * logistic(moisture, 0.60f, 0.05f);
                    float w_temperate = logistic(temp, 5.0f, 1.5f) *
                                        logistic(moisture, 0.40f, 0.05f) *
                                        (1.0f - logistic(temp, 22.0f, 2.0f));

                    // Biome colors
                    struct BiomeColor {
                        float r, g, b;
                    };
                    BiomeColor tundra = {170.0f, 175.0f, 185.0f};
                    BiomeColor boreal = {75.0f + norm_elev * 15.0f, 90.0f + norm_elev * 10.0f,
                                         68.0f};
                    BiomeColor alpine = {135.0f - norm_elev * 15.0f, 112.0f - norm_elev * 10.0f,
                                         100.0f + norm_elev * 20.0f};
                    BiomeColor arid = {192.0f + norm_elev * 10.0f, 172.0f - norm_elev * 8.0f,
                                       125.0f - norm_elev * 15.0f};
                    BiomeColor steppe = {148.0f + norm_elev * 20.0f, 142.0f - norm_elev * 5.0f,
                                         75.0f};
                    BiomeColor tropical = {48.0f + norm_elev * 25.0f, 98.0f + norm_elev * 15.0f,
                                           38.0f + norm_elev * 10.0f};
                    BiomeColor temperate = {85.0f + norm_elev * 50.0f, 118.0f - norm_elev * 5.0f,
                                            58.0f + norm_elev * 8.0f};

                    // Weighted blend of all biome colors
                    float total_w = w_tundra + w_boreal + w_alpine + w_arid + w_steppe +
                                    w_tropical + w_temperate;
                    total_w = std::max(total_w, 0.01f);
                    float cr = (w_tundra * tundra.r + w_boreal * boreal.r + w_alpine * alpine.r +
                                w_arid * arid.r + w_steppe * steppe.r + w_tropical * tropical.r +
                                w_temperate * temperate.r) /
                               total_w;
                    float cg = (w_tundra * tundra.g + w_boreal * boreal.g + w_alpine * alpine.g +
                                w_arid * arid.g + w_steppe * steppe.g + w_tropical * tropical.g +
                                w_temperate * temperate.g) /
                               total_w;
                    float cb = (w_tundra * tundra.b + w_boreal * boreal.b + w_alpine * alpine.b +
                                w_arid * arid.b + w_steppe * steppe.b + w_tropical * tropical.b +
                                w_temperate * temperate.b) /
                               total_w;

                    // Snow: temperature + elevation + aspect + slope + moisture
                    if (temp < 0.0f && norm_elev > 0.35f) {
                        float slope_mag_local = 0.0f;
                        if (x > 0 && x < tw - 1 && y > 0 && y < th - 1) {
                            float sdx =
                                terrain.tile_at(x + 1, y).elev01 - terrain.tile_at(x - 1, y).elev01;
                            float sdy =
                                terrain.tile_at(x, y + 1).elev01 - terrain.tile_at(x, y - 1).elev01;
                            slope_mag_local = std::sqrt(sdx * sdx + sdy * sdy);
                        }
                        float snow = std::clamp(-temp / 12.0f, 0.0f, 0.80f);
                        // Steep slopes shed snow
                        snow *= std::clamp(1.0f - slope_mag_local * 10.0f, 0.0f, 1.0f);
                        // Elevation ramp
                        snow *= std::clamp((norm_elev - 0.35f) / 0.30f, 0.0f, 1.0f);
                        // Aspect: pole-facing slopes accumulate more snow
                        float pole_facing =
                            (lat_t < 0.5f)
                                ? std::clamp(-std::sin(tile.aspect) * 0.5f + 0.5f, 0.3f, 1.0f)
                                : std::clamp(std::sin(tile.aspect) * 0.5f + 0.5f, 0.3f, 1.0f);
                        snow *= pole_facing;
                        // Moisture: wetter areas have more snow
                        snow *= std::clamp(moisture * 1.3f, 0.4f, 1.0f);
                        // Slight blue-white tint variation
                        float warm_snow = std::clamp(temp / -5.0f, 0.0f, 1.0f);
                        float sr = 228.0f + warm_snow * 10.0f;
                        float sg = 233.0f + warm_snow * 8.0f;
                        float sb = 242.0f + warm_snow * 6.0f;
                        cr = cr * (1.0f - snow) + sr * snow;
                        cg = cg * (1.0f - snow) + sg * snow;
                        cb = cb * (1.0f - snow) + sb * snow;
                    }
                    // Aspect-based albedo variation: sun-facing slopes slightly warmer,
                    // shadow-facing cooler/darker. Adds material variety to mountains.
                    if (norm_elev > 0.25f) {
                        float aspect_v = tile.aspect;
                        // South-facing (NH) or north-facing (SH) warmer
                        float sun_facing = std::cos(aspect_v) * (lat_t < 0.5f ? -1.0f : 1.0f);
                        float elev_scale = std::clamp((norm_elev - 0.25f) * 3.0f, 0.0f, 1.0f);
                        float albedo_shift = sun_facing * elev_scale * 8.0f;
                        cr += albedo_shift;
                        cg += albedo_shift * 0.7f;
                        cb -= albedo_shift * 0.3f;  // warm shift
                    }
                    r = static_cast<uint8_t>(std::clamp(cr * shade, 0.0f, 255.0f));
                    g = static_cast<uint8_t>(std::clamp(cg * shade, 0.0f, 255.0f));
                    b = static_cast<uint8_t>(std::clamp(cb * shade, 0.0f, 255.0f));
                }
                // Rivers removed from static terrain (now dynamic)
                pixels[y * (static_cast<uint32_t>(surf->pitch) / 4) + x] =
                    SDL_MapRGBA(surf->format, r, g, b, 255);
            }
        }
        SDL_UnlockSurface(surf);

        std::string fname = "terrain_" + cfg.preset + "_" + std::to_string(cfg.seed) + ".bmp";
        SDL_SaveBMP(surf, fname.c_str());
        SDL_FreeSurface(surf);
        std::cout << "[HEADLESS] BMP saved to " << fname << "\n";
        SDL_Quit();
        return 0;
    }

    // ── Tile test mode: render representative tiles at high zoom ────────
    if (cfg.tile_test) {
        if (SDL_Init(SDL_INIT_VIDEO) < 0) {
            std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
            return 1;
        }

        const int PPT = 16;                   // native: 16 pixels per tile
        const int PATCH = 24;                 // render 24×24 tiles centered on target
        const int TILE_PX = PATCH * PPT;      // 384×384 pixels per sample
        const float WPP = 1.0f / static_cast<float>(PPT);

        // Categories of tiles to find
        struct TileSample {
            const char* label;
            int tx, ty;
            bool found;
        };

        // Search for representative tiles
        auto w = cfg.world_width;
        auto h = cfg.world_height;
        float wl = cfg.env.water_level;

        // We'll find: deep ocean, shelf, beach, flat lowland, rolling lowland,
        // foothill, mountain slope, mountain peak, different rock types
        TileSample samples[] = {
            {"deep_ocean", 0, 0, false},
            {"shelf", 0, 0, false},
            {"beach", 0, 0, false},
            {"coast_gentle", 0, 0, false},
            {"coast_medium", 0, 0, false},
            {"coast_cliff", 0, 0, false},
            {"flat_lowland", 0, 0, false},
            {"rolling_lowland", 0, 0, false},
            {"hills", 0, 0, false},
            {"mountain_slope", 0, 0, false},
            {"mountain_peak", 0, 0, false},
            {"steep_cliff", 0, 0, false},
            {"interior_plateau", 0, 0, false},
            {"coastal_plain", 0, 0, false},
            {"river_valley", 0, 0, false},
        };
        constexpr int NUM_SAMPLES = sizeof(samples) / sizeof(samples[0]);

        // Margin: half-patch so rendering doesn't go out of bounds
        constexpr int M = PATCH / 2 + 1;

        for (uint32_t y = M; y < h - M && !g_quit; ++y) {
            for (uint32_t x = M; x < w - M; ++x) {
                const auto& t = terrain.tile_at(x, y);
                int ix = static_cast<int>(x), iy = static_cast<int>(y);

                // Deep ocean: far from coast, clearly underwater
                if (!samples[0].found && t.is_ocean && t.dist_ocean < 0.5f &&
                    t.elev01 < wl - 0.15f) {
                    samples[0] = {"deep_ocean", ix, iy, true};
                }
                // Shelf: ocean, near coast
                if (!samples[1].found && t.is_ocean && t.dist_ocean < 0.5f &&
                    t.elev01 > wl - 0.10f && t.elev01 < wl - 0.02f) {
                    samples[1] = {"shelf", ix, iy, true};
                }
                // Beach: land, very close to ocean, low slope (flat sandy)
                if (!samples[2].found && !t.is_ocean && t.dist_ocean < 2.0f && t.slope01 < 0.05f &&
                    t.elev01 < wl + 0.06f) {
                    samples[2] = {"beach", ix, iy, true};
                }
                // Coast tiles: must be classified as Coast family
                // Score by land/water balance in 3×3 patch — prefer ~50/50
                {
                    auto cls = classify_tile(terrain, ix, iy, wl);
                    if (cls.family == TileFamily::Coast) {
                        bool too_close = false;
                        for (int ci : {3, 4, 5}) {
                            if (samples[ci].found && std::abs(ix - samples[ci].tx) < 4 &&
                                std::abs(iy - samples[ci].ty) < 4)
                                too_close = true;
                        }
                        if (!too_close) {
                            // Count land vs water in 3×3 patch
                            int land_count = 0, total = 0;
                            for (int dy = -1; dy <= 1; ++dy)
                                for (int dx = -1; dx <= 1; ++dx) {
                                    int nx = ix + dx, ny = iy + dy;
                                    if (nx >= 0 && nx < static_cast<int>(w) && ny >= 0 &&
                                        ny < static_cast<int>(h)) {
                                        if (!terrain
                                                 .tile_at(static_cast<uint32_t>(nx),
                                                          static_cast<uint32_t>(ny))
                                                 .is_ocean)
                                            land_count++;
                                        total++;
                                    }
                                }
                            float balance = static_cast<float>(land_count) /
                                            static_cast<float>(std::max(total, 1));
                            // Best balance is 0.5; penalize extreme ratios
                            float score = 1.0f - 2.0f * std::abs(balance - 0.5f);
                            // Need at least 30% land to be useful
                            if (balance < 0.3f)
                                score *= 0.1f;

                            int slot = -1;
                            if (cls.grade == TransitionGrade::Shallow)
                                slot = 3;
                            else if (cls.grade == TransitionGrade::Medium)
                                slot = 4;
                            else
                                slot = 5;

                            // Use score to find best tile (not just first)
                            static float best_score[3] = {-1.0f, -1.0f, -1.0f};
                            int si = slot - 3;
                            if (score > best_score[si]) {
                                best_score[si] = score;
                                const char* labels[] = {"coast_gentle", "coast_medium",
                                                        "coast_cliff"};
                                samples[slot] = {labels[si], ix, iy, true};
                            }
                        }
                    }
                }
                // Flat lowland: land, low elev, very low slope
                if (!samples[6].found && !t.is_ocean && t.band == ElevBand::Lowland &&
                    t.slope01 < 0.03f && t.dist_ocean > 5.0f) {
                    samples[6] = {"flat_lowland", ix, iy, true};
                }
                // Rolling lowland: moderate slope, lowland band
                if (!samples[7].found && !t.is_ocean && t.band == ElevBand::Lowland &&
                    t.slope01 > 0.05f && t.slope01 < 0.12f && t.dist_ocean > 5.0f) {
                    samples[7] = {"rolling_lowland", ix, iy, true};
                }
                // Hills: prefer inland to avoid coast confusion
                if (!samples[8].found && t.band == ElevBand::Hills && t.slope01 > 0.08f &&
                    t.slope01 < 0.18f && t.dist_ocean > 4.0f) {
                    samples[8] = {"hills", ix, iy, true};
                }
                // Mountain slope: high elev, steep, away from coast
                if (!samples[9].found && t.band == ElevBand::Mountains && t.slope01 > 0.15f &&
                    t.slope01 < 0.30f && t.dist_ocean > 5.0f) {
                    samples[9] = {"mountain_slope", ix, iy, true};
                }
                // Mountain peak: very high elevation, away from coast
                if (!samples[10].found && t.band == ElevBand::Mountains && t.elev01 > 0.72f &&
                    t.slope01 > 0.10f && t.dist_ocean > 6.0f) {
                    samples[10] = {"mountain_peak", ix, iy, true};
                }
                // Steep cliff: prefer inland steep terrain
                if (!samples[11].found && !t.is_ocean && t.slope01 > 0.25f && t.dist_ocean > 3.0f) {
                    samples[11] = {"steep_cliff", ix, iy, true};
                }
                // Interior plateau: high elevation, low slope, far from coast
                if (!samples[12].found && !t.is_ocean && t.elev01 > 0.55f && t.slope01 < 0.06f &&
                    t.dist_ocean > 12.0f) {
                    samples[12] = {"interior_plateau", ix, iy, true};
                }
                // Coastal plain: low elev, flat, near coast
                if (!samples[13].found && !t.is_ocean && t.band == ElevBand::Lowland &&
                    t.slope01 < 0.04f && t.dist_ocean > 2.0f && t.dist_ocean < 6.0f) {
                    samples[13] = {"coastal_plain", ix, iy, true};
                }
                // River valley: low point between higher neighbors (curvature proxy)
                if (!samples[14].found && !t.is_ocean && t.band != ElevBand::Water &&
                    t.slope01 < 0.06f && t.roughness > 0.015f && t.elev01 > wl + 0.05f) {
                    samples[14] = {"river_valley", ix, iy, true};
                }
            }
        }

        // Count found samples
        int found_count = 0;
        for (int i = 0; i < NUM_SAMPLES; ++i) {
            if (samples[i].found)
                ++found_count;
        }
        std::cout << "\n[TILE-TEST] Found " << found_count << "/" << NUM_SAMPLES
                  << " representative tiles\n";

        // Ensure atlas is ready before rendering
        {
            const auto& atlas = get_template_atlas();
            if (!atlas.valid) {
                reset_template_atlas(cfg.seed);
            }
        }

        // Render each found sample as a 3×3 tile patch at high zoom
        for (int i = 0; i < NUM_SAMPLES; ++i) {
            if (!samples[i].found) {
                std::cout << "  SKIP: " << samples[i].label << " (not found)\n";
                continue;
            }

            const auto& t = terrain.tile_at(static_cast<uint32_t>(samples[i].tx),
                                            static_cast<uint32_t>(samples[i].ty));

            // Classify tile family
            auto cls = classify_tile(terrain, samples[i].tx, samples[i].ty, cfg.env.water_level);
            const char* family_str[] = {"Pure", "Coast", "Elev"};
            const char* grade_str[] = {"shallow", "medium", "steep"};
            const char* edge_str[] = {"N", "E", "S", "W"};

            // Print tile attributes
            std::cout << "  " << samples[i].label << " @ (" << samples[i].tx << "," << samples[i].ty
                      << ")"
                      << " elev=" << std::fixed << std::setprecision(3) << t.elev01
                      << " slope=" << t.slope01 << " dist_ocean=" << std::setprecision(1)
                      << t.dist_ocean << " family=" << family_str[static_cast<int>(cls.family)];
            if (cls.family != TileFamily::PureTerrain) {
                std::cout << " " << edge_str[static_cast<int>(cls.edges.entry)] << "-"
                          << edge_str[static_cast<int>(cls.edges.exit)] << "/"
                          << grade_str[static_cast<int>(cls.grade)];
            }
            std::cout << "\n";

            // Render 3×3 tile patch centered on target
            float world_x0 = static_cast<float>(samples[i].tx - PATCH / 2);
            float world_y0 = static_cast<float>(samples[i].ty - PATCH / 2);

            std::vector<uint32_t> pixels(static_cast<size_t>(TILE_PX) *
                                         static_cast<size_t>(TILE_PX));
            render_terrain_region(terrain, world_x0, world_y0, WPP, TILE_PX, TILE_PX, cfg.seed,
                                  pixels.data(), TILE_PX, cfg.env.water_level);

            // Save as BMP
            SDL_Surface* surf =
                SDL_CreateRGBSurfaceWithFormat(0, TILE_PX, TILE_PX, 32, SDL_PIXELFORMAT_RGBA32);
            SDL_LockSurface(surf);
            std::memcpy(surf->pixels, pixels.data(),
                        static_cast<size_t>(TILE_PX) * static_cast<size_t>(TILE_PX) * 4);
            SDL_UnlockSurface(surf);

            // Auto-increment version: find next unused v1, v2, v3...
            std::string base = std::string("tile_") + samples[i].label;
            std::string fname;
            for (int ver = 1; ; ++ver) {
                fname = base + "_v" + std::to_string(ver) + ".bmp";
                std::ifstream check(fname);
                if (!check.good()) break;
            }
            SDL_SaveBMP(surf, fname.c_str());
            SDL_FreeSurface(surf);
            std::cout << "    -> " << fname << " (" << TILE_PX << "x" << TILE_PX << " px)\n";
        }

        std::cout << "\n[TILE-TEST] Done. " << found_count << " tile images saved.\n";

        // ── Synthetic grade comparison sheet ──────────────────────────────
        // 3 grades (Shallow/Medium/Steep) × 3 materials (rows).
        // Each cell is 5×5 tiles, ALL elevation transition tiles of the target grade.
        // Elevation is carefully set per-tile so each tile straddles exactly one
        // 0.08 contour (corners on opposite sides of the threshold).
        {
            constexpr int CELL = 5;
            constexpr int CELL_PX = CELL * PPT;
            constexpr int COLS = 3;  // Shallow, Medium, Steep
            constexpr int ROWS = 3;  // Granite, Sandstone, Basalt
            constexpr int GAP = 4;
            constexpr int LABEL_H = 14;
            constexpr int IMG_W = COLS * CELL_PX + (COLS - 1) * GAP;
            constexpr int IMG_H = LABEL_H + ROWS * (CELL_PX + LABEL_H) + (ROWS - 1) * GAP;
            constexpr int SYNTH_W = CELL + 2;
            constexpr int SYNTH_H = CELL + 2;
            constexpr float CONTOUR = 0.08f;

            struct MatSetup {
                const char* name;
                RockType rock;
                SoilTexture soil;
            };
            MatSetup mats[] = {
                {"Granite", RockType::Granite, SoilTexture::Sand},
                {"Sandstone", RockType::Sandstone, SoilTexture::Loam},
                {"Basalt", RockType::Basalt, SoilTexture::Clay},
            };

            struct GradeSetup {
                const char* name;
                float target_slope;
            };
            GradeSetup grades[] = {
                {"Shallow", 0.10f},
                {"Medium", 0.27f},
                {"Steep", 0.50f},
            };

            SDL_Surface* sheet = SDL_CreateRGBSurface(0, IMG_W, IMG_H, 32,
                0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);
            SDL_FillRect(sheet, nullptr, SDL_MapRGBA(sheet->format, 40, 40, 40, 255));

            for (int row = 0; row < ROWS; ++row) {
                for (int col_idx = 0; col_idx < COLS; ++col_idx) {
                    Terrain synth;
                    synth.width = SYNTH_W;
                    synth.height = SYNTH_H;
                    synth.tiles.resize(static_cast<size_t>(SYNTH_W * SYNTH_H));

                    float slope_val = grades[col_idx].target_slope;

                    // All tiles at nearly the same elevation so darkening/color is uniform.
                    // Continuous downhill ramp: high at top (y=0), low at bottom.
                    // The REAL elevation drop per tile is always 0.06 (< contour
                    // spacing 0.08), ensuring each tile crosses at most one contour
                    // and doesn't clamp. The slope01 is set independently to control
                    // grade classification.
                    constexpr float REAL_DROP = 0.12f;  // per tile, visual ramp (< contour spacing 0.16)
                    float top_elev = 0.90f;
                    for (int sy = 0; sy < SYNTH_H; ++sy) {
                        float elev = top_elev - REAL_DROP * static_cast<float>(sy);
                        elev = std::clamp(elev, wl + 0.02f, 0.98f);
                        for (int sx = 0; sx < SYNTH_W; ++sx) {
                            auto& st = synth.tiles[static_cast<size_t>(sy * SYNTH_W + sx)];
                            st.elev01 = elev;
                            st.is_ocean = false;
                            st.rock = mats[row].rock;
                            st.soil = mats[row].soil;
                            st.soil_depth = 0.8f;
                            st.soil_blend = 0.5f;
                            st.bedrock_hardness = 0.5f;
                            st.dist_ocean = 10.0f;
                            st.roughness = 0.05f;
                            st.slope01 = slope_val;
                            st.aspect = 3.14159f * 0.5f;
                        }
                    }

                    // Render
                    int ox = col_idx * (CELL_PX + GAP);
                    int oy = LABEL_H + row * (CELL_PX + LABEL_H + GAP);

                    SDL_LockSurface(sheet);
                    auto* dst = static_cast<uint32_t*>(sheet->pixels);
                    int pitch = sheet->pitch / 4;

                    int n_pure = 0, n_elev = 0;
                    int n_shallow = 0, n_medium = 0, n_steep = 0;
                    for (int ty = 0; ty < CELL; ++ty) {
                        for (int tx = 0; tx < CELL; ++tx) {
                            auto cls = classify_tile(synth, tx + 1, ty + 1, wl);
                            if (cls.family == TileFamily::Elevation) {
                                ++n_elev;
                                if (cls.grade == TransitionGrade::Shallow) ++n_shallow;
                                else if (cls.grade == TransitionGrade::Medium) ++n_medium;
                                else ++n_steep;
                            } else {
                                ++n_pure;
                            }
                            auto tile_px = generate_tile_texture(synth,
                                tx + 1, ty + 1, 42, wl);
                            for (int py = 0; py < PPT; ++py) {
                                for (int px = 0; px < PPT; ++px) {
                                    int sx_px = ox + tx * PPT + px;
                                    int sy_px = oy + ty * PPT + py;
                                    if (sx_px < IMG_W && sy_px < IMG_H)
                                        dst[sy_px * pitch + sx_px] =
                                            tile_px.pixels[py * 16 + px];
                                }
                            }
                        }
                    }
                    SDL_UnlockSurface(sheet);
                    std::cout << "  [" << mats[row].name << "/" << grades[col_idx].name
                              << "] slope=" << slope_val
                              << " pure=" << n_pure << " elev=" << n_elev
                              << " (S=" << n_shallow << " M=" << n_medium
                              << " St=" << n_steep << ")\n";
                }
            }

            std::string grade_fname;
            for (int ver = 1; ; ++ver) {
                grade_fname = "tile_grade_comparison_v" + std::to_string(ver) + ".bmp";
                std::ifstream check(grade_fname);
                if (!check.good()) break;
            }
            SDL_SaveBMP(sheet, grade_fname.c_str());
            SDL_FreeSurface(sheet);
            std::cout << "\n[GRADE-COMPARE] " << grade_fname
                      << " (" << IMG_W << "x" << IMG_H << " px)\n";
            std::cout << "  Columns: Shallow | Medium | Steep\n";
            std::cout << "  Rows: " << mats[0].name << " | "
                      << mats[1].name << " | " << mats[2].name << "\n";
        }

        // ── Full planetary map render ────────────────────────────────────
        {
            // Macro scale: 4 px/tile (full world overview)
            constexpr int MACRO_PPT = 4;
            int map_w = static_cast<int>(terrain.width) * MACRO_PPT;
            int map_h = static_cast<int>(terrain.height) * MACRO_PPT;
            float world_per_pixel = 1.0f / static_cast<float>(MACRO_PPT);

            std::vector<uint32_t> map_pixels(static_cast<size_t>(map_w * map_h));
            std::cout << "\n[MAP] Rendering macro map (" << map_w << "x" << map_h
                      << " @ " << MACRO_PPT << "px/tile)...\n";

            reset_template_atlas(cfg.seed);
            render_terrain_region(terrain, 0.0f, 0.0f, world_per_pixel,
                                  map_w, map_h, cfg.seed, map_pixels.data(), map_w,
                                  cfg.env.water_level);

            SDL_Surface* map_surf = SDL_CreateRGBSurfaceFrom(
                map_pixels.data(), map_w, map_h, 32, map_w * 4,
                0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);
            std::string map_fname = "tile_map_macro_" + std::to_string(cfg.seed) + ".bmp";
            SDL_SaveBMP(map_surf, map_fname.c_str());
            SDL_FreeSurface(map_surf);
            std::cout << "[MAP] Saved " << map_fname << "\n";

            // Meso scale: 16 px/tile (zoomed into a 32×32 tile region)
            constexpr int MESO_PPT = 16;
            constexpr int MESO_TILES = 32;
            int meso_w = MESO_TILES * MESO_PPT;
            int meso_h = MESO_TILES * MESO_PPT;
            // Pick a region with both land and coast
            int cx = static_cast<int>(terrain.width) / 2;
            int cy = static_cast<int>(terrain.height) / 2;
            for (int ty = 0; ty < static_cast<int>(terrain.height); ++ty) {
                for (int tx = 0; tx < static_cast<int>(terrain.width); ++tx) {
                    const auto& t = terrain.tile_at(
                        static_cast<uint32_t>(tx), static_cast<uint32_t>(ty));
                    if (t.dist_ocean > 0.5f && t.dist_ocean < 6.0f && !t.is_ocean) {
                        cx = tx; cy = ty;
                        goto found_coast;
                    }
                }
            }
            found_coast:
            int rx = std::max(0, cx - MESO_TILES / 2);
            int ry = std::max(0, cy - MESO_TILES / 2);
            rx = std::min(rx, static_cast<int>(terrain.width) - MESO_TILES);
            ry = std::min(ry, static_cast<int>(terrain.height) - MESO_TILES);

            std::vector<uint32_t> meso_pixels(static_cast<size_t>(meso_w * meso_h));
            float meso_wpp = 1.0f / static_cast<float>(MESO_PPT);
            std::cout << "[MAP] Rendering meso map (" << meso_w << "x" << meso_h
                      << " @ " << MESO_PPT << "px/tile, region " << rx << "," << ry << ")...\n";

            render_terrain_region(terrain, static_cast<float>(rx), static_cast<float>(ry),
                                  meso_wpp, meso_w, meso_h, cfg.seed, meso_pixels.data(),
                                  meso_w, cfg.env.water_level);

            SDL_Surface* meso_surf = SDL_CreateRGBSurfaceFrom(
                meso_pixels.data(), meso_w, meso_h, 32, meso_w * 4,
                0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);
            std::string meso_fname = "tile_map_meso_" + std::to_string(cfg.seed) + ".bmp";
            SDL_SaveBMP(meso_surf, meso_fname.c_str());
            SDL_FreeSurface(meso_surf);
            std::cout << "[MAP] Saved " << meso_fname << "\n";
        }

        SDL_Quit();
        return 0;
    }

    // SDL2 init (non-headless path)
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return 1;
    }

    constexpr int INITIAL_WIN_W = 1280;
    constexpr int INITIAL_WIN_H = 720;

    SDL_Window* window = SDL_CreateWindow(
        "Darwin Chronicles — TerrainGen Sandbox", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        INITIAL_WIN_W, INITIAL_WIN_H, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window) {
        std::cerr << "Window creation failed: " << SDL_GetError() << "\n";
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* sdl_renderer =
        SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!sdl_renderer) {
        std::cerr << "Renderer creation failed: " << SDL_GetError() << "\n";
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Init renderer
    Renderer tile_renderer;
    tile_renderer.init(sdl_renderer);

    // Bake terrain texture cache (3 LOD levels with loading screen)
    tile_renderer.bake_terrain_cache(terrain, cfg.seed, cfg.env.water_level);

    // Camera — start fully zoomed out to show entire world
    Camera cam;
    cam.fit_world(cfg.world_width, cfg.world_height, Renderer::TILE_SIZE, INITIAL_WIN_W,
                  INITIAL_WIN_H);

    // State
    OverlayMode overlay = OverlayMode::None;
    bool show_grid = false;
    bool show_fps = false;
    auto active_tweak = TweakParam::Seed;
    bool needs_regen = false;
    FPSCounter fps_counter;

    std::mt19937 rng(static_cast<unsigned>(std::time(nullptr)));

    // UI button state
    ButtonRect regen_btn = {0, 0, 0, 0};

    // Key state for continuous panning
    bool key_up = false, key_down = false, key_left = false, key_right = false;
    uint32_t last_tick = SDL_GetTicks();

    // Event loop
    bool running = true;
    while (running && !g_quit) {
        uint32_t now = SDL_GetTicks();
        float dt = static_cast<float>(now - last_tick) / 1000.0f;
        dt = std::min(dt, 0.1f);  // Cap delta time
        last_tick = now;

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT:
                    running = false;
                    break;

                case SDL_WINDOWEVENT:
                    if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
                        running = false;
                    }
                    break;

                case SDL_MOUSEBUTTONDOWN: {
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        int mx = event.button.x;
                        int my = event.button.y;
                        if (regen_btn.contains(mx, my)) {
                            cfg.seed = static_cast<uint32_t>(rng());
                            std::cout << "[REGEN] Random seed: " << cfg.seed << "\n";
                            needs_regen = true;
                        }
                    }
                    break;
                }

                case SDL_MOUSEWHEEL: {
                    int mx, my;
                    SDL_GetMouseState(&mx, &my);
                    float factor = (event.wheel.y > 0) ? 1.15f : (1.0f / 1.15f);
                    int w, h;
                    SDL_GetWindowSize(window, &w, &h);
                    cam.zoom_at(static_cast<float>(mx), static_cast<float>(my), factor, w, h);
                    break;
                }

                case SDL_KEYDOWN: {
                    auto key = event.key.keysym.sym;
                    bool shift = (event.key.keysym.mod & KMOD_SHIFT) != 0;
                    bool ctrl = (event.key.keysym.mod & KMOD_CTRL) != 0;

                    switch (key) {
                        case SDLK_ESCAPE:
                            running = false;
                            break;

                        // Pan keys
                        case SDLK_w:
                        case SDLK_UP:
                            key_up = true;
                            break;
                        case SDLK_s:
                        case SDLK_DOWN:
                            key_down = true;
                            break;
                        case SDLK_a:
                        case SDLK_LEFT:
                            key_left = true;
                            break;
                        case SDLK_d:
                        case SDLK_RIGHT:
                            key_right = true;
                            break;

                        // Overlays: 1-9
                        case SDLK_1:
                            overlay = OverlayMode::None;
                            break;
                        case SDLK_2:
                            overlay = OverlayMode::Continental;
                            break;
                        case SDLK_3:
                            overlay = OverlayMode::Slope;
                            break;
                        case SDLK_4:
                            overlay = OverlayMode::ElevBand;
                            break;
                        case SDLK_5:
                            overlay = OverlayMode::DistOcean;
                            break;
                        case SDLK_6:
                            overlay = OverlayMode::Roughness;
                            break;
                        case SDLK_7:
                            overlay = OverlayMode::Aspect;
                            break;
                        case SDLK_8:
                            overlay = OverlayMode::Geology;
                            break;
                        case SDLK_9:
                            overlay = OverlayMode::SoilTexture;
                            break;

                        // Grid
                        case SDLK_g:
                            show_grid = !show_grid;
                            break;

                        // FPS
                        case SDLK_f:
                            show_fps = !show_fps;
                            break;

                        // Tweak: Tab cycles, [/] adjusts
                        case SDLK_TAB: {
                            int next = (static_cast<int>(active_tweak) + 1) %
                                       static_cast<int>(TweakParam::COUNT);
                            active_tweak = static_cast<TweakParam>(next);
                            std::cout << "[TWEAK] Active param: " << tweak_name(active_tweak)
                                      << " = " << tweak_value(active_tweak, cfg) << "\n";
                            break;
                        }
                        case SDLK_RIGHTBRACKET: {
                            float step = tweak_step(active_tweak) * (shift ? 10.0f : 1.0f);
                            tweak_apply(active_tweak, cfg, step);
                            needs_regen = true;
                            break;
                        }
                        case SDLK_LEFTBRACKET: {
                            float step = tweak_step(active_tweak) * (shift ? 10.0f : 1.0f);
                            tweak_apply(active_tweak, cfg, -step);
                            needs_regen = true;
                            break;
                        }

                        // Regenerate
                        case SDLK_r:
                            needs_regen = true;
                            break;
                        case SDLK_SPACE: {
                            cfg.seed = static_cast<uint32_t>(rng());
                            std::cout << "[REGEN] Random seed: " << cfg.seed << "\n";
                            needs_regen = true;
                            break;
                        }

                        // Export
                        case SDLK_F12: {
                            int w, h;
                            SDL_GetWindowSize(window, &w, &h);
                            std::string fname =
                                ctrl ? "terraingen_full_" + std::to_string(cfg.seed) + ".bmp"
                                     : "terraingen_" + std::to_string(cfg.seed) + ".bmp";
                            take_screenshot(sdl_renderer, w, h, fname);
                            break;
                        }
                        case SDLK_l:
                            export_log(cfg, timings, stats);
                            break;

                        default:
                            break;
                    }
                    break;
                }

                case SDL_KEYUP: {
                    auto key = event.key.keysym.sym;
                    switch (key) {
                        case SDLK_w:
                        case SDLK_UP:
                            key_up = false;
                            break;
                        case SDLK_s:
                        case SDLK_DOWN:
                            key_down = false;
                            break;
                        case SDLK_a:
                        case SDLK_LEFT:
                            key_left = false;
                            break;
                        case SDLK_d:
                        case SDLK_RIGHT:
                            key_right = false;
                            break;
                        default:
                            break;
                    }
                    break;
                }
            }
        }

        // Continuous panning
        float dx = (key_right ? 1.0f : 0.0f) - (key_left ? 1.0f : 0.0f);
        float dy = (key_down ? 1.0f : 0.0f) - (key_up ? 1.0f : 0.0f);
        if (dx != 0.0f || dy != 0.0f) {
            cam.pan(dx, dy, dt);
        }

        // Regenerate if needed
        if (needs_regen) {
            terrain =
                generate_terrain(cfg.world_width, cfg.world_height, cfg.env, cfg.seed, &timings);
            stats = compute_stats(terrain);
            tile_renderer.invalidate_terrain_cache();
            tile_renderer.bake_terrain_cache(terrain, cfg.seed, cfg.env.water_level);
            std::cout << "[REGEN] seed=" << cfg.seed << " total=" << std::fixed
                      << std::setprecision(1) << timings.total_ms << "ms\n";
            needs_regen = false;
        }

        // Update window title
        {
            int mx, my;
            SDL_GetMouseState(&mx, &my);
            int win_w, win_h;
            SDL_GetWindowSize(window, &win_w, &win_h);

            int tile_x, tile_y;
            cam.screen_to_tile(mx, my, win_w, win_h, Renderer::TILE_SIZE, tile_x, tile_y);

            std::ostringstream title;
            title << "TerrainGen | " << cfg.world_width << "x" << cfg.world_height
                  << " seed=" << cfg.seed;

            if (tile_x >= 0 && tile_y >= 0 && tile_x < static_cast<int>(terrain.width) &&
                tile_y < static_cast<int>(terrain.height)) {
                const auto& t =
                    terrain.tile_at(static_cast<uint32_t>(tile_x), static_cast<uint32_t>(tile_y));
                title << " | (" << tile_x << "," << tile_y << ") " << elevband_name(t.band)
                      << " h=" << std::fixed << std::setprecision(2) << t.elev01
                      << " s=" << std::setprecision(2) << t.slope01
                      << " do=" << std::setprecision(0) << t.dist_ocean
                      << " erod=" << std::setprecision(2) << t.erodibility
                      << " soil_d=" << std::setprecision(2) << t.soil_depth;
            }

            if (overlay != OverlayMode::None) {
                title << " | Overlay: " << overlay_name(overlay);
            }

            title << " | Tweak: " << tweak_name(active_tweak) << "=" << std::setprecision(3)
                  << tweak_value(active_tweak, cfg);

            if (show_fps) {
                title << " | FPS: " << std::setprecision(0) << fps_counter.fps();
            }

            SDL_SetWindowTitle(window, title.str().c_str());
        }

        // Render
        int win_w, win_h;
        SDL_GetWindowSize(window, &win_w, &win_h);

        SDL_SetRenderDrawColor(sdl_renderer, 20, 20, 30, 255);
        SDL_RenderClear(sdl_renderer);

        tile_renderer.render_terrain(terrain, cam, win_w, win_h);

        render_overlay(sdl_renderer, terrain, stats, cam, win_w, win_h, overlay,
                       Renderer::TILE_SIZE);

        if (show_grid) {
            render_grid(sdl_renderer, terrain, cam, win_w, win_h, Renderer::TILE_SIZE);
        }

        render_legend(sdl_renderer, stats, win_w, win_h);
        render_overlay_legend(sdl_renderer, stats, overlay, win_w, win_h);

        // Regenerate button (top-right)
        {
            int mx, my;
            SDL_GetMouseState(&mx, &my);
            bool hovered = regen_btn.w > 0 && regen_btn.contains(mx, my);
            constexpr int BTN_MARGIN = 10;
            // Position from right edge; first frame uses x=0, corrects on next
            int btn_x = win_w - (regen_btn.w > 0 ? regen_btn.w : 150) - BTN_MARGIN;
            regen_btn = render_button(sdl_renderer, btn_x, BTN_MARGIN, "REGENERATE", hovered);
        }

        SDL_RenderPresent(sdl_renderer);
        fps_counter.tick();
    }

    // Cleanup
    tile_renderer.shutdown();
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
