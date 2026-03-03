#include "config.h"
#include "renderer.h"
#include "telemetry.h"
#include "world_gen.h"

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

using namespace sandbox;

// ── Tweak parameters ────────────────────────────────────────────────────────

enum class TweakParam {
    Seed,
    Humidity,
    Toxicity,
    NutrientRichness,
    MeanTemp,
    NoiseScale,
    WaterLevel,
    WindDirection,

    COUNT
};

static const char* tweak_name(TweakParam p) {
    switch (p) {
        case TweakParam::Seed: return "Seed";
        case TweakParam::Humidity: return "Humidity";
        case TweakParam::Toxicity: return "Toxicity";
        case TweakParam::NutrientRichness: return "NutrientRichness";
        case TweakParam::MeanTemp: return "MeanTemp";
        case TweakParam::NoiseScale: return "NoiseScale";
        case TweakParam::WaterLevel: return "WaterLevel";
        case TweakParam::WindDirection: return "WindDirection";
        default: return "Unknown";
    }
}

static float tweak_value(TweakParam p, const SandboxConfig& cfg) {
    switch (p) {
        case TweakParam::Seed: return static_cast<float>(cfg.seed);
        case TweakParam::Humidity: return cfg.env.humidity;
        case TweakParam::Toxicity: return cfg.env.toxicity_level;
        case TweakParam::NutrientRichness: return cfg.env.nutrient_richness;
        case TweakParam::MeanTemp: return cfg.climate.mean_temp_K;
        case TweakParam::NoiseScale: return cfg.env.noise_scale;
        case TweakParam::WaterLevel: return cfg.env.water_level;
        case TweakParam::WindDirection: return cfg.env.wind_direction_deg;
        default: return 0.0f;
    }
}

static float tweak_step(TweakParam p) {
    switch (p) {
        case TweakParam::Seed: return 1.0f;
        case TweakParam::Humidity: return 0.05f;
        case TweakParam::Toxicity: return 0.05f;
        case TweakParam::NutrientRichness: return 0.1f;
        case TweakParam::MeanTemp: return 5.0f;
        case TweakParam::NoiseScale: return 0.005f;
        case TweakParam::WaterLevel: return 0.05f;
        case TweakParam::WindDirection: return 15.0f;
        default: return 0.01f;
    }
}

static void tweak_apply(TweakParam p, SandboxConfig& cfg, float delta) {
    float old_val = tweak_value(p, cfg);
    switch (p) {
        case TweakParam::Seed:
            cfg.seed = static_cast<uint32_t>(
                std::max(0.0f, static_cast<float>(cfg.seed) + delta));
            break;
        case TweakParam::Humidity:
            cfg.env.humidity = std::clamp(cfg.env.humidity + delta, 0.0f, 1.0f);
            break;
        case TweakParam::Toxicity:
            cfg.env.toxicity_level = std::clamp(cfg.env.toxicity_level + delta, 0.0f, 1.0f);
            break;
        case TweakParam::NutrientRichness:
            cfg.env.nutrient_richness = std::clamp(cfg.env.nutrient_richness + delta, 0.0f, 2.0f);
            break;
        case TweakParam::MeanTemp:
            cfg.climate.mean_temp_K = std::clamp(cfg.climate.mean_temp_K + delta, 100.0f, 800.0f);
            break;
        case TweakParam::NoiseScale:
            cfg.env.noise_scale = std::clamp(cfg.env.noise_scale + delta, 0.001f, 0.1f);
            break;
        case TweakParam::WaterLevel:
            cfg.env.water_level = std::clamp(cfg.env.water_level + delta, 0.0f, 0.8f);
            break;
        case TweakParam::WindDirection:
            cfg.env.wind_direction_deg = std::fmod(cfg.env.wind_direction_deg + delta + 360.0f, 360.0f);
            break;
        default: break;
    }
    float new_val = tweak_value(p, cfg);
    std::cout << "[TWEAK] " << tweak_name(p) << ": " << std::fixed << std::setprecision(3)
              << old_val << " -> " << new_val << "\n";
}

// ── Log export ──────────────────────────────────────────────────────────────

static void export_log(const SandboxConfig& cfg, const GenerationTimings& timings,
                       const WorldStats& stats) {
    std::string filename = "worldgen_log_" + std::to_string(cfg.seed) + ".txt";
    std::ofstream out(filename);
    if (!out) {
        std::cerr << "Failed to write " << filename << "\n";
        return;
    }

    out << "=== World Generation Log ===\n";
    out << "Preset: " << cfg.preset << "\n";
    out << "Seed: " << cfg.seed << "\n";
    out << "Size: " << cfg.world_width << "x" << cfg.world_height << "\n\n";

    out << "Parameters:\n";
    out << "  Mean Temp (K):     " << cfg.climate.mean_temp_K << "\n";
    out << "  Humidity:          " << cfg.env.humidity << "\n";
    out << "  Toxicity:          " << cfg.env.toxicity_level << "\n";
    out << "  Nutrient Richness: " << cfg.env.nutrient_richness << "\n";
    out << "  Water Level:       " << cfg.env.water_level << "\n";
    out << "  Noise Scale:       " << cfg.env.noise_scale << "\n\n";

    out << "Generation Timings:\n";
    out << "  Height:         " << timings.height_ms << " ms\n";
    out << "  Temperature:    " << timings.temperature_ms << " ms\n";
    out << "  Moisture:       " << timings.moisture_ms << " ms\n";
    out << "  Toxicity:       " << timings.toxicity_ms << " ms\n";
    out << "  Classification: " << timings.classification_ms << " ms\n";
    out << "  Total:          " << timings.total_ms << " ms\n\n";

    out << "Biome Distribution:\n";
    for (size_t i = 0; i < static_cast<size_t>(Biome::COUNT); ++i) {
        if (stats.biome_counts[i] == 0) continue;
        float pct = 100.0f * static_cast<float>(stats.biome_counts[i]) /
                    static_cast<float>(stats.total_tiles);
        out << "  " << std::left << std::setw(18) << biome_name(static_cast<Biome>(i))
            << stats.biome_counts[i] << " (" << std::fixed << std::setprecision(1) << pct
            << "%)\n";
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

static void signal_handler(int /*sig*/) { g_quit = 1; }

// ── Main ────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    SandboxConfig cfg = parse_args(argc, argv);

    std::cout << "Darwin Chronicles — World Generation Sandbox\n";
    std::cout << "Preset: " << cfg.preset << " | Seed: " << cfg.seed << " | Size: "
              << cfg.world_width << "x" << cfg.world_height << "\n";
    std::cout << "Climate: mean_temp=" << cfg.climate.mean_temp_K
              << "K, habitability=" << cfg.climate.habitability_factor << "\n";
    std::cout << "Env: humidity=" << cfg.env.humidity << ", toxicity=" << cfg.env.toxicity_level
              << ", nutrients=" << cfg.env.nutrient_richness << "\n\n";

    // Generate world
    GenerationTimings timings;
    World world = generate_world(cfg.world_width, cfg.world_height, cfg.env, cfg.climate, cfg.seed,
                                 &timings);

    std::cout << "Generation timings:\n";
    std::cout << "  Height:         " << std::fixed << std::setprecision(1) << timings.height_ms
              << " ms\n";
    std::cout << "  Temperature:    " << timings.temperature_ms << " ms\n";
    std::cout << "  Moisture:       " << timings.moisture_ms << " ms\n";
    std::cout << "  Toxicity:       " << timings.toxicity_ms << " ms\n";
    std::cout << "  Classification: " << timings.classification_ms << " ms\n";
    std::cout << "  Total:          " << timings.total_ms << " ms\n";

    WorldStats stats = compute_stats(world);
    print_stats(stats);
    std::cout << std::flush;

    // SDL2 init
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return 1;
    }

    constexpr int INITIAL_WIN_W = 1280;
    constexpr int INITIAL_WIN_H = 720;

    SDL_Window* window = SDL_CreateWindow("Darwin Chronicles — WorldGen Sandbox",
                                          SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          INITIAL_WIN_W, INITIAL_WIN_H,
                                          SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
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

    // Init renderer (biome color mode)
    Renderer tile_renderer;
    tile_renderer.init(sdl_renderer);

    // Camera
    Camera cam;
    cam.center_on_world(cfg.world_width, cfg.world_height, Renderer::TILE_SIZE);

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
                case SDL_QUIT: running = false; break;

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
                            cfg.seed = rng();
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
                        case SDLK_ESCAPE: running = false; break;

                        // Pan keys
                        case SDLK_w:
                        case SDLK_UP: key_up = true; break;
                        case SDLK_s:
                        case SDLK_DOWN: key_down = true; break;
                        case SDLK_a:
                        case SDLK_LEFT: key_left = true; break;
                        case SDLK_d:
                        case SDLK_RIGHT: key_right = true; break;

                        // Overlays: 1-6
                        case SDLK_1: overlay = OverlayMode::None; break;
                        case SDLK_2: overlay = OverlayMode::Height; break;
                        case SDLK_3: overlay = OverlayMode::Temperature; break;
                        case SDLK_4: overlay = OverlayMode::Moisture; break;
                        case SDLK_5: overlay = OverlayMode::Toxicity; break;
                        case SDLK_6: overlay = OverlayMode::BiomeColor; break;
                        case SDLK_7: overlay = OverlayMode::EffectiveMoisture; break;

                        // Grid
                        case SDLK_g: show_grid = !show_grid; break;

                        // FPS
                        case SDLK_f: show_fps = !show_fps; break;

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
                        case SDLK_r: needs_regen = true; break;
                        case SDLK_SPACE: {
                            cfg.seed = rng();
                            std::cout << "[REGEN] Random seed: " << cfg.seed << "\n";
                            needs_regen = true;
                            break;
                        }

                        // Export
                        case SDLK_F12: {
                            int w, h;
                            SDL_GetWindowSize(window, &w, &h);
                            std::string fname =
                                ctrl ? "worldgen_full_" + std::to_string(cfg.seed) + ".bmp"
                                     : "worldgen_" + std::to_string(cfg.seed) + ".bmp";
                            take_screenshot(sdl_renderer, w, h, fname);
                            break;
                        }
                        case SDLK_l: export_log(cfg, timings, stats); break;

                        default: break;
                    }
                    break;
                }

                case SDL_KEYUP: {
                    auto key = event.key.keysym.sym;
                    switch (key) {
                        case SDLK_w:
                        case SDLK_UP: key_up = false; break;
                        case SDLK_s:
                        case SDLK_DOWN: key_down = false; break;
                        case SDLK_a:
                        case SDLK_LEFT: key_left = false; break;
                        case SDLK_d:
                        case SDLK_RIGHT: key_right = false; break;
                        default: break;
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
            world = generate_world(cfg.world_width, cfg.world_height, cfg.env, cfg.climate,
                                   cfg.seed, &timings);
            stats = compute_stats(world);
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
            title << "WorldGen | " << cfg.world_width << "x" << cfg.world_height
                  << " seed=" << cfg.seed;

            if (tile_x >= 0 && tile_y >= 0 && tile_x < static_cast<int>(world.width) &&
                tile_y < static_cast<int>(world.height)) {
                const auto& t = world.tile_at(static_cast<uint32_t>(tile_x),
                                              static_cast<uint32_t>(tile_y));
                title << " | (" << tile_x << "," << tile_y << ") "
                      << biome_name(t.biome) << " h=" << std::fixed << std::setprecision(2)
                      << t.height << " t=" << std::setprecision(0) << t.temperature_K
                      << "K m=" << std::setprecision(2) << t.moisture
                      << " em=" << std::setprecision(2) << t.effective_moisture;
            }

            if (overlay != OverlayMode::None) {
                title << " | Overlay: " << overlay_name(overlay);
            }

            title << " | Tweak: " << tweak_name(active_tweak) << "="
                  << std::setprecision(3) << tweak_value(active_tweak, cfg);

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

        tile_renderer.render_world(world, cam, win_w, win_h);

        render_overlay(sdl_renderer, world, cam, win_w, win_h, overlay, Renderer::TILE_SIZE);

        if (show_grid) {
            render_grid(sdl_renderer, world, cam, win_w, win_h, Renderer::TILE_SIZE);
        }

        render_legend(sdl_renderer, stats, win_w, win_h);

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
