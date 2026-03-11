#include "renderer.h"

#include <algorithm>
#include <cmath>

#include "telemetry.h"

namespace {

// Flat-color fallback (used before cache is baked)
std::array<uint8_t, 3> terrain_color(const sandbox::TerrainTile& tile) {
    if (tile.is_ocean) {
        float depth = 1.0f - tile.elev01;
        uint8_t r = static_cast<uint8_t>(10.0f + depth * 5.0f);
        uint8_t g = static_cast<uint8_t>(30.0f + depth * 30.0f);
        uint8_t b = static_cast<uint8_t>(70.0f + (1.0f - depth) * 80.0f);
        return {r, g, b};
    }
    switch (tile.band) {
        case sandbox::ElevBand::Lowland: {
            float t = tile.elev01;
            uint8_t r = static_cast<uint8_t>(180.0f + t * 30.0f);
            uint8_t g = static_cast<uint8_t>(160.0f + t * 20.0f);
            uint8_t b = static_cast<uint8_t>(110.0f + t * 10.0f);
            return {r, g, b};
        }
        case sandbox::ElevBand::Hills: {
            float t = tile.elev01;
            uint8_t r = static_cast<uint8_t>(140.0f + t * 20.0f);
            uint8_t g = static_cast<uint8_t>(125.0f + t * 10.0f);
            uint8_t b = static_cast<uint8_t>(80.0f + t * 10.0f);
            return {r, g, b};
        }
        case sandbox::ElevBand::Mountains: {
            float t = std::clamp((tile.elev01 - 0.7f) / 0.3f, 0.0f, 1.0f);
            uint8_t v = static_cast<uint8_t>(130.0f + t * 110.0f);
            return {v, v, static_cast<uint8_t>(v + 5)};
        }
        case sandbox::ElevBand::Water:
            return {70, 140, 200};
        default:
            return {100, 100, 100};
    }
}

}  // namespace

namespace sandbox {

// ── Camera ──────────────────────────────────────────────────────────────────

void Camera::pan(float dx, float dy, float dt) {
    float speed = PAN_SPEED / zoom * dt;
    x += dx * speed;
    y += dy * speed;
}

void Camera::zoom_at(float screen_x, float screen_y, float factor, int win_w, int win_h) {
    float world_x = x + (screen_x - static_cast<float>(win_w) / 2.0f) / zoom;
    float world_y = y + (screen_y - static_cast<float>(win_h) / 2.0f) / zoom;

    zoom = std::clamp(zoom * factor, MIN_ZOOM, MAX_ZOOM);

    x = world_x - (screen_x - static_cast<float>(win_w) / 2.0f) / zoom;
    y = world_y - (screen_y - static_cast<float>(win_h) / 2.0f) / zoom;
}

void Camera::center_on_world(uint32_t world_w, uint32_t world_h, int tile_size) {
    x = static_cast<float>(world_w) * static_cast<float>(tile_size) / 2.0f;
    y = static_cast<float>(world_h) * static_cast<float>(tile_size) / 2.0f;
}

void Camera::fit_world(uint32_t world_w, uint32_t world_h, int tile_size, int win_w, int win_h) {
    center_on_world(world_w, world_h, tile_size);
    float world_px_w = static_cast<float>(world_w) * static_cast<float>(tile_size);
    float world_px_h = static_cast<float>(world_h) * static_cast<float>(tile_size);
    float zoom_x = static_cast<float>(win_w) / world_px_w;
    float zoom_y = static_cast<float>(win_h) / world_px_h;
    zoom = std::clamp(std::min(zoom_x, zoom_y) * 0.95f, MIN_ZOOM, MAX_ZOOM);
}

SDL_Rect Camera::tile_to_screen(int tile_x, int tile_y, int tile_size, int win_w, int win_h) const {
    float world_px = static_cast<float>(tile_x * tile_size);
    float world_py = static_cast<float>(tile_y * tile_size);

    float screen_x = (world_px - x) * zoom + static_cast<float>(win_w) / 2.0f;
    float screen_y = (world_py - y) * zoom + static_cast<float>(win_h) / 2.0f;
    float size = static_cast<float>(tile_size) * zoom;

    return SDL_Rect{
        static_cast<int>(screen_x),
        static_cast<int>(screen_y),
        static_cast<int>(std::ceil(size)),
        static_cast<int>(std::ceil(size)),
    };
}

void Camera::screen_to_tile(int screen_x, int screen_y, int win_w, int win_h, int tile_size,
                            int& tile_x, int& tile_y) const {
    float world_px = x + (static_cast<float>(screen_x) - static_cast<float>(win_w) / 2.0f) / zoom;
    float world_py = y + (static_cast<float>(screen_y) - static_cast<float>(win_h) / 2.0f) / zoom;
    tile_x = static_cast<int>(std::floor(world_px / static_cast<float>(tile_size)));
    tile_y = static_cast<int>(std::floor(world_py / static_cast<float>(tile_size)));
}

// ── Renderer ────────────────────────────────────────────────────────────────

void Renderer::init(SDL_Renderer* sdl_renderer) {
    renderer_ = sdl_renderer;
}

void Renderer::shutdown() {
    invalidate_terrain_cache();
    renderer_ = nullptr;
}

void Renderer::invalidate_terrain_cache() {
    // Signal background thread to stop, then join it
    detail_cancel_.store(true, std::memory_order_relaxed);
    if (detail_thread_ && detail_thread_->joinable()) {
        detail_thread_->join();
    }
    detail_thread_.reset();
    detail_cancel_.store(false, std::memory_order_relaxed);
    detail_baking_.store(false, std::memory_order_relaxed);
    detail_ready_.store(false, std::memory_order_relaxed);
    detail_pixels_.clear();
    detail_pixels_.shrink_to_fit();

    if (cache_macro_.texture) {
        SDL_DestroyTexture(cache_macro_.texture);
        cache_macro_.texture = nullptr;
    }
    if (cache_meso_.texture) {
        SDL_DestroyTexture(cache_meso_.texture);
        cache_meso_.texture = nullptr;
    }
    if (cache_detail_.texture) {
        SDL_DestroyTexture(cache_detail_.texture);
        cache_detail_.texture = nullptr;
    }
    cache_macro_.valid = false;
    cache_meso_.valid = false;
    cache_detail_.valid = false;
}

void Renderer::render_loading_screen(float progress, const char* stage) {
    if (!renderer_)
        return;

    int win_w, win_h;
    SDL_GetRendererOutputSize(renderer_, &win_w, &win_h);

    SDL_SetRenderDrawColor(renderer_, 11, 16, 28, 255);
    SDL_RenderClear(renderer_);

    int cx = win_w / 2;
    int cy = win_h / 2;

    // Title
    const char* title = "Baking terrain textures...";
    int tw = text_pixel_width(title, 2);
    draw_text(renderer_, cx - tw / 2, cy - 40, title, 2, 215, 220, 227);

    // Stage label
    int sw = text_pixel_width(stage, 1);
    draw_text(renderer_, cx - sw / 2, cy - 10, stage, 1, 154, 166, 178);

    // Progress bar
    constexpr int BAR_W = 300, BAR_H = 16;
    int bar_x = cx - BAR_W / 2;
    int bar_y = cy + 10;

    SDL_SetRenderDrawColor(renderer_, 42, 53, 66, 255);
    SDL_Rect outline = {bar_x, bar_y, BAR_W, BAR_H};
    SDL_RenderDrawRect(renderer_, &outline);

    int fill_w = static_cast<int>(progress * static_cast<float>(BAR_W - 2));
    if (fill_w > 0) {
        SDL_SetRenderDrawColor(renderer_, 110, 195, 240, 255);
        SDL_Rect fill = {bar_x + 1, bar_y + 1, fill_w, BAR_H - 2};
        SDL_RenderFillRect(renderer_, &fill);
    }

    char pct_text[16];
    snprintf(pct_text, sizeof(pct_text), "%d%%", static_cast<int>(progress * 100.0f));
    int pw = text_pixel_width(pct_text, 1);
    draw_text(renderer_, cx - pw / 2, bar_y + BAR_H + 8, pct_text, 1, 215, 220, 227);

    SDL_RenderPresent(renderer_);
}

void Renderer::finish_detail_bake() {
    if (!detail_ready_.load(std::memory_order_acquire))
        return;

    if (detail_thread_ && detail_thread_->joinable()) {
        detail_thread_->join();
    }
    detail_thread_.reset();

    if (cache_detail_.texture)
        SDL_DestroyTexture(cache_detail_.texture);
    cache_detail_.texture = SDL_CreateTexture(
        renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, detail_tex_w_, detail_tex_h_);
    if (cache_detail_.texture) {
        SDL_UpdateTexture(cache_detail_.texture, nullptr, detail_pixels_.data(), detail_tex_w_ * 4);
        cache_detail_.tex_w = detail_tex_w_;
        cache_detail_.tex_h = detail_tex_h_;
        cache_detail_.world_x0 = 0.0f;
        cache_detail_.world_y0 = 0.0f;
        cache_detail_.world_per_pixel = 1.0f / 16.0f;
        cache_detail_.seed = cached_seed_;
        cache_detail_.valid = true;
    }

    detail_pixels_.clear();
    detail_pixels_.shrink_to_fit();
    detail_baking_.store(false, std::memory_order_relaxed);
    detail_ready_.store(false, std::memory_order_relaxed);
}

void Renderer::bake_terrain_cache(const Terrain& world, uint32_t seed, float water_level) {
    // Ensure template atlas is generated before any rendering
    {
        const auto& atlas = get_template_atlas();
        if (!atlas.valid || atlas.seed != seed) {
            reset_template_atlas(seed);
        }
    }

    // Cancel any in-progress background bake
    detail_cancel_.store(true, std::memory_order_relaxed);
    if (detail_thread_ && detail_thread_->joinable()) {
        detail_thread_->join();
    }
    detail_thread_.reset();
    detail_cancel_.store(false, std::memory_order_relaxed);
    detail_baking_.store(false, std::memory_order_relaxed);
    detail_ready_.store(false, std::memory_order_relaxed);

    cached_seed_ = seed;
    cached_water_level_ = water_level;

    // Macro + meso baked synchronously (fast). Detail bakes in background.
    // Progress tracks tile rows (what the callback reports), not pixel rows.
    int world_h = static_cast<int>(world.height);
    int total_tile_rows = world_h * 2;  // macro + meso each iterate world_h tile rows
    int tile_rows_done = 0;

    auto bake_level = [&](TerrainCacheLevel& cache, int ppt, const char* stage) {
        int tex_w = static_cast<int>(world.width) * ppt;
        int tex_h = static_cast<int>(world.height) * ppt;
        float wpp = 1.0f / static_cast<float>(ppt);

        std::vector<uint32_t> pixels(static_cast<size_t>(tex_w) * static_cast<size_t>(tex_h));

        int base_rows = tile_rows_done;
        auto progress = [&](int row, int /*row_total*/) {
            float pct = static_cast<float>(base_rows + row) / static_cast<float>(total_tile_rows);
            render_loading_screen(pct, stage);
        };

        render_terrain_region(world, 0.0f, 0.0f, wpp, tex_w, tex_h, seed, pixels.data(), tex_w,
                              water_level, progress);
        tile_rows_done += world_h;

        if (cache.texture)
            SDL_DestroyTexture(cache.texture);
        cache.texture = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32,
                                          SDL_TEXTUREACCESS_STATIC, tex_w, tex_h);
        if (!cache.texture) {
            cache.valid = false;
            return;
        }
        SDL_UpdateTexture(cache.texture, nullptr, pixels.data(), tex_w * 4);
        cache.tex_w = tex_w;
        cache.tex_h = tex_h;
        cache.world_x0 = 0.0f;
        cache.world_y0 = 0.0f;
        cache.world_per_pixel = wpp;
        cache.seed = seed;
        cache.valid = true;
    };

    render_loading_screen(0.0f, "Preparing...");

    bake_level(cache_macro_, 4, "Macro level (4 px/tile)");
    bake_level(cache_meso_, 8, "Meso level (8 px/tile)");

    // Detail: bake in background thread
    cache_detail_.valid = false;
    detail_tex_w_ = static_cast<int>(world.width) * 16;
    detail_tex_h_ = static_cast<int>(world.height) * 16;
    detail_pixels_.resize(static_cast<size_t>(detail_tex_w_) * detail_tex_h_);

    detail_baking_.store(true, std::memory_order_relaxed);
    detail_ready_.store(false, std::memory_order_relaxed);
    detail_cancel_.store(false, std::memory_order_relaxed);

    float wpp_detail = 1.0f / 16.0f;
    detail_thread_ = std::make_unique<std::thread>([this, &world, wpp_detail, seed, water_level]() {
        render_terrain_region(world, 0.0f, 0.0f, wpp_detail, detail_tex_w_, detail_tex_h_, seed,
                              detail_pixels_.data(), detail_tex_w_, water_level, nullptr,
                              &detail_cancel_);
        if (!detail_cancel_.load(std::memory_order_relaxed)) {
            detail_ready_.store(true, std::memory_order_release);
        }
    });
}

void Renderer::render_terrain(const Terrain& terrain, const Camera& cam, int win_w, int win_h) {
    // Check for completed background detail bake
    if (detail_ready_.load(std::memory_order_acquire)) {
        finish_detail_bake();
    }

    // Choose cache level based on zoom
    float tile_screen = static_cast<float>(TILE_SIZE) * cam.zoom;

    TerrainCacheLevel* active_cache = nullptr;
    if (tile_screen > 24.0f && cache_detail_.valid) {
        active_cache = &cache_detail_;
    } else if (tile_screen > 6.0f && cache_meso_.valid) {
        active_cache = &cache_meso_;
    }
    if (!active_cache && cache_macro_.valid) {
        active_cache = &cache_macro_;
    }

    if (active_cache && active_cache->texture) {
        float wpp = active_cache->world_per_pixel;
        float screen_x0 =
            (active_cache->world_x0 * static_cast<float>(TILE_SIZE) - cam.x) * cam.zoom +
            static_cast<float>(win_w) / 2.0f;
        float screen_y0 =
            (active_cache->world_y0 * static_cast<float>(TILE_SIZE) - cam.y) * cam.zoom +
            static_cast<float>(win_h) / 2.0f;
        float px_scale = wpp * static_cast<float>(TILE_SIZE) * cam.zoom;
        int dst_w = static_cast<int>(static_cast<float>(active_cache->tex_w) * px_scale);
        int dst_h = static_cast<int>(static_cast<float>(active_cache->tex_h) * px_scale);

        SDL_Rect dst_rect = {static_cast<int>(screen_x0), static_cast<int>(screen_y0), dst_w,
                             dst_h};
        SDL_RenderCopy(renderer_, active_cache->texture, nullptr, &dst_rect);
    } else {
        // Fallback: flat color rectangles (no cache available yet)
        int min_tx, min_ty, max_tx, max_ty;
        cam.screen_to_tile(0, 0, win_w, win_h, TILE_SIZE, min_tx, min_ty);
        cam.screen_to_tile(win_w, win_h, win_w, win_h, TILE_SIZE, max_tx, max_ty);

        min_tx = std::max(min_tx - 1, 0);
        min_ty = std::max(min_ty - 1, 0);
        max_tx = std::min(max_tx + 1, static_cast<int>(terrain.width) - 1);
        max_ty = std::min(max_ty + 1, static_cast<int>(terrain.height) - 1);

        for (int ty = min_ty; ty <= max_ty; ++ty) {
            for (int tx = min_tx; tx <= max_tx; ++tx) {
                const TerrainTile& tile =
                    terrain.tile_at(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty));
                SDL_Rect dst = cam.tile_to_screen(tx, ty, TILE_SIZE, win_w, win_h);

                auto color = terrain_color(tile);
                SDL_SetRenderDrawColor(renderer_, color[0], color[1], color[2], 255);
                SDL_RenderFillRect(renderer_, &dst);
            }
        }
    }
}

}  // namespace sandbox
