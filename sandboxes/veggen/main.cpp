#include <SDL.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>

#include "metrics.h"
#include "renderer.h"
#include "telemetry.h"
#include "vegetation.h"

using namespace veggen;

static volatile sig_atomic_t g_quit = 0;

static void signal_handler(int /*sig*/) {
    g_quit = 1;
}

// Auto-screenshot mode: render each biome at day 60 (established growth) and save BMP
static int run_auto_screenshots(uint32_t seed) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return 1;
    }

    constexpr int WIN_W = 800;
    constexpr int WIN_H = 700;
    SDL_Window* window = SDL_CreateWindow("VegGen Auto", SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED, WIN_W, WIN_H, SDL_WINDOW_SHOWN);
    SDL_Renderer* sdl_renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!sdl_renderer) {
        std::cerr << "Renderer creation failed\n";
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    Renderer tile_renderer;
    tile_renderer.init(sdl_renderer);

    // Snapshot days: Spring(90), Summer(180), Autumn(270), Winter(350)
    constexpr float SNAPSHOT_DAYS[] = {90.0f, 180.0f, 270.0f, 350.0f};
    constexpr const char* SNAPSHOT_LABELS[] = {"spring", "summer", "autumn", "winter"};
    constexpr int NUM_SNAPSHOTS = 4;

    for (int preset = 0; preset < NUM_PRESETS; ++preset) {
        SimState sim;
        sim.init(preset, seed);
        sim.time_scale = 8.0f;

        for (int snap = 0; snap < NUM_SNAPSHOTS; ++snap) {
            while (sim.elapsed_days < SNAPSHOT_DAYS[snap]) {
                sim.update(1.0f / 60.0f);
            }

            // Render
            SDL_SetRenderDrawColor(sdl_renderer, 30, 30, 40, 255);
            SDL_RenderClear(sdl_renderer);
            tile_renderer.render_tiles(sim, WIN_W, WIN_H);
            tile_renderer.render_plants(sim, WIN_W, WIN_H);
            tile_renderer.render_ui(sim, WIN_W, WIN_H, OverlayMode::None, false, 0);
            SDL_RenderPresent(sdl_renderer);

            // Save screenshot
            SDL_Surface* sshot =
                SDL_CreateRGBSurfaceWithFormat(0, WIN_W, WIN_H, 32, SDL_PIXELFORMAT_RGBA32);
            SDL_RenderReadPixels(sdl_renderer, nullptr, SDL_PIXELFORMAT_RGBA32, sshot->pixels,
                                 sshot->pitch);
            char fname[128];
            std::snprintf(fname, sizeof(fname), "veggen_%02d_%s_%s.bmp", preset,
                          BIOME_PRESETS[preset].name, SNAPSHOT_LABELS[snap]);
            for (char* p = fname; *p; ++p)
                if (*p == ' ')
                    *p = '_';
            SDL_SaveBMP(sshot, fname);
            SDL_FreeSurface(sshot);
            std::printf("Saved: %s (day %.0f, %d plants)\n", fname,
                        static_cast<double>(sim.elapsed_days), static_cast<int>(sim.plants.size()));
        }
    }

    tile_renderer.shutdown();
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

// Metrics mode: run all biomes for N years headless, export CSV + summary
static int run_metrics(uint32_t seed, float years) {
    // No SDL needed for headless metrics
    std::printf("Running metrics collection: seed=%u, %.0f years per biome\n", seed,
                static_cast<double>(years));

    float total_days = years * veggen::DAYS_PER_YEAR;
    constexpr float DT = 1.0f / 60.0f;  // simulate at 60fps

    for (int preset = 0; preset < veggen::NUM_PRESETS; ++preset) {
        veggen::SimState sim;
        veggen::MetricsCollector collector;
        collector.snapshot_interval = 30.0f;  // snapshot every 30 sim-days

        sim.metrics = &collector;
        sim.init(preset, seed);
        sim.time_scale = 1.0f;

        while (sim.elapsed_days < total_days) {
            sim.update(DT);
        }

        const char* biome_name = veggen::BIOME_PRESETS[preset].name;
        collector.print_summary(biome_name);

        // Export CSV
        char filename[256];
        // Sanitize biome name for filename
        std::snprintf(filename, sizeof(filename), "metrics_%02d_%s.csv", preset, biome_name);
        for (char* p = filename; *p; ++p) {
            if (*p == ' ')
                *p = '_';
        }
        collector.export_csv(filename);
    }

    std::printf("\nDone. CSV files written for each biome.\n");
    return 0;
}

int main(int argc, char* argv[]) {
    // Auto-screenshot mode: sandbox_veggen --auto [seed]
    if (argc >= 2 && std::strcmp(argv[1], "--auto") == 0) {
        uint32_t seed = (argc >= 3) ? static_cast<uint32_t>(std::strtoul(argv[2], nullptr, 10))
                                    : static_cast<uint32_t>(std::time(nullptr));
        return run_auto_screenshots(seed);
    }

    // Metrics mode: sandbox_veggen --metrics [seed] [years]
    if (argc >= 2 && std::strcmp(argv[1], "--metrics") == 0) {
        uint32_t seed =
            (argc >= 3) ? static_cast<uint32_t>(std::strtoul(argv[2], nullptr, 10)) : 42;
        float years = (argc >= 4) ? static_cast<float>(std::atof(argv[3])) : 10.0f;
        return run_metrics(seed, years);
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "Darwin Chronicles - Vegetation Generation Sandbox\n";
    std::cout << "Controls:\n";
    std::cout << "  Left/Right  Cycle biome preset\n";
    std::cout << "  Space       Restart growth\n";
    std::cout << "  +/-         Adjust speed (0.5x..8x)\n";
    std::cout << "  P           Pause/unpause\n";
    std::cout << "  1-7         Overlay (None/Moisture/Temp/Health/Elev/Water/Canopy)\n";
    std::cout << "  G           Toggle grid\n";
    std::cout << "  F           Toggle FPS\n";
    std::cout << "  S           Screenshot\n";
    std::cout << "  ESC         Quit\n\n";

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return 1;
    }

    constexpr int INITIAL_WIN_W = 800;
    constexpr int INITIAL_WIN_H = 700;

    SDL_Window* window =
        SDL_CreateWindow("VegGen Sandbox", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
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

    // Init
    Renderer tile_renderer;
    tile_renderer.init(sdl_renderer);

    MetricsCollector collector;
    collector.snapshot_interval = 30.0f;

    SimState sim;
    sim.metrics = &collector;
    sim.init(0, static_cast<uint32_t>(std::time(nullptr)));

    OverlayMode overlay = OverlayMode::None;
    bool show_grid = true;
    bool show_fps = false;
    FPSCounter fps_counter;

    uint32_t last_tick = SDL_GetTicks();

    // Speed steps
    constexpr float SPEED_STEPS[] = {0.5f, 1.0f, 2.0f, 4.0f, 8.0f};
    constexpr int NUM_SPEED_STEPS = 5;
    int speed_index = 1;  // start at 1x

    bool running = true;
    while (running && !g_quit) {
        uint32_t now = SDL_GetTicks();
        float dt = static_cast<float>(now - last_tick) / 1000.0f;
        dt = std::min(dt, 0.1f);
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

                case SDL_KEYDOWN: {
                    auto key = event.key.keysym.sym;
                    switch (key) {
                        case SDLK_ESCAPE:
                            running = false;
                            break;

                        case SDLK_LEFT: {
                            int idx = (sim.preset_index - 1 + NUM_PRESETS) % NUM_PRESETS;
                            collector.reset();
                            sim.init(idx, static_cast<uint32_t>(std::time(nullptr)));
                            tile_renderer.flush_cache();
                            std::cout << "[BIOME] " << BIOME_PRESETS[idx].name << "\n";
                            break;
                        }
                        case SDLK_RIGHT: {
                            int idx = (sim.preset_index + 1) % NUM_PRESETS;
                            collector.reset();
                            sim.init(idx, static_cast<uint32_t>(std::time(nullptr)));
                            tile_renderer.flush_cache();
                            std::cout << "[BIOME] " << BIOME_PRESETS[idx].name << "\n";
                            break;
                        }

                        case SDLK_SPACE:
                            sim.reset_plants();
                            sim.elapsed_days = 0;
                            tile_renderer.flush_cache();
                            std::cout << "[RESET] Plants cleared and respawned\n";
                            break;

                        case SDLK_EQUALS:
                        case SDLK_PLUS:
                        case SDLK_KP_PLUS:
                            if (speed_index < NUM_SPEED_STEPS - 1) {
                                ++speed_index;
                                sim.time_scale = SPEED_STEPS[speed_index];
                                std::cout << "[SPEED] " << sim.time_scale << "x\n";
                            }
                            break;

                        case SDLK_MINUS:
                        case SDLK_KP_MINUS:
                            if (speed_index > 0) {
                                --speed_index;
                                sim.time_scale = SPEED_STEPS[speed_index];
                                std::cout << "[SPEED] " << sim.time_scale << "x\n";
                            }
                            break;

                        case SDLK_p:
                            sim.paused = !sim.paused;
                            std::cout << (sim.paused ? "[PAUSED]" : "[UNPAUSED]") << "\n";
                            break;

                        case SDLK_1:
                            overlay = OverlayMode::None;
                            break;
                        case SDLK_2:
                            overlay = OverlayMode::Moisture;
                            break;
                        case SDLK_3:
                            overlay = OverlayMode::Temperature;
                            break;
                        case SDLK_4:
                            overlay = OverlayMode::Health;
                            break;
                        case SDLK_5:
                            overlay = OverlayMode::Elevation;
                            break;
                        case SDLK_6:
                            overlay = OverlayMode::SurfaceWater;
                            break;
                        case SDLK_7:
                            overlay = OverlayMode::Canopy;
                            break;

                        case SDLK_g:
                            show_grid = !show_grid;
                            break;
                        case SDLK_f:
                            show_fps = !show_fps;
                            break;

                        case SDLK_s: {
                            // Screenshot
                            int sw, sh;
                            SDL_GetWindowSize(window, &sw, &sh);
                            SDL_Surface* sshot = SDL_CreateRGBSurfaceWithFormat(
                                0, sw, sh, 32, SDL_PIXELFORMAT_RGBA32);
                            SDL_RenderReadPixels(sdl_renderer, nullptr, SDL_PIXELFORMAT_RGBA32,
                                                 sshot->pixels, sshot->pitch);
                            static int screenshot_num = 0;
                            char fname[128];
                            std::snprintf(fname, sizeof(fname), "veggen_screenshot_%02d.bmp",
                                          screenshot_num++);
                            SDL_SaveBMP(sshot, fname);
                            SDL_FreeSurface(sshot);
                            std::cout << "[SCREENSHOT] " << fname << "\n";
                            break;
                        }

                        default:
                            break;
                    }
                    break;
                }
            }
        }

        // Update simulation
        sim.update(dt);

        // Update window title
        {
            char title[256];
            const auto& preset = BIOME_PRESETS[sim.preset_index];
            std::snprintf(title, sizeof(title), "VegGen | %s | Day %d | %s | %.1fx", preset.name,
                          static_cast<int>(sim.elapsed_days), season_name(sim.elapsed_days),
                          static_cast<double>(sim.time_scale));
            SDL_SetWindowTitle(window, title);
        }

        // Render
        int win_w, win_h;
        SDL_GetWindowSize(window, &win_w, &win_h);

        SDL_SetRenderDrawColor(sdl_renderer, 30, 30, 40, 255);
        SDL_RenderClear(sdl_renderer);

        tile_renderer.render_tiles(sim, win_w, win_h);
        tile_renderer.render_overlay(sim, win_w, win_h, overlay);

        if (show_grid) {
            tile_renderer.render_grid(win_w, win_h);
        }

        tile_renderer.render_plants(sim, win_w, win_h);
        tile_renderer.render_ui(sim, win_w, win_h, overlay, show_fps, fps_counter.fps());

        SDL_RenderPresent(sdl_renderer);
        fps_counter.tick();
    }

    tile_renderer.shutdown();
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
