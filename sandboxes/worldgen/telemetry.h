#pragma once

#include "world_gen.h"

#include <SDL.h>

#include <array>
#include <cstdint>
#include <string>

namespace sandbox {

// ── Overlay modes ───────────────────────────────────────────────────────────

enum class OverlayMode {
    None,
    Height,
    Temperature,
    Moisture,
    Toxicity,
    BiomeColor,
    EffectiveMoisture,

    COUNT
};

const char* overlay_name(OverlayMode mode);

// ── Color mapping functions ─────────────────────────────────────────────────

// Returns RGBA color for overlay value
SDL_Color height_color(float h);
SDL_Color temperature_color(float temp_K);
SDL_Color moisture_color(float m);
SDL_Color toxicity_color(float t);
SDL_Color effective_moisture_color(float em);

// ── World statistics ────────────────────────────────────────────────────────

struct WorldStats {
    std::array<uint32_t, static_cast<size_t>(Biome::COUNT)> biome_counts{};
    uint32_t total_tiles = 0;

    float height_min = 0.0f, height_max = 0.0f, height_mean = 0.0f, height_stddev = 0.0f;
    float temp_min = 0.0f, temp_max = 0.0f, temp_mean = 0.0f, temp_stddev = 0.0f;
    float moisture_min = 0.0f, moisture_max = 0.0f, moisture_mean = 0.0f, moisture_stddev = 0.0f;
    float toxicity_min = 0.0f, toxicity_max = 0.0f, toxicity_mean = 0.0f, toxicity_stddev = 0.0f;
};

WorldStats compute_stats(const World& world);
void print_stats(const WorldStats& stats);

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

void render_overlay(SDL_Renderer* renderer, const World& world, const Camera& cam, int win_w,
                    int win_h, OverlayMode mode, int tile_size);

void render_grid(SDL_Renderer* renderer, const World& world, const Camera& cam, int win_w,
                 int win_h, int tile_size);

// ── Legend ───────────────────────────────────────────────────────────────────

void render_legend(SDL_Renderer* renderer, const WorldStats& stats, int win_w, int win_h);

// ── UI Buttons ──────────────────────────────────────────────────────────────

struct ButtonRect {
    int x, y, w, h;
    bool contains(int px, int py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

ButtonRect render_button(SDL_Renderer* renderer, int x, int y, const char* label, bool hovered);

}  // namespace sandbox
