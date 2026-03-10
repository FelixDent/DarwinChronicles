#pragma once

#include <SDL.h>

#include <array>
#include <cstdint>
#include <string>

#include "terrain_gen.h"

namespace sandbox {

// Forward-declare text utilities (defined in telemetry.cpp, used by renderer.cpp)
void draw_text(SDL_Renderer* renderer, int x, int y, const char* text, int scale, uint8_t r,
               uint8_t g, uint8_t b);
int text_pixel_width(const char* text, int scale);

// ── Overlay modes ───────────────────────────────────────────────────────────

enum class OverlayMode {
    None,
    Continental,
    Slope,
    ElevBand,
    DistOcean,
    Roughness,
    Aspect,
    Geology,
    SoilTexture,

    COUNT
};

const char* overlay_name(OverlayMode mode);

// ── Color mapping functions ─────────────────────────────────────────────────

SDL_Color continental_color(float h);
SDL_Color slope_color(float s);
SDL_Color elevband_color(ElevBand band);
SDL_Color dist_color(float dist, float max_dist);
SDL_Color aspect_color(float aspect);
SDL_Color erodibility_color(float erodibility);
SDL_Color soil_depth_color(float depth);

// ── Terrain statistics ──────────────────────────────────────────────────────

struct TerrainStats {
    uint32_t total_tiles = 0;
    uint32_t ocean_tiles = 0;
    uint32_t land_tiles = 0;
    std::array<uint32_t, 4> band_counts{};  // Water, Lowland, Hills, Mountains

    float height_min = 0.0f, height_max = 0.0f, height_mean = 0.0f, height_stddev = 0.0f;
    float slope_min = 0.0f, slope_max = 0.0f, slope_mean = 0.0f, slope_stddev = 0.0f;
    float dist_ocean_max = 0.0f;
    float roughness_min = 0.0f, roughness_max = 0.0f, roughness_mean = 0.0f;

    // Geology stats
    float erodibility_mean = 0.0f;
    float soil_depth_mean = 0.0f;
};

TerrainStats compute_stats(const Terrain& world);
void print_stats(const TerrainStats& stats);

// ── FPS counter ─────────────────────────────────────────────────────────────

class FPSCounter {
public:
    void tick();
    float fps() const { return fps_; }

private:
    static constexpr int WINDOW = 60;
    std::array<uint32_t, WINDOW> frame_times_{};
    int index_ = 0;
    int count_ = 0;
    float fps_ = 0.0f;
};

// ── Overlay rendering ───────────────────────────────────────────────────────

struct Camera;

void render_overlay(SDL_Renderer* renderer, const Terrain& world, const TerrainStats& stats,
                    const Camera& cam, int win_w, int win_h, OverlayMode mode, int tile_size);

void render_grid(SDL_Renderer* renderer, const Terrain& world, const Camera& cam, int win_w,
                 int win_h, int tile_size);

// ── Legend ───────────────────────────────────────────────────────────────────

void render_legend(SDL_Renderer* renderer, const TerrainStats& stats, int win_w, int win_h);

void render_overlay_legend(SDL_Renderer* renderer, const TerrainStats& stats, OverlayMode mode,
                           int win_w, int win_h);

// ── UI Buttons ──────────────────────────────────────────────────────────────

struct ButtonRect {
    int x, y, w, h;
    bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

ButtonRect render_button(SDL_Renderer* renderer, int x, int y, const char* label, bool hovered);

}  // namespace sandbox
