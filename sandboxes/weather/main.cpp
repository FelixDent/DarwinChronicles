#include <SDL.h>

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

#include "atmosphere.h"
#include "dynamics.h"
#include "renderer.h"
#include "telemetry.h"
#include "terrain_gen.h"
#include "tile_texture.h"
#include "ui.h"
#include "weather.h"

using namespace sandbox;

// ── Controls hint box ────────────────────────────────────────────────────────

static void render_controls_hint(SDL_Renderer* r, int win_w, int win_h) {
    constexpr int HINT_SCALE = 1;
    const char* hlines[] = {
        "T:PLAY/PAUSE  R:RESET  SPACE:REBAKE",
        ",:SLOWER  .:FASTER  V:WIND  G:GRID",
        "N/P:PRESET  H:HIDE HUD  OVERLAYS:BTN",
    };
    int hint_w = ui::auto_width({hlines[0], hlines[1], hlines[2]}, HINT_SCALE, ui::S2);
    int hint_h = ui::S2 * 2 + ui::row_height(HINT_SCALE) * 3;

    // Bottom-right anchor
    ui::Panel hint_panel;
    hint_panel.bounds = {win_w - hint_w - ui::SAFE_R, win_h - hint_h - ui::SAFE_B, hint_w, hint_h};
    hint_panel.padding = ui::S2;
    hint_panel.draw_bg(r);

    SDL_Rect prev_clip = hint_panel.push_clip(r);
    auto cr = hint_panel.content_rect();
    ui::ColumnLayout col{cr.x, cr.y, cr.w, 0};
    auto muted = ui::Theme::instance().text_muted;
    for (auto* hl : hlines)
        col.text(r, hl, HINT_SCALE, muted);
    ui::Panel::pop_clip(r, prev_clip);
}

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
      .ridge_strength = 0.6f,
      .plate_count = 14,
      .continental_ratio = 0.30f},
     42},
    {"Pangaea",
     {.water_level = 0.40f,
      .noise_scale = 0.015f,
      .continent_threshold = 0.65f,
      .ridge_strength = 1.2f,
      .plate_count = 8,
      .continental_ratio = 0.55f},
     101},
    {"Island Chain",
     {.water_level = 0.58f,
      .noise_scale = 0.025f,
      .continent_threshold = 0.45f,
      .ridge_strength = 0.8f,
      .plate_count = 16,
      .continental_ratio = 0.25f},
     256},
    {"Continental",
     {.water_level = 0.42f,
      .noise_scale = 0.020f,
      .continent_threshold = 0.55f,
      .ridge_strength = 1.0f,
      .plate_count = 12,
      .continental_ratio = 0.45f},
     789},
    {"Highland",
     {.water_level = 0.35f,
      .noise_scale = 0.020f,
      .continent_threshold = 0.60f,
      .ridge_strength = 1.8f,
      .plate_count = 10,
      .continental_ratio = 0.50f},
     1337},
    {"Waterworld",
     {.water_level = 0.60f,
      .noise_scale = 0.030f,
      .continent_threshold = 0.40f,
      .ridge_strength = 0.5f,
      .plate_count = 18,
      .continental_ratio = 0.20f},
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
    TickVarianceBudget tick_diag;
    // Accumulators for averaging variance budget across ticks between reports
    float avg_diag_count = 0;
    TickVarianceBudget avg_diag{};

    // ── Reality-check tracking ──────────────────────────────────────────
    // Track wind direction at 6 sample cells over time (sampled every ~0.5 days)
    // to measure temporal decorrelation — the key test for "does wind change direction?"
    struct SamplePoint {
        uint32_t x, y;
        const char* label;
    };
    SamplePoint samples[] = {
        {atmosphere.width / 4, atmosphere.height / 4, "NW-mid"},
        {atmosphere.width / 2, atmosphere.height / 4, "N-center"},
        {3 * atmosphere.width / 4, atmosphere.height / 4, "NE-mid"},
        {atmosphere.width / 4, atmosphere.height / 2, "W-equator"},
        {atmosphere.width / 2, atmosphere.height / 2, "center"},
        {atmosphere.width / 2, 3 * atmosphere.height / 4, "S-center"},
    };
    constexpr int N_SAMPLES = 6;
    // Store wind angles at each sample point over time
    std::vector<std::vector<float>> wind_history(N_SAMPLES);
    // Track per-cell rain days (was it raining this snapshot?)
    std::vector<uint32_t> rain_day_count(atmosphere.cells.size(), 0);
    uint32_t rain_snapshot_count = 0;
    // Track per-cell wind direction for spatial uniformity check
    float last_rc_sample = -1.0f;
    constexpr float RC_SAMPLE_INTERVAL = 0.5f;  // sample every 0.5 sim-days

    while (dynamics.elapsed_days < static_cast<float>(days) && !g_quit) {
        tick_atmosphere(atmosphere, terrain, dynamics, DT, &tick_diag);
        tick_dynamics(dynamics, terrain, climate, DT, &atmosphere);

        // Reality-check sampling
        if (dynamics.elapsed_days - last_rc_sample >= RC_SAMPLE_INTERVAL) {
            last_rc_sample = dynamics.elapsed_days;
            // Sample wind direction at probe points
            for (int si = 0; si < N_SAMPLES; ++si) {
                const auto& c = atmosphere.cell_at(samples[si].x, samples[si].y);
                float angle = std::atan2(c.v, c.u);
                wind_history[si].push_back(angle);
            }
            // Track rain occurrence per cell
            rain_snapshot_count++;
            for (size_t i = 0; i < atmosphere.cells.size(); ++i) {
                if (atmosphere.cells[i].precip_rate > 0.005f)
                    rain_day_count[i]++;
            }
        }

        // Accumulate variance budget for averaging
        if (tick_diag.valid) {
            for (int i = 0; i < 7; ++i) {
                avg_diag.T_anom_var[i] += tick_diag.T_anom_var[i];
                avg_diag.q_anom_var[i] += tick_diag.q_anom_var[i];
            }
            avg_diag.displacement_p50 += tick_diag.displacement_p50;
            avg_diag.displacement_p90 += tick_diag.displacement_p90;
            avg_diag.displacement_max =
                std::max(avg_diag.displacement_max, tick_diag.displacement_max);
            avg_diag.frac_sub_tenth += tick_diag.frac_sub_tenth;
            avg_diag_count++;
        }

        // Cheap per-tick cumulative extreme tracking (no full stats computation)
        uint32_t tick_heat = 0, tick_freeze = 0, tick_storm = 0;
        uint32_t tick_rain = 0, tick_gale = 0;
        for (const auto& c : atmosphere.cells) {
            cum_T_max = std::max(cum_T_max, c.T);
            cum_T_min = std::min(cum_T_min, c.T);
            cum_precip_peak = std::max(cum_precip_peak, c.precip_rate);
            cum_storm_max = std::max(cum_storm_max, c.storminess);
            if (c.T > 30.0f)
                ++tick_heat;
            if (c.T < -25.0f)
                ++tick_freeze;
            if (c.storminess > 0.5f)
                ++tick_storm;
            if (c.precip_rate > 0.15f)
                ++tick_rain;
            float ws = std::sqrt(c.u * c.u + c.v * c.v);
            if (ws > 4.0f)
                ++tick_gale;
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

            std::cout << std::setprecision(0) << std::setw(3) << dynamics.elapsed_days << " | "
                      << std::setprecision(1) << std::setw(6) << as.T_band[0] << " " << std::setw(6)
                      << as.T_band[1] << " " << std::setw(6) << as.T_band[2] << " | "
                      << std::setw(6) << as.T_min << " " << std::setw(6) << as.T_mean << " "
                      << std::setw(6) << as.T_max << " " << std::setw(5) << as.T_stddev << " | "
                      << std::setprecision(2) << std::setw(5) << as.wind_mean << " " << std::setw(5)
                      << as.wind_max << " " << std::setw(5) << as.wind_stddev << " " << std::setw(4)
                      << as.wind_dir_spread << " | " << std::setprecision(3) << std::setw(6)
                      << as.q_mean << " " << std::setw(5) << as.q_stddev << " " << std::setw(6)
                      << as.cloud_mean << " " << std::setw(6) << as.precip_mean << " "
                      << std::setw(5) << as.precip_stddev << " | " << std::setprecision(2)
                      << std::setw(4) << as.storm_max << " " << std::setw(5)
                      << as.severe_storm_cells << " " << std::setw(5) << as.heavy_rain_cells << " "
                      << std::setw(5) << as.raining_cells << "/" << std::setw(5) << as.cloudy_cells
                      << " " << std::setw(4) << as.gale_cells << " " << std::setw(4)
                      << as.heat_wave_cells << " " << std::setw(5) << as.deep_freeze_cells;

            // QG diagnostics: psi rms to track instability growth
            double psi1_sum2 = 0, psi2_sum2 = 0, psi_bc_sum2 = 0;
            for (const auto& c : atmosphere.cells) {
                psi1_sum2 += static_cast<double>(c.psi1) * c.psi1;
                psi2_sum2 += static_cast<double>(c.psi2) * c.psi2;
                float bc = c.psi1 - c.psi2;
                psi_bc_sum2 += static_cast<double>(bc) * bc;
            }
            float nc = static_cast<float>(atmosphere.cells.size());
            std::cout << std::setprecision(3) << " | psi1=" << std::sqrt(psi1_sum2 / nc)
                      << " psi2=" << std::sqrt(psi2_sum2 / nc)
                      << " bc=" << std::sqrt(psi_bc_sum2 / nc) << "\n";
        }
    }

    std::cout << "\n=== Final state ===\n";
    auto as = compute_atmosphere_stats(atmosphere);
    auto ds_final = compute_dynamic_stats(dynamics, terrain);
    std::cout << "T range: " << std::setprecision(1) << as.T_min << " to " << as.T_max << " (mean "
              << as.T_mean << ")\n";
    std::cout << "T bands [N/Eq/S]: " << as.T_band[0] << " / " << as.T_band[1] << " / "
              << as.T_band[2] << "\n";
    std::cout << "Wind bands [N/Eq/S]: " << std::setprecision(2) << as.wind_band[0] << " / "
              << as.wind_band[1] << " / " << as.wind_band[2] << "\n";
    float T_spread = as.T_max - as.T_min;
    float band_spread = std::max({as.T_band[0], as.T_band[1], as.T_band[2]}) -
                        std::min({as.T_band[0], as.T_band[1], as.T_band[2]});
    std::cout << "T spread: " << std::setprecision(1) << T_spread
              << " (band spread: " << band_spread << ")\n";
    std::cout << "Variation — T_sd: " << std::setprecision(2) << as.T_stddev
              << "  W_sd: " << as.wind_stddev << "  Q_sd: " << std::setprecision(3) << as.q_stddev
              << "  P_sd: " << as.precip_stddev << "  Wdir_spread: " << std::setprecision(2)
              << as.wind_dir_spread << "\n";

    if (T_spread < 5.0f)
        std::cout << "WARNING: Temperature nearly uniform!\n";
    if (band_spread < 3.0f)
        std::cout << "WARNING: Latitude bands nearly identical!\n";
    if (as.wind_max < 0.5f)
        std::cout << "WARNING: Wind nearly dead!\n";
    if (as.T_stddev < 1.0f)
        std::cout << "WARNING: Temperature variation collapsed!\n";
    if (as.wind_stddev < 0.2f)
        std::cout << "WARNING: Wind speed variation collapsed!\n";
    if (as.wind_dir_spread < 0.3f)
        std::cout << "WARNING: Wind direction nearly uniform!\n";
    std::cout << "Peak extremes (across entire run):\n";
    std::cout << "  T range: " << std::setprecision(1) << cum_T_min << " to " << cum_T_max << "\n";
    std::cout << "  storm_max: " << std::setprecision(2) << cum_storm_max
              << "  peak_severe_storms: " << cum_severe_storms
              << "  peak_heavy_rain: " << cum_heavy_rain << "  peak_gales: " << cum_gale << "\n";
    std::cout << "  peak_heat_wave: " << cum_heat_wave << "  peak_deep_freeze: " << cum_deep_freeze
              << "  precip_peak: " << std::setprecision(3) << cum_precip_peak << "\n";
    std::cout << "Aridity: mean=" << std::setprecision(2) << ds_final.aridity_mean
              << " min=" << ds_final.aridity_min << " max=" << ds_final.aridity_max
              << "  arid_tiles=" << ds_final.arid_tiles << " humid_tiles=" << ds_final.humid_tiles
              << "\n";
    std::cout << "Hydrology: groundwater_mean=" << std::setprecision(4) << ds_final.groundwater_mean
              << " max=" << ds_final.groundwater_max
              << "  discharge_mean=" << ds_final.discharge_mean << " max=" << ds_final.discharge_max
              << "\n";
    std::cout << "Surface: sw_mean=" << std::setprecision(4) << ds_final.surface_water_mean
              << " sw_max=" << ds_final.surface_water_max << " flooded=" << ds_final.flooded_tiles
              << " soil_mean=" << ds_final.soil_moisture_mean << "\n";
    if (cum_storm_max < 0.5f)
        std::cout << "WARNING: No significant storms!\n";
    if (cum_severe_storms == 0 && cum_heavy_rain == 0)
        std::cout << "WARNING: No extreme weather events!\n";

    // ── Reality Check Report ──────────────────────────────────────────────
    std::cout << "\n=== REALITY CHECK ===\n";

    // RC1: Wind direction temporal decorrelation at sample points
    // Real weather: wind direction at a point changes over days (decorrelation ~3-7 days)
    // Fake weather: wind direction stays ~constant (autocorrelation stays near 1.0)
    std::cout << "\n--- RC1: Wind Direction Temporal Variability ---\n";
    std::cout << "  (autocorrelation of wind angle at lag 1,3,7,14 days — real weather "
                 "decorrelates by lag 3-7)\n";
    for (int si = 0; si < N_SAMPLES; ++si) {
        const auto& hist = wind_history[si];
        if (hist.size() < 30)
            continue;
        std::cout << "  " << samples[si].label << " (" << samples[si].x << "," << samples[si].y
                  << "): n=" << hist.size();

        // Compute circular autocorrelation at various lags
        // Lag in samples (each sample = 0.5 days)
        int lags[] = {2, 6, 14, 28};  // 1, 3, 7, 14 days
        const char* lag_labels[] = {"1d", "3d", "7d", "14d"};
        for (int li = 0; li < 4; ++li) {
            int lag = lags[li];
            if (static_cast<int>(hist.size()) <= lag) {
                std::cout << "  " << lag_labels[li] << "=N/A";
                continue;
            }
            // Circular correlation: mean of cos(theta_t - theta_{t+lag})
            double corr_sum = 0;
            int corr_count = 0;
            for (size_t t = 0; t + static_cast<size_t>(lag) < hist.size(); ++t) {
                corr_sum +=
                    std::cos(static_cast<double>(hist[t] - hist[t + static_cast<size_t>(lag)]));
                corr_count++;
            }
            float autocorr = (corr_count > 0) ? static_cast<float>(corr_sum / corr_count) : 0.0f;
            std::cout << "  " << lag_labels[li] << "=" << std::setprecision(2) << autocorr;
        }

        // Also print the actual wind direction range (min/max angle)
        float dir_sum_sin = 0, dir_sum_cos = 0;
        for (float a : hist) {
            dir_sum_sin += std::sin(a);
            dir_sum_cos += std::cos(a);
        }
        float mean_resultant = std::sqrt(dir_sum_sin * dir_sum_sin + dir_sum_cos * dir_sum_cos) /
                               static_cast<float>(hist.size());
        std::cout << "  MRL=" << std::setprecision(2) << mean_resultant;
        // MRL close to 1 = always same direction. Close to 0 = uniformly varied.
        if (mean_resultant > 0.7f)
            std::cout << " FAIL:constant";
        else if (mean_resultant > 0.4f)
            std::cout << " WARN:biased";
        else
            std::cout << " OK:varied";
        std::cout << "\n";
    }

    // RC2: Spatial wind direction uniformity
    // Real weather: neighboring cells have different wind directions
    // Fake weather: all cells in a latitude band blow the same direction
    std::cout << "\n--- RC2: Spatial Wind Direction Structure ---\n";
    std::cout << "  (adjacent cell wind angle difference — real weather has varied directions)\n";
    {
        // Compute distribution of wind angle differences between adjacent cells
        std::vector<float> adj_diffs;
        uint32_t aw = atmosphere.width, ah = atmosphere.height;
        for (uint32_t ay = 1; ay + 1 < ah; ++ay) {
            for (uint32_t ax = 0; ax < aw; ++ax) {
                const auto& c = atmosphere.cell_at(ax, ay);
                float angle_c = std::atan2(c.v, c.u);
                // Right neighbor (periodic)
                uint32_t ax_r = (ax + 1) % aw;
                const auto& r = atmosphere.cell_at(ax_r, ay);
                float angle_r = std::atan2(r.v, r.u);
                float diff = std::abs(angle_c - angle_r);
                if (diff > 3.14159f)
                    diff = 6.28318f - diff;
                adj_diffs.push_back(diff);
                // Down neighbor
                const auto& d = atmosphere.cell_at(ax, ay + 1);
                float angle_d = std::atan2(d.v, d.u);
                diff = std::abs(angle_c - angle_d);
                if (diff > 3.14159f)
                    diff = 6.28318f - diff;
                adj_diffs.push_back(diff);
            }
        }
        std::sort(adj_diffs.begin(), adj_diffs.end());
        auto pct = [&](float p) {
            size_t idx = static_cast<size_t>(p * static_cast<float>(adj_diffs.size() - 1));
            return adj_diffs[idx] * 180.0f / 3.14159f;  // convert to degrees
        };
        std::cout << "  Adjacent cell angle diff (degrees): p10=" << std::setprecision(1)
                  << pct(0.1f) << " p50=" << pct(0.5f) << " p90=" << pct(0.9f)
                  << " max=" << pct(1.0f) << "\n";
        // At 625km grid cells, cells within the same weather system (500-1500km) SHOULD
        // have similar wind direction. Median diff of 5-15 deg is realistic.
        if (pct(0.5f) < 3.0f)
            std::cout << "  FAIL: median diff < 3 deg — wind is spatially uniform\n";
        else if (pct(0.5f) < 8.0f)
            std::cout << "  WARN: median diff 3-8 deg — limited spatial structure\n";
        else
            std::cout << "  OK: median diff >= 8 deg — good spatial variation\n";
    }

    // RC3: Precipitation intermittency
    // Real weather: any location has rain 10-40% of days, not 0% or 100%
    std::cout << "\n--- RC3: Precipitation Intermittency ---\n";
    if (rain_snapshot_count > 10) {
        std::vector<float> rain_fracs;
        uint32_t always_dry = 0, always_wet = 0;
        for (size_t i = 0; i < atmosphere.cells.size(); ++i) {
            float frac =
                static_cast<float>(rain_day_count[i]) / static_cast<float>(rain_snapshot_count);
            rain_fracs.push_back(frac);
            if (frac < 0.01f)
                always_dry++;
            if (frac > 0.9f)
                always_wet++;
        }
        std::sort(rain_fracs.begin(), rain_fracs.end());
        auto pct = [&](float p) {
            return rain_fracs[static_cast<size_t>(p * static_cast<float>(rain_fracs.size() - 1))];
        };
        std::cout << "  Rain fraction per cell: p10=" << std::setprecision(2) << pct(0.1f)
                  << " p25=" << pct(0.25f) << " p50=" << pct(0.5f) << " p75=" << pct(0.75f)
                  << " p90=" << pct(0.9f) << "\n";
        std::cout << "  Always dry (<1%): " << always_dry << "/" << atmosphere.cells.size()
                  << "  Always wet (>90%): " << always_wet << "\n";
        if (pct(0.5f) < 0.02f)
            std::cout << "  FAIL: most cells never see rain\n";
        else if (always_wet > atmosphere.cells.size() / 4)
            std::cout << "  FAIL: >25% cells always raining\n";
        else
            std::cout << "  OK: intermittent precipitation\n";
    }

    // RC4: T_anom spatial autocorrelation length
    // Real weather: weather systems span 3-8 cells, not grid-wide
    std::cout << "\n--- RC4: Temperature Anomaly Spatial Scale ---\n";
    {
        uint32_t aw = atmosphere.width, ah = atmosphere.height;
        // Compute correlation of T_anom at various x-separation lags (zonal)
        std::cout << "  T_anom zonal autocorrelation: ";
        int sep_lags[] = {1, 2, 4, 8, 16, 32};
        for (int lag : sep_lags) {
            if (static_cast<uint32_t>(lag) >= aw / 2)
                break;
            double corr_num = 0, var = 0;
            int cnt = 0;
            for (uint32_t ay = 2; ay + 2 < ah; ++ay) {
                for (uint32_t ax = 0; ax < aw; ++ax) {
                    float a = atmosphere.cell_at(ax, ay).T_anom;
                    float b = atmosphere.cell_at((ax + static_cast<uint32_t>(lag)) % aw, ay).T_anom;
                    corr_num += static_cast<double>(a) * b;
                    var += static_cast<double>(a) * a;
                    cnt++;
                }
            }
            float autocorr = (var > 0.01) ? static_cast<float>(corr_num / var) : 0.0f;
            std::cout << "lag" << lag << "=" << std::setprecision(2) << autocorr << " ";
        }
        std::cout << "\n";
        std::cout << "  (should drop below 0.5 by lag 4-8 for synoptic-scale weather)\n";
    }

    // Comprehensive hydrology diagnostics
    print_hydrology_diagnostics(dynamics, terrain);

    return 0;
}

// ── Main ────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // --headless N [preset]: run N sim-days without a window and print diagnostics
    // --headless-all N: run N sim-days for ALL presets
    // --headless-diag N [preset]: detailed atmosphere diagnostics for GPT analysis
    if (argc >= 2 && std::string(argv[1]) == "--headless-diag") {
        int days = (argc >= 3) ? std::atoi(argv[2]) : 120;
        if (days <= 0)
            days = 120;
        int preset = 3;  // default: Continental
        if (argc >= 4) {
            std::string arg = argv[3];
            bool found = false;
            for (int p = 0; p < NUM_PRESETS; ++p) {
                if (arg == PRESETS[p].name) {
                    preset = p;
                    found = true;
                    break;
                }
            }
            if (!found)
                preset = std::atoi(argv[3]);
        }
        preset = std::clamp(preset, 0, NUM_PRESETS - 1);
        const auto& pr = PRESETS[preset];

        constexpr uint32_t WORLD_W = 256, WORLD_H = 128;
        constexpr float DT = 1.0f / 30.0f;
        std::cout << "=== Detailed Atmosphere Diagnostics ===\n";
        std::cout << "Preset: " << pr.name << " | Sim days: " << days << "\n";

        GenerationTimings gt;
        Terrain terrain = generate_terrain(WORLD_W, WORLD_H, pr.env, pr.seed, &gt);
        WeatherTimings wt;
        AtmosphereConfig ac;
        ClimateData climate = bake_weather(terrain, ac, &wt);
        DynamicState dynamics;
        AtmosphereState atmosphere;
        init_dynamics(dynamics, terrain, climate);
        init_atmosphere(atmosphere, terrain, climate);
        dynamics.paused = false;

        // Print diagnostics at intervals: day 2, 10, 30, 60, and end
        float checkpoints[] = {2, 10, 30, 60, static_cast<float>(days)};
        int cp_idx = 0;
        float last_report = 0;

        // Time series header
        printf("\nTIME_SERIES:\n");
        printf("DAY | T_mean T_sd | bands[0] bands[7] bands[15] | ");
        printf("q_mean cloud precip p_sd storm_p95 | wind_p95 wind_max | ");
        printf("E_total E_drift%% | W_total w_corr | budget_mean budget_p05\n");

        TickVarianceBudget tick_diag_d;
        float avg_diag_count = 0;
        TickVarianceBudget avg_diag{};
        while (dynamics.elapsed_days < static_cast<float>(days) && !g_quit) {
            tick_atmosphere(atmosphere, terrain, dynamics, DT, &tick_diag_d);
            tick_dynamics(dynamics, terrain, climate, DT, &atmosphere);

            // Accumulate variance budget
            if (tick_diag_d.valid) {
                for (int i = 0; i < 7; ++i) {
                    avg_diag.T_anom_var[i] += tick_diag_d.T_anom_var[i];
                    avg_diag.q_anom_var[i] += tick_diag_d.q_anom_var[i];
                }
                avg_diag.displacement_p50 += tick_diag_d.displacement_p50;
                avg_diag.displacement_p90 += tick_diag_d.displacement_p90;
                avg_diag.displacement_max =
                    std::max(avg_diag.displacement_max, tick_diag_d.displacement_max);
                avg_diag.frac_sub_tenth += tick_diag_d.frac_sub_tenth;
                avg_diag_count++;
            }

            // Print time series every 2 days
            if (dynamics.elapsed_days - last_report >= 2.0f) {
                last_report = dynamics.elapsed_days;
                auto as = compute_atmosphere_stats(atmosphere);

                // Quick 16-band T computation for time series
                double band_T[16] = {};
                uint32_t band_n[16] = {};
                for (uint32_t ay = 0; ay < atmosphere.height; ++ay) {
                    int bi = std::clamp(static_cast<int>(ay * 16 / atmosphere.height), 0, 15);
                    for (uint32_t ax = 0; ax < atmosphere.width; ++ax) {
                        band_T[bi] += atmosphere.cells[ay * atmosphere.width + ax].T;
                        band_n[bi]++;
                    }
                }

                printf("%3.0f | %6.1f %5.1f | %6.1f %6.1f %6.1f | ", dynamics.elapsed_days,
                       as.T_mean, as.T_stddev, band_n[0] > 0 ? band_T[0] / band_n[0] : 0,
                       band_n[7] > 0 ? band_T[7] / band_n[7] : 0,
                       band_n[15] > 0 ? band_T[15] / band_n[15] : 0);
                printf("%6.4f %6.4f %6.4f %5.3f %6.4f | ", as.q_mean, as.cloud_mean, as.precip_mean,
                       as.precip_stddev, as.storm_max);
                printf("%5.2f %5.2f | ", as.wind_max * 0.95f,
                       as.wind_max);  // approximate p95 as 0.95*max
                printf("%8.0f %6.2f | ", atmosphere.total_energy, as.energy_drift_pct);
                printf("%7.3f %8.6f | ", atmosphere.total_water, atmosphere.last_water_correction);
                printf("%5.3f %5.3f\n", as.budget_mean, as.budget_min);
            }

            // Print detailed snapshot at checkpoints
            if (cp_idx < 5 && dynamics.elapsed_days >= checkpoints[cp_idx]) {
                printf("\n=== SNAPSHOT at day %.0f ===\n", dynamics.elapsed_days);
                print_atmosphere_diagnostics(atmosphere);
                print_variability_diagnostics(atmosphere);

                // Print averaged variance budget
                if (avg_diag_count > 0) {
                    float n = avg_diag_count;
                    printf("\n--- VARIANCE BUDGET (averaged over %.0f ticks) ---\n", n);
                    printf("  Step:      start    advect   diffuse  vert_ex  solar    moisture\n");
                    printf("  T_anom: ");
                    for (int i = 0; i < 6; ++i)
                        printf(" %8.4f", avg_diag.T_anom_var[i] / n);
                    printf("\n  q_anom: ");
                    for (int i = 0; i < 6; ++i)
                        printf(" %8.4f", avg_diag.q_anom_var[i] / n);
                    printf("\n  T_anom ratios (vs start): ");
                    float T0 = avg_diag.T_anom_var[0] / n;
                    if (T0 > 0) {
                        for (int i = 1; i < 6; ++i)
                            printf(" %.4f", (avg_diag.T_anom_var[i] / n) / T0);
                    }
                    printf("\n");
                    printf("  Displacement: p50=%.4f p90=%.4f max=%.4f frac<0.1=%.1f%%\n",
                           avg_diag.displacement_p50 / n, avg_diag.displacement_p90 / n,
                           avg_diag.displacement_max, 100.0f * avg_diag.frac_sub_tenth / n);
                    // Reset accumulators
                    avg_diag = {};
                    avg_diag_count = 0;
                }
                ++cp_idx;
            }
        }

        // Final hydrology
        print_hydrology_diagnostics(dynamics, terrain);
        return 0;
    }
    // --screenshots [preset] [days]: render full UI screenshots for multiple overlays
    if (argc >= 2 && std::string(argv[1]) == "--screenshots") {
        int preset = 3;  // default: Continental
        int sim_days = 30;
        if (argc >= 3) {
            std::string arg = argv[2];
            bool found = false;
            for (int p = 0; p < NUM_PRESETS; ++p) {
                if (arg == PRESETS[p].name) {
                    preset = p;
                    found = true;
                    break;
                }
            }
            if (!found)
                preset = std::atoi(argv[2]);
        }
        if (argc >= 4)
            sim_days = std::atoi(argv[3]);
        if (sim_days <= 0)
            sim_days = 30;
        preset = std::clamp(preset, 0, NUM_PRESETS - 1);
        const auto& pr = PRESETS[preset];

        constexpr uint32_t WORLD_W = 256, WORLD_H = 128;
        constexpr int WIN_W = 1280, WIN_H = 720;
        constexpr float DT = 1.0f / 30.0f;

        std::cout << "=== Screenshot Capture Mode ===\n";
        std::cout << "Preset: " << pr.name << " | Sim days: " << sim_days << "\n";

        // Generate terrain + bake weather
        GenerationTimings gt;
        Terrain terrain = generate_terrain(WORLD_W, WORLD_H, pr.env, pr.seed, &gt);
        WeatherTimings wt;
        AtmosphereConfig ac;
        ClimateData climate = bake_weather(terrain, ac, &wt);
        WeatherStats weather_stats = compute_weather_stats(climate, terrain);

        DynamicState dynamics;
        AtmosphereState atmosphere;
        init_dynamics(dynamics, terrain, climate);
        init_atmosphere(atmosphere, terrain, climate);
        dynamics.paused = false;

        // Simulate for requested days
        std::cout << "Simulating " << sim_days << " days...\n";
        while (dynamics.elapsed_days < static_cast<float>(sim_days) && !g_quit) {
            tick_atmosphere(atmosphere, terrain, dynamics, DT);
            tick_dynamics(dynamics, terrain, climate, DT, &atmosphere);
        }
        std::cout << "Simulation complete at day " << dynamics.elapsed_days << "\n";

        // Compute stats for legends (use live stats from running atmosphere)
        weather_stats = compute_live_weather_stats(atmosphere, dynamics, terrain);
        DynamicStats dyn_stats = compute_dynamic_stats(dynamics, terrain);
        AtmosphereStats atmo_stats = compute_atmosphere_stats(atmosphere);

        // Init SDL with offscreen software renderer
        if (SDL_Init(SDL_INIT_VIDEO) < 0) {
            std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
            return 1;
        }

        SDL_Surface* surface =
            SDL_CreateRGBSurfaceWithFormat(0, WIN_W, WIN_H, 32, SDL_PIXELFORMAT_RGBA32);
        if (!surface) {
            std::cerr << "Surface creation failed: " << SDL_GetError() << "\n";
            SDL_Quit();
            return 1;
        }
        SDL_Renderer* sw_renderer = SDL_CreateSoftwareRenderer(surface);
        if (!sw_renderer) {
            std::cerr << "Software renderer creation failed: " << SDL_GetError() << "\n";
            SDL_FreeSurface(surface);
            SDL_Quit();
            return 1;
        }

        Renderer tile_renderer;
        tile_renderer.init(sw_renderer);
        tile_renderer.bake_terrain_cache(terrain, pr.seed, pr.env.water_level);

        // Screenshots need detail level — wait for background bake to finish
        while (tile_renderer.is_detail_baking()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        // Force finalize (render_terrain would do this, but we call it explicitly)
        tile_renderer.render_terrain(terrain, Camera{}, 1, 1);

        // Camera centered on world
        Camera cam;
        cam.center_on_world(WORLD_W, WORLD_H, Renderer::TILE_SIZE);

        // Overlays to capture — representative selection
        struct OverlayCapture {
            OverlayMode mode;
            bool wind;
            float zoom;  // 0 = use default
            const char* suffix;
        };
        OverlayCapture captures[] = {
            // Default zoomed-out views with different overlays
            {OverlayMode::None, false, 0, "terrain"},
            {OverlayMode::Temperature, true, 0, "temperature"},
            {OverlayMode::Precipitation, false, 0, "precipitation"},
            {OverlayMode::Moisture, false, 0, "moisture"},
            {OverlayMode::WindDirection, true, 0, "wind"},
            {OverlayMode::Storminess, false, 0, "storminess"},
            {OverlayMode::SurfaceWater, false, 0, "surface_water"},
            {OverlayMode::SoilMoisture, false, 0, "soil_moisture"},
            {OverlayMode::SnowDepth, false, 0, "snow"},
            {OverlayMode::Aridity, false, 0, "aridity"},
            {OverlayMode::Discharge, false, 0, "discharge"},
            {OverlayMode::Stability, false, 0, "stability"},
            {OverlayMode::PrecipBudget, false, 0, "precip_budget"},
            {OverlayMode::Groundwater, false, 0, "groundwater"},
            {OverlayMode::Geology, false, 0, "geology"},
            {OverlayMode::UpperWind, true, 0, "upper_wind"},
            // Zoomed-in view
            {OverlayMode::Temperature, true, 2.5f, "temperature_zoomed"},
            {OverlayMode::Discharge, false, 2.5f, "discharge_zoomed"},
            // Zoomed-out view
            {OverlayMode::Temperature, true, 0.5f, "temperature_wide"},
        };

        for (auto& cap : captures) {
            Camera frame_cam = cam;
            if (cap.zoom > 0) {
                frame_cam.zoom = cap.zoom;
            }

            // Clear
            SDL_SetRenderDrawColor(sw_renderer, 20, 20, 30, 255);
            SDL_RenderClear(sw_renderer);

            // Terrain base (dim glyphs when overlay active)
            bool has_overlay = cap.mode != OverlayMode::None;
            tile_renderer.render_terrain(terrain, frame_cam, WIN_W, WIN_H, &dynamics);

            // Overlay
            if (cap.mode != OverlayMode::None) {
                if (cap.mode == OverlayMode::SurfaceWater ||
                    cap.mode == OverlayMode::SoilMoisture || cap.mode == OverlayMode::SnowDepth ||
                    cap.mode == OverlayMode::Aridity || cap.mode == OverlayMode::Groundwater ||
                    cap.mode == OverlayMode::Discharge) {
                    tile_renderer.render_dynamic_overlay(terrain, dynamics, frame_cam, WIN_W, WIN_H,
                                                         cap.mode);
                } else if (cap.mode == OverlayMode::MoistureBars) {
                    tile_renderer.render_moisture_bars(terrain, climate, frame_cam, WIN_W, WIN_H,
                                                       &dynamics, &atmosphere);
                } else if (cap.mode != OverlayMode::WindDirection) {
                    tile_renderer.render_weather_overlay(terrain, climate, frame_cam, WIN_W, WIN_H,
                                                         cap.mode, &dynamics, &atmosphere);
                }
            }

            // Wind arrows
            if (cap.wind && cap.mode != OverlayMode::None) {
                tile_renderer.render_wind_arrows(climate, frame_cam, WIN_W, WIN_H, &atmosphere);
            }

            // Legends
            {
                constexpr int PAD = 8;
                int stats_w = render_weather_stats(sw_renderer, weather_stats, WIN_W, WIN_H);
                render_overlay_legend(sw_renderer, cap.mode, WIN_W, WIN_H, stats_w + PAD, true);
            }
            render_dynamic_legend(sw_renderer, dyn_stats, atmo_stats, dynamics.elapsed_days,
                                  dynamics.time_scale, dynamics.paused, cap.mode, WIN_W, WIN_H,
                                  atmosphere.time_of_day, atmosphere.day_of_year);

            // Buttons
            {
                constexpr int BTN_MARGIN = 10;
                constexpr int BTN_GAP = 8;
                int bx = BTN_MARGIN;
                render_button(sw_renderer, bx, BTN_MARGIN, "< PREV", false);
                bx += 60 + BTN_GAP;
                render_button(sw_renderer, bx, BTN_MARGIN, "NEXT >", false);
                bx += 60 + BTN_GAP + 20;
                render_button(sw_renderer, bx, BTN_MARGIN, "REBAKE", false);
                bx += 70 + BTN_GAP + 20;
                char ov_label[64];
                std::snprintf(ov_label, sizeof(ov_label), "OVERLAY: %s", overlay_name(cap.mode));
                render_button(sw_renderer, bx, BTN_MARGIN, ov_label, false);
            }

            render_controls_hint(sw_renderer, WIN_W, WIN_H);

            // Save
            std::string fname = "weather_ui_" + std::string(pr.name) + "_" + cap.suffix + ".bmp";
            SDL_SaveBMP(surface, fname.c_str());
            std::cout << "  Saved: " << fname << "\n";
        }

        tile_renderer.shutdown();
        SDL_DestroyRenderer(sw_renderer);
        SDL_FreeSurface(surface);
        SDL_Quit();

        std::cout << "\nDone! " << (sizeof(captures) / sizeof(captures[0]))
                  << " screenshots saved.\n";
        return 0;
    }

    // --tiles [preset] [seed]: render sample tile patches for GPT review
    if (argc >= 2 && std::string(argv[1]) == "--tiles") {
        int preset_idx = 3;  // default: Continental
        if (argc >= 3) {
            std::string arg = argv[2];
            bool found = false;
            for (int i = 0; i < NUM_PRESETS; ++i) {
                if (arg == PRESETS[i].name) {
                    preset_idx = i;
                    found = true;
                    break;
                }
            }
            if (!found)
                preset_idx = std::atoi(argv[2]);
        }
        uint32_t seed_override = 0;
        bool use_override = false;
        if (argc >= 4) {
            seed_override = static_cast<uint32_t>(std::atoi(argv[3]));
            use_override = true;
        }

        const auto& pr = PRESETS[std::clamp(preset_idx, 0, NUM_PRESETS - 1)];
        uint32_t seed = use_override ? seed_override : pr.seed;
        constexpr uint32_t WORLD_W = 256, WORLD_H = 128;

        GenerationTimings gt;
        Terrain terrain = generate_terrain(WORLD_W, WORLD_H, pr.env, seed, &gt);
        std::cout << "Generated terrain: " << pr.name << " seed=" << seed << " (" << gt.total_ms
                  << " ms)\n";

        // Find sample regions: coastal, mountain, lowland, mixed
        struct SampleRegion {
            const char* name;
            int cx, cy;
        };
        SampleRegion regions[4] = {
            {"coast", -1, -1}, {"mountain", -1, -1}, {"lowland", -1, -1}, {"mixed", -1, -1}};

        // Scan terrain for good sample centers
        for (uint32_t y = 16; y < WORLD_H - 16; ++y) {
            for (uint32_t x = 16; x < WORLD_W - 16; ++x) {
                const auto& t = terrain.tile_at(x, y);
                // Coastal: land tile near ocean
                if (regions[0].cx < 0 && !t.is_ocean && t.dist_ocean < 4.0f &&
                    t.dist_ocean > 0.5f) {
                    regions[0] = {"coast", static_cast<int>(x), static_cast<int>(y)};
                }
                // Mountain: high elevation
                if (regions[1].cx < 0 && t.band == ElevBand::Mountains && t.elev01 > 0.7f) {
                    regions[1] = {"mountain", static_cast<int>(x), static_cast<int>(y)};
                }
                // Lowland: flat land far from ocean
                if (regions[2].cx < 0 && t.band == ElevBand::Lowland && t.dist_ocean > 15.0f &&
                    t.slope01 < 0.05f) {
                    regions[2] = {"lowland", static_cast<int>(x), static_cast<int>(y)};
                }
                // Mixed: hills near lowland
                if (regions[3].cx < 0 && t.band == ElevBand::Hills && t.dist_ocean > 5.0f) {
                    regions[3] = {"mixed", static_cast<int>(x), static_cast<int>(y)};
                }
            }
        }

        // Multi-zoom rendering for GPT review
        // Zoom levels: close (3x3 tiles), medium (12x12), region (32x32), planetary (full world)
        struct ZoomLevel {
            const char* suffix;
            int tiles;
        };
        ZoomLevel zooms[] = {{"close", 3}, {"medium", 12}, {"region", 32}};

        if (SDL_Init(SDL_INIT_VIDEO) < 0) {
            std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
            return 1;
        }

        // Per-region multi-zoom patches
        for (auto& reg : regions) {
            if (reg.cx < 0) {
                std::cout << "  Skipped " << reg.name << " (not found)\n";
                continue;
            }
            for (auto& zoom : zooms) {
                int patch_tiles = zoom.tiles;
                int patch_px = patch_tiles * TilePixels::SIZE;
                int px = std::clamp(reg.cx - patch_tiles / 2, 0,
                                    static_cast<int>(WORLD_W) - patch_tiles);
                int py = std::clamp(reg.cy - patch_tiles / 2, 0,
                                    static_cast<int>(WORLD_H) - patch_tiles);

                SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, patch_px, patch_px, 32,
                                                                   SDL_PIXELFORMAT_RGBA32);
                if (!surf)
                    continue;
                SDL_LockSurface(surf);
                render_tile_patch(terrain, px, py, patch_tiles, patch_tiles, seed,
                                  static_cast<uint32_t*>(surf->pixels), surf->pitch / 4,
                                  pr.env.water_level);
                SDL_UnlockSurface(surf);

                std::string fname =
                    "tiles_" + std::string(pr.name) + "_" + reg.name + "_" + zoom.suffix + ".bmp";
                SDL_SaveBMP(surf, fname.c_str());
                SDL_FreeSurface(surf);
                std::cout << "  Saved: " << fname << " (" << patch_tiles << "x" << patch_tiles
                          << " tiles, " << patch_px << "x" << patch_px << "px)\n";
            }
        }

        // Planetary view: render entire world
        {
            int pw = static_cast<int>(WORLD_W) * TilePixels::SIZE;
            int ph = static_cast<int>(WORLD_H) * TilePixels::SIZE;
            SDL_Surface* surf =
                SDL_CreateRGBSurfaceWithFormat(0, pw, ph, 32, SDL_PIXELFORMAT_RGBA32);
            if (surf) {
                SDL_LockSurface(surf);
                // Render in strips to avoid huge single allocation
                for (int strip_y = 0; strip_y < static_cast<int>(WORLD_H); strip_y += 16) {
                    int strip_h = std::min(16, static_cast<int>(WORLD_H) - strip_y);
                    render_tile_patch(terrain, 0, strip_y, static_cast<int>(WORLD_W), strip_h, seed,
                                      static_cast<uint32_t*>(surf->pixels) +
                                          strip_y * TilePixels::SIZE * (surf->pitch / 4),
                                      surf->pitch / 4, pr.env.water_level);
                }
                SDL_UnlockSurface(surf);
                std::string fname = "tiles_" + std::string(pr.name) + "_planetary.bmp";
                SDL_SaveBMP(surf, fname.c_str());
                SDL_FreeSurface(surf);
                std::cout << "  Saved: " << fname << " (full world " << pw << "x" << ph << "px)\n";
            }
        }

        SDL_Quit();
        return 0;
    }

    if (argc >= 2 && std::string(argv[1]) == "--headless-all") {
        int days = (argc >= 3) ? std::atoi(argv[2]) : 30;
        if (days <= 0)
            days = 30;
        for (int p = 0; p < NUM_PRESETS; ++p) {
            run_headless(days, p);
            std::cout << "\n";
        }
        return 0;
    }
    if (argc >= 2 && std::string(argv[1]) == "--headless") {
        int days = (argc >= 3) ? std::atoi(argv[2]) : 30;
        if (days <= 0)
            days = 30;
        int preset = 0;
        if (argc >= 4) {
            // Try name match first, then numeric index
            std::string arg = argv[3];
            bool found = false;
            for (int p = 0; p < NUM_PRESETS; ++p) {
                if (arg == PRESETS[p].name) {
                    preset = p;
                    found = true;
                    break;
                }
            }
            if (!found)
                preset = std::atoi(argv[3]);
        }
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
    bool show_hud = true;   // H key: toggle all info panels
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
        int fw, fh;
        SDL_GetWindowSize(window, &fw, &fh);
        cam.fit_world(WORLD_W, WORLD_H, Renderer::TILE_SIZE, fw, fh);
        tile_renderer.bake_terrain_cache(terrain, preset.seed, preset.env.water_level);
        bake_current_weather();
    };

    generate_current();
    overlay = OverlayMode::Temperature;  // Start with temperature overlay visible

    // UI button state
    ButtonRect prev_btn = {0, 0, 0, 0};
    ButtonRect next_btn = {0, 0, 0, 0};
    ButtonRect bake_btn = {0, 0, 0, 0};
    ButtonRect overlay_btn = {0, 0, 0, 0};

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
                        } else if (overlay_btn.contains(mx, my)) {
                            int next = (static_cast<int>(overlay) + 1) %
                                       static_cast<int>(OverlayMode::COUNT);
                            overlay = static_cast<OverlayMode>(next);
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
                        case SDLK_BACKQUOTE:
                            overlay = OverlayMode::PrecipBudget;
                            break;
                        case SDLK_F1:
                            overlay = OverlayMode::UpperWind;
                            break;
                        case SDLK_F2:
                            overlay = OverlayMode::Stability;
                            break;
                        case SDLK_F3:
                            overlay = OverlayMode::Aridity;
                            break;
                        case SDLK_F4:
                            overlay = OverlayMode::Groundwater;
                            break;
                        case SDLK_F5:
                            overlay = OverlayMode::Discharge;
                            break;
                        case SDLK_F6:
                            overlay = OverlayMode::Geology;
                            break;
                        case SDLK_F7:
                            overlay = OverlayMode::SoilTextureOverlay;
                            break;

                        // Dynamics: play/pause with T, speed with ,/.
                        case SDLK_t:
                            dynamics.paused = !dynamics.paused;
                            std::cout << (dynamics.paused ? "[DYNAMICS] Paused\n"
                                                          : "[DYNAMICS] Running\n");
                            break;
                        case SDLK_PERIOD:
                            dynamics.time_scale = std::min(dynamics.time_scale * 2.0f, 32.0f);
                            std::cout << "[DYNAMICS] Speed: " << dynamics.time_scale << "x\n";
                            break;
                        case SDLK_COMMA:
                            dynamics.time_scale = std::max(dynamics.time_scale * 0.5f, 0.25f);
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

                        // HUD toggle
                        case SDLK_h:
                            show_hud = !show_hud;
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
        // Keep camera within world bounds
        {
            int cw, ch;
            SDL_GetWindowSize(window, &cw, &ch);
            cam.clamp_to_world(WORLD_W, WORLD_H, Renderer::TILE_SIZE, cw, ch);
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
                std::cout << std::fixed << std::setprecision(3) << "[DAY " << std::setprecision(1)
                          << dynamics.elapsed_days << "] "
                          << "atmo: T=" << std::setprecision(1) << as.T_min << "/" << as.T_mean
                          << "/" << as.T_max << "  q=" << std::setprecision(3) << as.q_min << "/"
                          << as.q_mean << "/" << as.q_max << "  cloud=" << as.cloud_mean << "(max "
                          << as.cloud_max << ")"
                          << "  precip=" << as.precip_mean << "(max " << as.precip_max << ")"
                          << "  wind=" << std::setprecision(2) << as.wind_mean << "(max "
                          << as.wind_max << ")"
                          << "  P=" << std::setprecision(0) << as.p_min << "-" << as.p_max
                          << "  rain=" << as.raining_cells << " cloudy=" << as.cloudy_cells
                          << "\n        dyn: sw=" << std::setprecision(4) << ds.surface_water_mean
                          << "(max " << ds.surface_water_max << ")"
                          << "  soil=" << ds.soil_moisture_mean << "(max " << ds.soil_moisture_max
                          << ")"
                          << "  snow=" << ds.snow_mean << "(max " << ds.snow_max << ")"
                          << "  snow_tiles=" << ds.snow_tiles << "  flooded=" << ds.flooded_tiles
                          << "\n        hydro: gw=" << std::setprecision(4) << ds.groundwater_mean
                          << "(max " << ds.groundwater_max << ")"
                          << "  discharge=" << ds.discharge_mean << "(max " << ds.discharge_max
                          << ")"
                          << "\n        aridity: mean=" << std::setprecision(3) << ds.aridity_mean
                          << " min=" << ds.aridity_min << " max=" << ds.aridity_max
                          << "  arid=" << ds.arid_tiles << " humid=" << ds.humid_tiles
                          << "\n        inv: H2O=" << std::setprecision(1) << as.total_atmo_water
                          << "  CFL=" << std::setprecision(2) << as.max_courant
                          << (as.max_courant > 1.0f ? " VIOLATION" : "")
                          << "  corr=" << std::setprecision(4) << as.water_correction << "\n";
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
                          << " P=" << std::setprecision(2) << show_p << " M=" << show_m;

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

        // Full-frame clear: reset clip, clear, then overdraw with opaque rect
        // (SDL_RenderClear alone can fail on some WSL2/GPU backends)
        SDL_RenderSetClipRect(sdl_renderer, nullptr);
        SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(sdl_renderer, 20, 20, 30, 255);
        SDL_RenderClear(sdl_renderer);
        SDL_Rect fullscreen = {0, 0, win_w, win_h};
        SDL_RenderFillRect(sdl_renderer, &fullscreen);
        SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);

        bool overlay_active = weather_baked && overlay != OverlayMode::None;
        tile_renderer.render_terrain(terrain, cam, win_w, win_h,
                                     weather_baked ? &dynamics : nullptr, overlay_active);

        // Weather overlays (only if baked)
        if (overlay_active) {
            // Dynamic overlays
            if (overlay == OverlayMode::SurfaceWater || overlay == OverlayMode::SoilMoisture ||
                overlay == OverlayMode::SnowDepth || overlay == OverlayMode::Aridity ||
                overlay == OverlayMode::Groundwater || overlay == OverlayMode::Discharge) {
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

        int mx, my;
        SDL_GetMouseState(&mx, &my);

        if (show_hud) {
            if (weather_baked) {
                constexpr int PAD = 8;
                int stats_w = render_weather_stats(sdl_renderer, weather_stats, win_w, win_h, mx, my);
                render_overlay_legend(sdl_renderer, overlay, win_w, win_h,
                                      stats_w + PAD, /*show_info=*/true);
                render_dynamic_legend(sdl_renderer, dyn_stats, atmo_stats, dynamics.elapsed_days,
                                      dynamics.time_scale, dynamics.paused, overlay, win_w, win_h,
                                      atmosphere.time_of_day, atmosphere.day_of_year, mx, my);
            }

            // Buttons (top area)
            {
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
                bx += bake_btn.w + BTN_GAP + 20;

                // Overlay cycle button
                char ov_label[64];
                std::snprintf(ov_label, sizeof(ov_label), "OVERLAY: %s", overlay_name(overlay));
                bool ov_hov = overlay_btn.w > 0 && overlay_btn.contains(mx, my);
                overlay_btn = render_status_chip(sdl_renderer, bx, BTN_MARGIN, ov_label, ov_hov);
            }

            render_controls_hint(sdl_renderer, win_w, win_h);
        } else {
            // HUD hidden — show only the overlay legend at bottom-left, no info panel
            if (weather_baked) {
                render_overlay_legend(sdl_renderer, overlay, win_w, win_h,
                                      0, /*show_info=*/false);
            }
        }

        // Tile inspector — always visible regardless of HUD toggle
        if (weather_baked && mx >= 0 && my >= 0) {
            int tile_x, tile_y;
            cam.screen_to_tile(mx, my, win_w, win_h, Renderer::TILE_SIZE, tile_x, tile_y);
            if (tile_x >= 0 && tile_y >= 0 && tile_x < static_cast<int>(terrain.width) &&
                tile_y < static_cast<int>(terrain.height)) {
                render_tile_inspector(sdl_renderer, tile_x, tile_y, mx, my, terrain, dynamics,
                                      atmosphere, win_w, win_h);
            }
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
