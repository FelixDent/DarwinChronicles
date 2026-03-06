#pragma once

#include <SDL.h>

#include <unordered_map>
#include <vector>

#include "plant_sprite.h"
#include "vegetation.h"

namespace veggen {

// ── Overlay modes ───────────────────────────────────────────────────────────

enum class OverlayMode { None = 0, Moisture, Temperature, Health, Elevation, SurfaceWater, Canopy, COUNT };

const char* overlay_name(OverlayMode mode);

// ── Sprite cache key ────────────────────────────────────────────────────────

struct SpriteCacheKey {
    uint32_t sprite_seed;
    GrowthPhase phase;
    PlantHealth health;

    bool operator==(const SpriteCacheKey& o) const {
        return sprite_seed == o.sprite_seed && phase == o.phase && health == o.health;
    }
};

struct SpriteCacheHash {
    size_t operator()(const SpriteCacheKey& k) const {
        return std::hash<uint32_t>()(k.sprite_seed) ^
               (std::hash<uint8_t>()(static_cast<uint8_t>(k.phase)) << 16) ^
               (std::hash<uint8_t>()(static_cast<uint8_t>(k.health)) << 24);
    }
};

// ── Renderer ────────────────────────────────────────────────────────────────

class Renderer {
public:
    static constexpr int TILE_PX = 128;

    void init(SDL_Renderer* sdl_renderer);
    void shutdown();

    void render_tiles(const SimState& sim, int win_w, int win_h);
    void render_plants(const SimState& sim, int win_w, int win_h);
    void render_overlay(const SimState& sim, int win_w, int win_h, OverlayMode mode);
    void render_grid(int win_w, int win_h);
    void render_ui(const SimState& sim, int win_w, int win_h, OverlayMode mode, bool show_fps,
                   float fps);

private:
    SDL_Renderer* renderer_ = nullptr;

    // Sprite texture cache with LRU eviction
    struct CacheEntry {
        SDL_Texture* tex = nullptr;
        uint32_t last_used_frame = 0;
    };
    std::unordered_map<SpriteCacheKey, CacheEntry, SpriteCacheHash> texture_cache_;
    uint32_t frame_counter_ = 0;
    static constexpr uint32_t CACHE_MAX_SIZE = 256;
    static constexpr uint32_t EVICT_STALE_FRAMES = 300;  // ~5 seconds at 60fps

    SDL_Texture* get_or_create_texture(const PlantTraits& traits, GrowthPhase phase,
                                       PlantHealth health);
    void evict_stale_textures();

public:
    void flush_cache();

private:
    // Reusable sort buffer to avoid per-frame allocation
    std::vector<size_t> sort_buffer_;

    // Grid layout: compute screen rect for a tile
    SDL_Rect tile_rect(int tx, int ty, int win_w, int win_h) const;
    int grid_offset_x(int win_w) const;
    int grid_offset_y(int win_h) const;
};

}  // namespace veggen
