#pragma once

#include <SDL.h>

#include <array>
#include <cstdint>

#include "atmosphere.h"
#include "dynamics.h"
#include "terrain_gen.h"
#include "weather.h"

namespace sandbox {

// ── Color mapping functions ─────────────────────────────────────────────────

SDL_Color temperature_color(float temp_c);       // Blue→white→red (-20C to 40C)
SDL_Color precipitation_color(float precip);     // Brown→green→blue (0 to 1)
SDL_Color moisture_color(float moisture);        // Brown→green (0 to 1)
SDL_Color evaporation_color(float evap);         // Yellow→red (0 to 1)
SDL_Color storminess_color(float storm);         // Gray→purple (0 to 1)
SDL_Color surface_water_color(float sw);         // Tan→cyan→blue (0 to 1)
SDL_Color soil_moisture_color(float sm);         // Brown→dark green (0 to 1)
SDL_Color snow_depth_color(float snow);          // Transparent→white (0 to 1)
SDL_Color precip_budget_color(float budget);     // Brown→green→white (0 to 2)
SDL_Color stability_color(float stab);           // Red (unstable) → gray → blue (inversion)
SDL_Color aridity_color(float aridity);          // Red (desert) → yellow → green → blue (humid)
SDL_Color groundwater_color(float gw);           // Tan → cyan → blue (0 to soil_depth*porosity)
SDL_Color discharge_color(float discharge);      // Black → cyan → white (log-scaled flow)
SDL_Color geology_color(RockType rock);          // Categorical bedrock colors
SDL_Color soil_texture_color(SoilTexture soil);  // Categorical soil colors

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
                           int win_w, int win_h, int mouse_x = -1, int mouse_y = -1);

void render_dynamic_legend(SDL_Renderer* renderer, const DynamicStats& stats,
                           const AtmosphereStats& atmo_stats, float elapsed_days, float time_scale,
                           bool paused, OverlayMode overlay, int win_w, int win_h,
                           float time_of_day = 12.0f, float day_of_year = 80.0f, int mouse_x = -1,
                           int mouse_y = -1);

void render_grid(SDL_Renderer* renderer, uint32_t world_w, uint32_t world_h, const Camera& cam,
                 int win_w, int win_h, int tile_size);

void render_tile_inspector(SDL_Renderer* renderer, int tile_x, int tile_y, int mouse_x, int mouse_y,
                           const Terrain& terrain, const DynamicState& dynamics,
                           const AtmosphereState& atmosphere, int win_w, int win_h);

// ── Bitmap font ─────────────────────────────────────────────────────────────

void draw_text(SDL_Renderer* renderer, int x, int y, const char* text, int scale, uint8_t r,
               uint8_t g, uint8_t b);

int text_pixel_width(const char* text, int scale);

}  // namespace sandbox
