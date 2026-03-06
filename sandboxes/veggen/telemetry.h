#pragma once

#include <SDL.h>

#include <array>
#include <cstdint>

#include "vegetation.h"

namespace veggen {

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

// ── Text rendering ──────────────────────────────────────────────────────────

void draw_text(SDL_Renderer* renderer, int x, int y, const char* text, int scale, uint8_t r,
               uint8_t g, uint8_t b);

int text_pixel_width(const char* text, int scale);

void draw_panel_bg(SDL_Renderer* renderer, int x, int y, int w, int h);

// ── Vegetation-specific colors ──────────────────────────────────────────────

SDL_Color health_color(PlantHealth health);
SDL_Color soil_moisture_color(float moisture);
SDL_Color temperature_color(float temp_c);
SDL_Color elevation_color(float elev01);
SDL_Color surface_water_color(float surface_water);
SDL_Color canopy_color(float canopy_cover);

}  // namespace veggen
