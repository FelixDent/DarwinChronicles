#pragma once

#include <SDL.h>

#include <array>
#include <cstdint>

#include "atmosphere.h"
#include "weather.h"

namespace sandbox {

// ── Color mapping functions ─────────────────────────────────────────────────

SDL_Color temperature_color(float temp_c);    // Blue→white→red (-20C to 40C)
SDL_Color precipitation_color(float precip);  // Brown→green→blue (0 to 1)
SDL_Color moisture_color(float moisture);     // Brown→green (0 to 1)
SDL_Color evaporation_color(float evap);      // Yellow→red (0 to 1)
SDL_Color storminess_color(float storm);      // Gray→purple (0 to 1)
SDL_Color surface_water_color(float sw);      // Tan→cyan→blue (0 to 1)
SDL_Color soil_moisture_color(float sm);      // Brown→dark green (0 to 1)
SDL_Color snow_depth_color(float snow);       // Transparent→white (0 to 1)

// ── Wind direction name ─────────────────────────────────────────────────────

const char* wind_direction_name(float u, float v);

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

// ── Legend rendering ────────────────────────────────────────────────────────

struct Camera;
struct DynamicStats;
enum class OverlayMode;

void render_weather_legend(SDL_Renderer* renderer, const WeatherStats& stats, OverlayMode overlay,
                           int win_w, int win_h);

void render_dynamic_legend(SDL_Renderer* renderer, const DynamicStats& stats,
                           const AtmosphereStats& atmo_stats, float elapsed_days, float time_scale,
                           bool paused, OverlayMode overlay, int win_w, int win_h,
                           float time_of_day = 12.0f, float day_of_year = 80.0f);

void render_grid(SDL_Renderer* renderer, uint32_t world_w, uint32_t world_h, const Camera& cam,
                 int win_w, int win_h, int tile_size);

// ── Bitmap font ─────────────────────────────────────────────────────────────

void draw_text(SDL_Renderer* renderer, int x, int y, const char* text, int scale, uint8_t r,
               uint8_t g, uint8_t b);

int text_pixel_width(const char* text, int scale);

}  // namespace sandbox
