#pragma once

#include <SDL.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include "atmosphere.h"
#include "dynamics.h"
#include "terrain_gen.h"
#include "weather.h"

namespace sandbox {

// ── Overlay modes ───────────────────────────────────────────────────────────

enum class OverlayMode {
    None,
    Temperature,
    Precipitation,
    Moisture,
    WindDirection,
    Evaporation,
    Storminess,
    MoistureBars,
    RainShadow,
    SurfaceWater,
    SoilMoisture,
    SnowDepth,
    PrecipBudget,
    UpperWind,
    Stability,
    Aridity,
    Groundwater,
    Discharge,
    Geology,
    SoilTextureOverlay,

    COUNT
};

const char* overlay_name(OverlayMode mode);

// ── Camera ──────────────────────────────────────────────────────────────────

struct Camera {
    float x = 0.0f;
    float y = 0.0f;
    float zoom = 1.0f;

    static constexpr float MIN_ZOOM = 0.25f;
    static constexpr float MAX_ZOOM = 4.0f;
    static constexpr float PAN_SPEED = 400.0f;

    void pan(float dx, float dy, float dt);
    void zoom_at(float screen_x, float screen_y, float factor, int win_w, int win_h);
    void center_on_world(uint32_t world_w, uint32_t world_h, int tile_size);
    void fit_world(uint32_t world_w, uint32_t world_h, int tile_size, int win_w, int win_h);
    void clamp_to_world(uint32_t world_w, uint32_t world_h, int tile_size, int win_w, int win_h);

    SDL_Rect tile_to_screen(int tile_x, int tile_y, int tile_size, int win_w, int win_h) const;
    void screen_to_tile(int screen_x, int screen_y, int win_w, int win_h, int tile_size,
                        int& tile_x, int& tile_y) const;
};

// ── Terrain clipmap cache ────────────────────────────────────────────────────
// Multi-resolution procedural terrain texture cache. Each level covers a
// screen-sized region at a different world-space resolution. Updated
// incrementally as the camera moves (only dirty strips regenerated).

struct TerrainCacheLevel {
    SDL_Texture* texture = nullptr;  // GPU texture, RGBA32
    int tex_w = 0, tex_h = 0;        // texture dimensions (pixels)
    float world_x0 = 0.0f;           // world-space left edge (tile coords)
    float world_y0 = 0.0f;           // world-space top edge
    float world_per_pixel = 1.0f;    // world-space units per output pixel
    bool valid = false;              // true if cache matches current camera
    uint32_t seed = 0;               // terrain seed used for generation
};

// ── Renderer ────────────────────────────────────────────────────────────────

class Renderer {
public:
    static constexpr int TILE_SIZE = 16;

    void init(SDL_Renderer* sdl_renderer);
    void shutdown();

    // Bake the macro (planetary) cache level once at startup.
    // Must be called after terrain generation, before first render.
    void bake_terrain_cache(const Terrain& world, uint32_t seed, float water_level = 0.45f);

    void render_terrain(const Terrain& world, const Camera& cam, int win_w, int win_h,
                        const DynamicState* dyn = nullptr, bool show_dynamic_tint = true);
    void render_weather_overlay(const Terrain& world, const ClimateData& climate, const Camera& cam,
                                int win_w, int win_h, OverlayMode mode,
                                const DynamicState* dyn = nullptr,
                                const AtmosphereState* atmo = nullptr);
    void render_dynamic_overlay(const Terrain& world, const DynamicState& dyn, const Camera& cam,
                                int win_w, int win_h, OverlayMode mode);
    void render_wind_arrows(const ClimateData& climate, const Camera& cam, int win_w, int win_h,
                            const AtmosphereState* atmo = nullptr);
    void render_moisture_bars(const Terrain& world, const ClimateData& climate, const Camera& cam,
                              int win_w, int win_h, const DynamicState* dyn = nullptr,
                              const AtmosphereState* atmo = nullptr);

    // Invalidate terrain cache (e.g., after terrain regeneration or preset change)
    void invalidate_terrain_cache();

    // Check if detail level is still baking in background
    bool is_detail_baking() const { return detail_baking_.load(std::memory_order_relaxed); }

private:
    SDL_Renderer* renderer_ = nullptr;

    // Terrain clipmap: L0 = macro, L1 = meso, L2 = detail
    TerrainCacheLevel cache_macro_;   // whole world (~4 px/tile)
    TerrainCacheLevel cache_meso_;    // whole world (~8 px/tile)
    TerrainCacheLevel cache_detail_;  // whole world (~16 px/tile)
    float cached_water_level_ = 0.45f;
    uint32_t cached_seed_ = 0;

    // Background detail bake state
    std::unique_ptr<std::thread> detail_thread_;
    std::vector<uint32_t> detail_pixels_;  // CPU buffer filled by bg thread
    int detail_tex_w_ = 0, detail_tex_h_ = 0;
    std::atomic<bool> detail_baking_{false};  // true while bg thread is working
    std::atomic<bool> detail_ready_{false};   // true when pixels are ready for upload
    std::atomic<bool> detail_cancel_{false};  // set true to abort bg thread early

    void render_loading_screen(float progress, const char* stage);
    void finish_detail_bake();  // join thread + upload texture (called from render)
};

// ── UI Buttons ──────────────────────────────────────────────────────────────

struct ButtonRect {
    int x, y, w, h;
    bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

ButtonRect render_button(SDL_Renderer* renderer, int x, int y, const char* label, bool hovered);
ButtonRect render_status_chip(SDL_Renderer* renderer, int x, int y, const char* label,
                              bool hovered);

}  // namespace sandbox
