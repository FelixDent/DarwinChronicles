#pragma once

#include <SDL.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include "terrain_gen.h"
#include "tile_texture.h"

namespace sandbox {

// ── Camera ──────────────────────────────────────────────────────────────────

struct Camera {
    float x = 0.0f;  // World-space center X
    float y = 0.0f;  // World-space center Y
    float zoom = 1.0f;

    static constexpr float MIN_ZOOM = 0.25f;
    static constexpr float MAX_ZOOM = 4.0f;
    static constexpr float PAN_SPEED = 400.0f;  // pixels/sec at zoom 1.0

    void pan(float dx, float dy, float dt);
    void zoom_at(float screen_x, float screen_y, float factor, int win_w, int win_h);
    void center_on_world(uint32_t world_w, uint32_t world_h, int tile_size);
    void fit_world(uint32_t world_w, uint32_t world_h, int tile_size, int win_w, int win_h);

    // Convert world tile coords to screen coords
    SDL_Rect tile_to_screen(int tile_x, int tile_y, int tile_size, int win_w, int win_h) const;

    // Convert screen coords to world tile coords
    void screen_to_tile(int screen_x, int screen_y, int win_w, int win_h, int tile_size,
                        int& tile_x, int& tile_y) const;
};

// ── Terrain clipmap cache ────────────────────────────────────────────────────

struct TerrainCacheLevel {
    SDL_Texture* texture = nullptr;
    int tex_w = 0, tex_h = 0;
    float world_x0 = 0.0f;
    float world_y0 = 0.0f;
    float world_per_pixel = 1.0f;
    bool valid = false;
    uint32_t seed = 0;
};

// ── Renderer ────────────────────────────────────────────────────────────────

class Renderer {
public:
    static constexpr int TILE_SIZE = 16;

    void init(SDL_Renderer* sdl_renderer);
    void shutdown();

    // Bake terrain cache. Macro + meso baked synchronously; detail bakes in background.
    void bake_terrain_cache(const Terrain& world, uint32_t seed, float water_level = 0.45f);

    // Invalidate cache (e.g., after terrain regeneration or preset change)
    void invalidate_terrain_cache();

    // Check if detail level is still baking in background
    bool is_detail_baking() const { return detail_baking_.load(std::memory_order_relaxed); }

    void render_terrain(const Terrain& terrain, const Camera& cam, int win_w, int win_h);

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
    std::vector<uint32_t> detail_pixels_;
    int detail_tex_w_ = 0, detail_tex_h_ = 0;
    std::atomic<bool> detail_baking_{false};
    std::atomic<bool> detail_ready_{false};
    std::atomic<bool> detail_cancel_{false};

    void render_loading_screen(float progress, const char* stage);
    void finish_detail_bake();
};

}  // namespace sandbox
