#include <SDL.h>

#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "atmosphere.h"
#include "dynamics.h"
#include "renderer.h"
#include "telemetry.h"
#include "weather.h"
#include "terrain_gen.h"

using namespace sandbox;

// ── Terrain presets ───────────────────────────────────────────────────────────

struct TerrainPreset {
    const char* name;
    EnvParams env;
    uint32_t seed;
};

static const TerrainPreset PRESETS[] = {
    {"Archipelago",
     {.water_level = 0.55f,
      .noise_scale = 0.020f,
      .continent_threshold = 0.50f,
      .ridge_strength = 0.6f},
     42},
    {"Pangaea",
     {.water_level = 0.40f,
      .noise_scale = 0.015f,
      .continent_threshold = 0.65f,
      .ridge_strength = 1.2f},
     101},
    {"Island Chain",
     {.water_level = 0.58f,
      .noise_scale = 0.025f,
      .continent_threshold = 0.45f,
      .ridge_strength = 0.8f},
     256},
    {"Continental",
     {.water_level = 0.42f,
      .noise_scale = 0.020f,
      .continent_threshold = 0.55f,
      .ridge_strength = 1.0f},
     789},
    {"Highland",
     {.water_level = 0.35f,
      .noise_scale = 0.020f,
      .continent_threshold = 0.60f,
      .ridge_strength = 1.8f},
     1337},
    {"Waterworld",
     {.water_level = 0.60f,
      .noise_scale = 0.030f,
      .continent_threshold = 0.40f,
      .ridge_strength = 0.5f},
     2024},
};

static constexpr int NUM_PRESETS = static_cast<int>(sizeof(PRESETS) / sizeof(PRESETS[0]));

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

// ── Log export ──────────────────────────────────────────────────────────────

static void export_log(const TerrainPreset& preset, const GenerationTimings& gen_timings,
                       const WeatherTimings& weather_timings, const WeatherStats& stats) {
    std::string filename = "weather_log_" + std::string(preset.name) + ".txt";
    std::ofstream out(filename);
    if (!out) {
        std::cerr << "Failed to write " << filename << "\n";
        return;
    }

    out << "=== Weather Generation Log ===\n";
    out << "Preset: " << preset.name << " | Seed: " << preset.seed << "\n\n";

    out << "Terrain Timings:\n";
    out << "  Total: " << gen_timings.total_ms << " ms\n\n";

    out << "Weather Timings:\n";
    out << "  Latitude:      " << weather_timings.latitude_ms << " ms\n";
    out << "  Temperature:   " << weather_timings.temperature_ms << " ms\n";
    out << "  Wind:          " << weather_timings.wind_ms << " ms\n";
    out << "  Precipitation: " << weather_timings.precipitation_ms << " ms\n";
    out << "  Evaporation:   " << weather_timings.evaporation_ms << " ms\n";
    out << "  Moisture:      " << weather_timings.moisture_ms << " ms\n";
    out << "  Storminess:    " << weather_timings.storminess_ms << " ms\n";
    out << "  Rain Shadow:   " << weather_timings.rain_shadow_ms << " ms\n";
    out << "  Total:         " << weather_timings.total_ms << " ms\n\n";

    out << "Statistics:\n";
    out << "  Temp:       " << stats.temp_min << " to " << stats.temp_max << " (mean "
        << stats.temp_mean << ")\n";
    out << "  Precip:     " << stats.precip_min << " to " << stats.precip_max << " (mean "
        << stats.precip_mean << ")\n";
    out << "  Moisture:   " << stats.moisture_min << " to " << stats.moisture_max << " (mean "
        << stats.moisture_mean << ")\n";
    out << "  Evap:       " << stats.evap_min << " to " << stats.evap_max << " (mean "
        << stats.evap_mean << ")\n";
    out << "  Wind max:   " << stats.wind_speed_max << "\n";
    out << "  Storm tiles: " << stats.storm_tiles << "\n";
    out << "  Rain shadow: " << stats.rain_shadow_tiles << "\n";
    out << "  Wet coast:   " << stats.wet_coast_tiles << "\n";

    out.close();
    std::cout << "[EXPORT] Log written to " << filename << "\n";
}

// ── Signal handling ─────────────────────────────────────────────────────────

static volatile sig_atomic_t g_quit = 0;

static void signal_handler(int /*sig*/) {
    g_quit = 1;
}

// ── Main ────────────────────────────────────────────────────────────────────

// ── Headless diagnostic mode ─────────────────────────────────────────────────

static int run_headless(int days, int preset_idx = 0) {
    constexpr uint32_t WORLD_W = 256;
    constexpr uint32_t WORLD_H = 128;
    constexpr float DT = 1.0f / 30.0f;  // simulate at 30fps equivalent

    preset_idx = std::clamp(preset_idx, 0, NUM_PRESETS - 1);
    const auto& preset = PRESETS[preset_idx];
    std::cout << "=== Headless Weather Diagnostic ===\n";
    std::cout << "Preset: " << preset.name << " | Sim days: " << days << "\n\n";

    GenerationTimings gen_timings;
    Terrain terrain = generate_terrain(WORLD_W, WORLD_H, preset.env, preset.seed, &gen_timings);

    WeatherTimings weather_timings;
    AtmosphereConfig atmo_cfg;
    ClimateData climate = bake_weather(terrain, atmo_cfg, &weather_timings);

    DynamicState dynamics;
    AtmosphereState atmosphere;
    init_dynamics(dynamics, terrain, climate);
    init_atmosphere(atmosphere, terrain, climate);
    dynamics.paused = false;

    std::cout << std::fixed;
    std::cout << "DAY | T_north T_equat T_south | T_min  T_mean T_max  T_sd  | "
                 "W_mean W_max  W_sd  Wdir | "
                 "Q_mean Q_sd  cloud  precip P_sd  | "
                 "Smax storm heavyR rain/cloud gale heat freeze\n";
    std::cout << std::string(155, '-') << "\n";

    // Cumulative extremes across entire run
    float cum_T_max = -1e9f, cum_T_min = 1e9f;
    uint32_t cum_heat_wave = 0, cum_deep_freeze = 0, cum_severe_storms = 0;
    uint32_t cum_heavy_rain = 0, cum_gale = 0;
    float cum_precip_peak = 0.0f, cum_storm_max = 0.0f;

    float last_report = 0.0f;
    while (dynamics.elapsed_days < static_cast<float>(days) && !g_quit) {
        tick_atmosphere(atmosphere, terrain, dynamics, DT);
        tick_dynamics(dynamics, terrain, climate, DT, &atmosphere);

        // Cheap per-tick cumulative extreme tracking (no full stats computation)
        uint32_t tick_heat = 0, tick_freeze = 0, tick_storm = 0;
        uint32_t tick_rain = 0, tick_gale = 0;
        for (const auto& c : atmosphere.cells) {
            cum_T_max = std::max(cum_T_max, c.T);
            cum_T_min = std::min(cum_T_min, c.T);
            cum_precip_peak = std::max(cum_precip_peak, c.precip_rate);
            cum_storm_max = std::max(cum_storm_max, c.storminess);
            if (c.T > 30.0f) ++tick_heat;
            if (c.T < -25.0f) ++tick_freeze;
            if (c.storminess > 0.5f) ++tick_storm;
            if (c.precip_rate > 0.15f) ++tick_rain;
            float ws = std::sqrt(c.u * c.u + c.v * c.v);
            if (ws > 4.0f) ++tick_gale;
        }
        cum_heat_wave = std::max(cum_heat_wave, tick_heat);
        cum_deep_freeze = std::max(cum_deep_freeze, tick_freeze);
        cum_severe_storms = std::max(cum_severe_storms, tick_storm);
        cum_heavy_rain = std::max(cum_heavy_rain, tick_rain);
        cum_gale = std::max(cum_gale, tick_gale);

        float report_interval = (days > 120) ? 15.0f : 2.0f;
        if (dynamics.elapsed_days - last_report >= report_interval) {
            last_report = dynamics.elapsed_days;
            auto as = compute_atmosphere_stats(atmosphere);
            (void)compute_dynamic_stats(dynamics, terrain);

            std::cout << std::setprecision(0) << std::setw(3) << dynamics.elapsed_days << " | "
                      << std::setprecision(1)
                      << std::setw(6) << as.T_band[0] << " "
                      << std::setw(6) << as.T_band[1] << " "
                      << std::setw(6) << as.T_band[2] << " | "
                      << std::setw(6) << as.T_min << " "
                      << std::setw(6) << as.T_mean << " "
                      << std::setw(6) << as.T_max << " "
                      << std::setw(5) << as.T_stddev << " | "
                      << std::setprecision(2)
                      << std::setw(5) << as.wind_mean << " "
                      << std::setw(5) << as.wind_max << " "
                      << std::setw(5) << as.wind_stddev << " "
                      << std::setw(4) << as.wind_dir_spread << " | "
                      << std::setprecision(3)
                      << std::setw(6) << as.q_mean << " "
                      << std::setw(5) << as.q_stddev << " "
                      << std::setw(6) << as.cloud_mean << " "
                      << std::setw(6) << as.precip_mean << " "
                      << std::setw(5) << as.precip_stddev << " | "
                      << std::setprecision(2) << std::setw(4) << as.storm_max << " "
                      << std::setw(5) << as.severe_storm_cells << " "
                      << std::setw(5) << as.heavy_rain_cells << " "
                      << std::setw(5) << as.raining_cells << "/"
                      << std::setw(5) << as.cloudy_cells << " "
                      << std::setw(4) << as.gale_cells << " "
                      << std::setw(4) << as.heat_wave_cells << " "
                      << std::setw(5) << as.deep_freeze_cells
                      << "\n";
        }
    }

    std::cout << "\n=== Final state ===\n";
    auto as = compute_atmosphere_stats(atmosphere);
    std::cout << "T range: " << std::setprecision(1) << as.T_min << " to " << as.T_max
              << " (mean " << as.T_mean << ")\n";
    std::cout << "T bands [N/Eq/S]: " << as.T_band[0] << " / " << as.T_band[1]
              << " / " << as.T_band[2] << "\n";
    std::cout << "Wind bands [N/Eq/S]: " << std::setprecision(2)
              << as.wind_band[0] << " / " << as.wind_band[1]
              << " / " << as.wind_band[2] << "\n";
    float T_spread = as.T_max - as.T_min;
    float band_spread = std::max({as.T_band[0], as.T_band[1], as.T_band[2]}) -
                         std::min({as.T_band[0], as.T_band[1], as.T_band[2]});
    std::cout << "T spread: " << std::setprecision(1) << T_spread
              << " (band spread: " << band_spread << ")\n";
    std::cout << "Variation — T_sd: " << std::setprecision(2) << as.T_stddev
              << "  W_sd: " << as.wind_stddev
              << "  Q_sd: " << std::setprecision(3) << as.q_stddev
              << "  P_sd: " << as.precip_stddev
              << "  Wdir_spread: " << std::setprecision(2) << as.wind_dir_spread << "\n";

    if (T_spread < 5.0f) std::cout << "WARNING: Temperature nearly uniform!\n";
    if (band_spread < 3.0f) std::cout << "WARNING: Latitude bands nearly identical!\n";
    if (as.wind_max < 0.5f) std::cout << "WARNING: Wind nearly dead!\n";
    if (as.T_stddev < 1.0f) std::cout << "WARNING: Temperature variation collapsed!\n";
    if (as.wind_stddev < 0.2f) std::cout << "WARNING: Wind speed variation collapsed!\n";
    if (as.wind_dir_spread < 0.3f) std::cout << "WARNING: Wind direction nearly uniform!\n";
    std::cout << "Peak extremes (across entire run):\n";
    std::cout << "  T range: " << std::setprecision(1) << cum_T_min << " to " << cum_T_max << "\n";
    std::cout << "  storm_max: " << std::setprecision(2) << cum_storm_max
              << "  peak_severe_storms: " << cum_severe_storms
              << "  peak_heavy_rain: " << cum_heavy_rain
              << "  peak_gales: " << cum_gale << "\n";
    std::cout << "  peak_heat_wave: " << cum_heat_wave
              << "  peak_deep_freeze: " << cum_deep_freeze
              << "  precip_peak: " << std::setprecision(3) << cum_precip_peak << "\n";
    if (cum_storm_max < 0.5f) std::cout << "WARNING: No significant storms!\n";
    if (cum_severe_storms == 0 && cum_heavy_rain == 0)
        std::cout << "WARNING: No extreme weather events!\n";

    return 0;
}

// ── Main ────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // --headless N [preset]: run N sim-days without a window and print diagnostics
    // --headless-all N: run N sim-days for ALL presets
    if (argc >= 2 && std::string(argv[1]) == "--headless-all") {
        int days = (argc >= 3) ? std::atoi(argv[2]) : 30;
        if (days <= 0) days = 30;
        for (int p = 0; p < NUM_PRESETS; ++p) {
            run_headless(days, p);
            std::cout << "\n";
        }
        return 0;
    }
    if (argc >= 2 && std::string(argv[1]) == "--headless") {
        int days = (argc >= 3) ? std::atoi(argv[2]) : 30;
        if (days <= 0) days = 30;
        int preset = (argc >= 4) ? std::atoi(argv[3]) : 0;
        return run_headless(days, preset);
    }

    constexpr uint32_t WORLD_W = 256;
    constexpr uint32_t WORLD_H = 128;

    std::cout << "Darwin Chronicles - Weather Sandbox\n";
    std::cout << "World size: " << WORLD_W << "x" << WORLD_H << " (" << NUM_PRESETS
              << " presets)\n\n";

    // SDL2 init
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return 1;
    }

    constexpr int INITIAL_WIN_W = 1280;
    constexpr int INITIAL_WIN_H = 720;

    SDL_Window* window =
        SDL_CreateWindow("Weather Sandbox", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
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

    // State
    int current_preset = 0;
    Terrain terrain;
    ClimateData climate;
    DynamicState dynamics;
    AtmosphereState atmosphere;
    DynamicStats dyn_stats;
    AtmosphereStats atmo_stats;
    GenerationTimings gen_timings;
    WeatherTimings weather_timings;
    WeatherStats weather_stats;
    bool weather_baked = false;
    float last_diag_day = 0.0f;  // last day diagnostic was printed

    Camera cam;
    OverlayMode overlay = OverlayMode::None;
    bool show_wind = true;  // Wind arrows overlaid on all climate views
    bool show_grid = false;
    bool show_fps = false;
    FPSCounter fps_counter;

    // Generate terrain + auto-bake weather
    auto bake_current_weather = [&]() {
        AtmosphereConfig atmo;
        climate = bake_weather(terrain, atmo, &weather_timings);
        weather_stats = compute_weather_stats(climate, terrain);
        weather_baked = true;

        // Initialize dynamic simulation from baked climate (auto-start)
        init_dynamics(dynamics, terrain, climate);
        init_atmosphere(atmosphere, terrain, climate);
        dynamics.paused = false;
        last_diag_day = 0.0f;
        dyn_stats = compute_dynamic_stats(dynamics, terrain);
        atmo_stats = compute_atmosphere_stats(atmosphere);

        std::cout << "[WEATHER] Bake complete: " << std::fixed << std::setprecision(1)
                  << weather_timings.total_ms << " ms\n";
        print_weather_stats(weather_stats);
    };

    auto generate_current = [&]() {
        const auto& preset = PRESETS[current_preset];
        std::cout << "[TERRAIN] Generating: " << preset.name << " (seed=" << preset.seed << ")\n";
        terrain = generate_terrain(WORLD_W, WORLD_H, preset.env, preset.seed, &gen_timings);
        std::cout << "  Terrain: " << std::fixed << std::setprecision(1) << gen_timings.total_ms
                  << " ms\n";
        cam.center_on_world(WORLD_W, WORLD_H, Renderer::TILE_SIZE);
        bake_current_weather();
    };

    generate_current();
    overlay = OverlayMode::Temperature;  // Start with temperature overlay visible

    // UI button state
    ButtonRect prev_btn = {0, 0, 0, 0};
    ButtonRect next_btn = {0, 0, 0, 0};
    ButtonRect bake_btn = {0, 0, 0, 0};

    // Key state for continuous panning
    bool key_up = false, key_down = false, key_left = false, key_right = false;
    uint32_t last_tick = SDL_GetTicks();

    // Event loop
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

                case SDL_MOUSEBUTTONDOWN: {
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        int mx = event.button.x;
                        int my = event.button.y;

                        if (prev_btn.contains(mx, my)) {
                            current_preset = (current_preset - 1 + NUM_PRESETS) % NUM_PRESETS;
                            generate_current();
                        } else if (next_btn.contains(mx, my)) {
                            current_preset = (current_preset + 1) % NUM_PRESETS;
                            generate_current();
                        } else if (bake_btn.contains(mx, my)) {
                            bake_current_weather();
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

                        // Bake weather
                        case SDLK_SPACE:
                            bake_current_weather();
                            break;

                        // Next/prev terrain
                        case SDLK_RIGHTBRACKET:
                        case SDLK_n:
                            current_preset = (current_preset + 1) % NUM_PRESETS;
                            generate_current();
                            break;
                        case SDLK_LEFTBRACKET:
                        case SDLK_p:
                            current_preset = (current_preset - 1 + NUM_PRESETS) % NUM_PRESETS;
                            generate_current();
                            break;

                        // Overlays: 1-9
                        case SDLK_1:
                            overlay = OverlayMode::None;
                            break;
                        case SDLK_2:
                            overlay = OverlayMode::Temperature;
                            break;
                        case SDLK_3:
                            overlay = OverlayMode::Precipitation;
                            break;
                        case SDLK_4:
                            overlay = OverlayMode::Moisture;
                            break;
                        case SDLK_5:
                            overlay = OverlayMode::WindDirection;
                            break;
                        case SDLK_6:
                            overlay = OverlayMode::Evaporation;
                            break;
                        case SDLK_7:
                            overlay = OverlayMode::Storminess;
                            break;
                        case SDLK_8:
                            overlay = OverlayMode::MoistureBars;
                            break;
                        case SDLK_9:
                            overlay = OverlayMode::RainShadow;
                            break;
                        case SDLK_0:
                            overlay = OverlayMode::SurfaceWater;
                            break;
                        case SDLK_MINUS:
                            overlay = OverlayMode::SoilMoisture;
                            break;
                        case SDLK_EQUALS:
                            overlay = OverlayMode::SnowDepth;
                            break;

                        // Dynamics: play/pause with T, speed with +/- (numpad)
                        case SDLK_t:
                            dynamics.paused = !dynamics.paused;
                            std::cout << (dynamics.paused ? "[DYNAMICS] Paused\n"
                                                          : "[DYNAMICS] Running\n");
                            break;
                        case SDLK_KP_PLUS:
                            dynamics.time_scale =
                                std::min(dynamics.time_scale * 2.0f, 32.0f);
                            std::cout << "[DYNAMICS] Speed: " << dynamics.time_scale << "x\n";
                            break;
                        case SDLK_KP_MINUS:
                            dynamics.time_scale =
                                std::max(dynamics.time_scale * 0.5f, 0.25f);
                            std::cout << "[DYNAMICS] Speed: " << dynamics.time_scale << "x\n";
                            break;
                        case SDLK_r:
                            if (weather_baked) {
                                init_dynamics(dynamics, terrain, climate);
                                dyn_stats = compute_dynamic_stats(dynamics, terrain);
                                std::cout << "[DYNAMICS] Reset\n";
                            }
                            break;

                        // Grid
                        case SDLK_g:
                            show_grid = !show_grid;
                            break;

                        // Wind arrows toggle
                        case SDLK_v:
                            show_wind = !show_wind;
                            break;

                        // FPS
                        case SDLK_f:
                            show_fps = !show_fps;
                            break;

                        // Export
                        case SDLK_F12: {
                            int w, h;
                            SDL_GetWindowSize(window, &w, &h);
                            std::string fname =
                                "weather_" + std::string(PRESETS[current_preset].name) + ".bmp";
                            take_screenshot(sdl_renderer, w, h, fname);
                            break;
                        }
                        case SDLK_l:
                            if (weather_baked) {
                                export_log(PRESETS[current_preset], gen_timings, weather_timings,
                                           weather_stats);
                            }
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

        // Tick dynamic simulation
        if (weather_baked) {
            tick_atmosphere(atmosphere, terrain, dynamics, dt);
            tick_dynamics(dynamics, terrain, climate, dt, &atmosphere);
            // Update stats periodically (not every frame for performance)
            if ((SDL_GetTicks() & 0x1F) == 0) {  // every ~32 frames
                dyn_stats = compute_dynamic_stats(dynamics, terrain);
                atmo_stats = compute_atmosphere_stats(atmosphere);
                if (!dynamics.paused)
                    weather_stats = compute_live_weather_stats(atmosphere, dynamics, terrain);
            }

            // Comprehensive diagnostic dump every 2 sim-days
            if (!dynamics.paused && dynamics.elapsed_days - last_diag_day >= 2.0f) {
                last_diag_day = dynamics.elapsed_days;
                // Compute fresh stats for accurate diagnostics
                atmo_stats = compute_atmosphere_stats(atmosphere);
                dyn_stats = compute_dynamic_stats(dynamics, terrain);
                auto& as = atmo_stats;
                auto& ds = dyn_stats;
                std::cout << std::fixed << std::setprecision(3)
                          << "[DAY " << std::setprecision(1) << dynamics.elapsed_days << "] "
                          << "atmo: T=" << std::setprecision(1) << as.T_min << "/"
                          << as.T_mean << "/" << as.T_max
                          << "  q=" << std::setprecision(3) << as.q_min << "/"
                          << as.q_mean << "/" << as.q_max
                          << "  cloud=" << as.cloud_mean << "(max " << as.cloud_max << ")"
                          << "  precip=" << as.precip_mean << "(max " << as.precip_max << ")"
                          << "  wind=" << std::setprecision(2) << as.wind_mean
                          << "(max " << as.wind_max << ")"
                          << "  P=" << std::setprecision(0) << as.p_min << "-" << as.p_max
                          << "  rain=" << as.raining_cells << " cloudy=" << as.cloudy_cells
                          << "\n        dyn: sw=" << std::setprecision(4)
                          << ds.surface_water_mean << "(max " << ds.surface_water_max << ")"
                          << "  soil=" << ds.soil_moisture_mean
                          << "(max " << ds.soil_moisture_max << ")"
                          << "  snow=" << ds.snow_mean << "(max " << ds.snow_max << ")"
                          << "  snow_tiles=" << ds.snow_tiles
                          << "  flooded=" << ds.flooded_tiles
                          << "\n        inv: H2O=" << std::setprecision(1)
                          << as.total_atmo_water
                          << "  CFL=" << std::setprecision(2) << as.max_courant
                          << (as.max_courant > 1.0f ? " VIOLATION" : "")
                          << "  corr=" << std::setprecision(4) << as.water_correction
                          << "\n";
            }
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
            title << "Weather | " << PRESETS[current_preset].name;

            if (tile_x >= 0 && tile_y >= 0 && tile_x < static_cast<int>(terrain.width) &&
                tile_y < static_cast<int>(terrain.height)) {
                const auto& tt =
                    terrain.tile_at(static_cast<uint32_t>(tile_x), static_cast<uint32_t>(tile_y));
                title << " | (" << tile_x << "," << tile_y << ") " << elevband_name(tt.band);

                if (weather_baked) {
                    auto utx = static_cast<uint32_t>(tile_x);
                    auto uty = static_cast<uint32_t>(tile_y);

                    // Show atmosphere values when dynamics running, else static
                    float show_t, show_p, show_m, show_wu, show_wv;
                    if (!dynamics.paused) {
                        show_t = atmosphere.sample(utx, uty, &AtmosphereCell::T);
                        show_p = dynamics.tile_at(utx, uty).effective_precip;
                        show_m = dynamics.tile_at(utx, uty).effective_moisture;
                        show_wu = atmosphere.sample(utx, uty, &AtmosphereCell::u);
                        show_wv = atmosphere.sample(utx, uty, &AtmosphereCell::v);
                    } else {
                        const auto& ct = climate.tile_at(utx, uty);
                        show_t = ct.temperature;
                        show_p = ct.precipitation;
                        show_m = ct.moisture;
                        show_wu = ct.wind_u;
                        show_wv = ct.wind_v;
                    }

                    title << " T=" << std::fixed << std::setprecision(1) << show_t << "C"
                          << " P=" << std::setprecision(2) << show_p
                          << " M=" << show_m;

                    float speed = std::sqrt(show_wu * show_wu + show_wv * show_wv);
                    title << " W=" << wind_direction_name(show_wu, show_wv) << "@"
                          << std::setprecision(1) << speed;

                    // Dynamic tile state
                    const auto& dyntile = dynamics.tile_at(static_cast<uint32_t>(tile_x),
                                                           static_cast<uint32_t>(tile_y));
                    title << " SW=" << std::setprecision(2) << dyntile.surface_water
                          << " SM=" << dyntile.soil_moisture;
                    if (dyntile.snow_depth > 0.01f)
                        title << " Snow=" << dyntile.snow_depth;
                }
            }

            if (overlay != OverlayMode::None) {
                title << " | Overlay: " << overlay_name(overlay);
            }

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

        tile_renderer.render_terrain(terrain, cam, win_w, win_h,
                                   weather_baked ? &dynamics : nullptr);

        // Weather overlays (only if baked)
        if (weather_baked && overlay != OverlayMode::None) {
            // Dynamic overlays
            if (overlay == OverlayMode::SurfaceWater || overlay == OverlayMode::SoilMoisture ||
                overlay == OverlayMode::SnowDepth) {
                tile_renderer.render_dynamic_overlay(terrain, dynamics, cam, win_w, win_h, overlay);
            } else if (overlay == OverlayMode::MoistureBars) {
                tile_renderer.render_moisture_bars(terrain, climate, cam, win_w, win_h, &dynamics,
                                                  &atmosphere);
            } else if (overlay != OverlayMode::WindDirection) {
                tile_renderer.render_weather_overlay(terrain, climate, cam, win_w, win_h, overlay,
                                                    &dynamics, &atmosphere);
            }
        }

        // Wind arrows: composite on top of any overlay (toggle with V key)
        if (weather_baked && show_wind && overlay != OverlayMode::None) {
            tile_renderer.render_wind_arrows(climate, cam, win_w, win_h, &atmosphere);
        }

        if (show_grid) {
            render_grid(sdl_renderer, terrain.width, terrain.height, cam, win_w, win_h,
                        Renderer::TILE_SIZE);
        }

        if (weather_baked) {
            render_weather_legend(sdl_renderer, weather_stats, overlay, win_w, win_h);
            render_dynamic_legend(sdl_renderer, dyn_stats, atmo_stats, dynamics.elapsed_days,
                                  dynamics.time_scale, dynamics.paused, overlay, win_w, win_h,
                                  atmosphere.time_of_day, atmosphere.day_of_year);
        }

        // Buttons (top area)
        {
            int mx, my;
            SDL_GetMouseState(&mx, &my);
            constexpr int BTN_MARGIN = 10;
            constexpr int BTN_GAP = 8;

            int bx = BTN_MARGIN;

            bool prev_hov = prev_btn.w > 0 && prev_btn.contains(mx, my);
            prev_btn = render_button(sdl_renderer, bx, BTN_MARGIN, "< PREV", prev_hov);
            bx += prev_btn.w + BTN_GAP;

            bool next_hov = next_btn.w > 0 && next_btn.contains(mx, my);
            next_btn = render_button(sdl_renderer, bx, BTN_MARGIN, "NEXT >", next_hov);
            bx += next_btn.w + BTN_GAP + 20;

            const char* bake_label = weather_baked ? "REBAKE" : "BAKE WEATHER";
            bool bake_hov = bake_btn.w > 0 && bake_btn.contains(mx, my);
            bake_btn = render_button(sdl_renderer, bx, BTN_MARGIN, bake_label, bake_hov);
        }

        // Help hint bar (top-right)
        {
            constexpr int HINT_SCALE = 1;
            constexpr int HINT_PAD = 6;
            const char* hint = "1-9 0-=:Overlays V:Wind G:Grid T:Dynamics R:Reset";
            int hint_w = text_pixel_width(hint, HINT_SCALE);
            int hint_x = win_w - hint_w - HINT_PAD * 2 - 8;
            int hint_y = 10;
            int hint_h = 7 * HINT_SCALE + HINT_PAD * 2;

            SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 160);
            SDL_Rect hint_bg = {hint_x, hint_y, hint_w + HINT_PAD * 2, hint_h};
            SDL_RenderFillRect(sdl_renderer, &hint_bg);
            draw_text(sdl_renderer, hint_x + HINT_PAD, hint_y + HINT_PAD, hint, HINT_SCALE, 180,
                      180, 200);
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
