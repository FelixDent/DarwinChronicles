#include <SDL.h>

#include <csignal>
#include <cstdlib>
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
                      << " rock=" << rock_type_name(t.rock)
                      << " soil=" << soil_texture_name(t.soil);
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
