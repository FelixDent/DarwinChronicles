#include "renderer.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "telemetry.h"
#include "tile_texture.h"

namespace {

// Terrain color fallback (used when no clipmap cache available)
std::array<uint8_t, 3> terrain_color(const sandbox::TerrainTile& tile) {
    if (tile.is_ocean) {
        float depth = 1.0f - tile.elev01;
        uint8_t r = static_cast<uint8_t>(10.0f + depth * 5.0f);
        uint8_t g = static_cast<uint8_t>(30.0f + depth * 30.0f);
        uint8_t b = static_cast<uint8_t>(70.0f + (1.0f - depth) * 80.0f);
        return {r, g, b};
    }
    switch (tile.band) {
        case sandbox::ElevBand::Water: {
            // Low-elevation non-ocean tile (basin)
            return {70, 140, 200};
        }
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
        default:
            return {100, 100, 100};
    }
}

}  // namespace

namespace sandbox {

// ── Overlay names ───────────────────────────────────────────────────────────

const char* overlay_name(OverlayMode mode) {
    switch (mode) {
        case OverlayMode::None:
            return "None";
        case OverlayMode::Temperature:
            return "Temperature";
        case OverlayMode::Precipitation:
            return "Precipitation";
        case OverlayMode::Moisture:
            return "Moisture";
        case OverlayMode::WindDirection:
            return "Wind";
        case OverlayMode::Evaporation:
            return "Evaporation";
        case OverlayMode::Storminess:
            return "Storminess";
        case OverlayMode::MoistureBars:
            return "MoistureBars";
        case OverlayMode::RainShadow:
            return "RainShadow";
        case OverlayMode::SurfaceWater:
            return "Surface Water";
        case OverlayMode::SoilMoisture:
            return "Soil Moisture";
        case OverlayMode::SnowDepth:
            return "Snow Depth";
        case OverlayMode::PrecipBudget:
            return "Precip Budget";
        case OverlayMode::UpperWind:
            return "Upper Wind";
        case OverlayMode::Stability:
            return "Stability";
        case OverlayMode::Aridity:
            return "Aridity";
        case OverlayMode::Groundwater:
            return "Groundwater";
        case OverlayMode::Discharge:
            return "Discharge";
        case OverlayMode::Geology:
            return "Geology";
        case OverlayMode::SoilTextureOverlay:
            return "Soil Texture";
        default:
            return "Unknown";
    }
}

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
    float margin = 0.95f;  // 5% margin
    float zx = static_cast<float>(win_w) / world_px_w * margin;
    float zy = static_cast<float>(win_h) / world_px_h * margin;
    zoom = std::clamp(std::min(zx, zy), MIN_ZOOM, MAX_ZOOM);
}

void Camera::clamp_to_world(uint32_t world_w, uint32_t world_h, int tile_size, int win_w,
                            int win_h) {
    float world_px_w = static_cast<float>(world_w) * static_cast<float>(tile_size);
    float world_px_h = static_cast<float>(world_h) * static_cast<float>(tile_size);
    float half_view_w = static_cast<float>(win_w) / (2.0f * zoom);
    float half_view_h = static_cast<float>(win_h) / (2.0f * zoom);
    // If world fits in view, center it; otherwise clamp to edges
    if (world_px_w <= half_view_w * 2.0f)
        x = world_px_w / 2.0f;
    else
        x = std::clamp(x, half_view_w, world_px_w - half_view_w);
    if (world_px_h <= half_view_h * 2.0f)
        y = world_px_h / 2.0f;
    else
        y = std::clamp(y, half_view_h, world_px_h - half_view_h);
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

    // Clear to panel background
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

    // Percentage
    char pct_text[16];
    snprintf(pct_text, sizeof(pct_text), "%d%%", static_cast<int>(progress * 100.0f));
    int pw = text_pixel_width(pct_text, 1);
    draw_text(renderer_, cx - pw / 2, bar_y + BAR_H + 8, pct_text, 1, 215, 220, 227);

    SDL_RenderPresent(renderer_);
}

void Renderer::finish_detail_bake() {
    if (!detail_ready_.load(std::memory_order_acquire))
        return;

    // Join the background thread
    if (detail_thread_ && detail_thread_->joinable()) {
        detail_thread_->join();
    }
    detail_thread_.reset();

    // Upload pixels to GPU texture
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

    // Only macro + meso are baked synchronously (fast — ~2.5M pixels).
    // Detail level (8.4M pixels) bakes in a background thread.
    // Progress tracks tile rows (what the callback reports), not pixel rows.
    int world_h = static_cast<int>(world.height);
    int total_tile_rows = world_h * 2;  // macro + meso each iterate world_h tile rows
    int tile_rows_done = 0;

    auto bake_level = [&](TerrainCacheLevel& cache, int ppt, const char* stage) {
        int tex_w = static_cast<int>(world.width) * ppt;
        int tex_h = static_cast<int>(world.height) * ppt;
        float wpp = 1.0f / static_cast<float>(ppt);

        std::vector<uint32_t> pixels(static_cast<size_t>(tex_w) * tex_h);

        int base_rows = tile_rows_done;
        auto progress = [&](int row, int /*row_total*/) {
            float pct = static_cast<float>(base_rows + row) / static_cast<float>(total_tile_rows);
            render_loading_screen(pct, stage);
        };

        render_terrain_region(world, 0.0f, 0.0f, wpp, tex_w, tex_h, seed, pixels.data(), tex_w,
                              water_level, progress);
        tile_rows_done += static_cast<int>(world.height);

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

    // L0 macro: 4 px/tile — planetary view
    bake_level(cache_macro_, 4, "Macro level (4 px/tile)");

    // L1 meso: 8 px/tile — regional view
    bake_level(cache_meso_, 8, "Meso level (8 px/tile)");

    // L2 detail: 16 px/tile — baked in background thread.
    // User can interact immediately with macro/meso; detail swaps in when ready.
    cache_detail_.valid = false;
    detail_tex_w_ = static_cast<int>(world.width) * 16;
    detail_tex_h_ = static_cast<int>(world.height) * 16;
    detail_pixels_.resize(static_cast<size_t>(detail_tex_w_) * detail_tex_h_);

    detail_baking_.store(true, std::memory_order_relaxed);
    detail_ready_.store(false, std::memory_order_relaxed);
    detail_cancel_.store(false, std::memory_order_relaxed);

    // LIFETIME CONTRACT: `world` is captured by reference. The caller must keep the
    // Terrain object alive for the duration of this thread. Safety is guaranteed by:
    // (1) invalidate_terrain_cache() joins the thread before any terrain destruction,
    // (2) bake_terrain_cache() joins at entry before starting a new thread.
    // If terrain ownership changes (e.g. moved to shared_ptr), update this capture.
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

void Renderer::render_terrain(const Terrain& world, const Camera& cam, int win_w, int win_h,
                              const DynamicState* dyn, bool show_dynamic_tint) {
    // ── Check for completed background detail bake ────────────────────
    if (detail_ready_.load(std::memory_order_acquire)) {
        finish_detail_bake();
    }

    // ── Clipmap terrain rendering ─────────────────────────────────────────
    float tile_screen = static_cast<float>(TILE_SIZE) * cam.zoom;

    // Choose cache level based on zoom:
    // Detail (16 px/tile) at zoom >= 1.5x, meso (8 px/tile) at 0.5x+, macro (4 px/tile) below
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
        // Compute screen rect for the cached region
        float wpp = active_cache->world_per_pixel;
        // World-to-screen: screen_x = (world_x * TILE_SIZE - cam.x) * cam.zoom + win_w/2
        float screen_x0 =
            (active_cache->world_x0 * static_cast<float>(TILE_SIZE) - cam.x) * cam.zoom +
            static_cast<float>(win_w) / 2.0f;
        float screen_y0 =
            (active_cache->world_y0 * static_cast<float>(TILE_SIZE) - cam.y) * cam.zoom +
            static_cast<float>(win_h) / 2.0f;
        // Each cache pixel covers wpp world units = wpp * TILE_SIZE * zoom screen pixels
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
        max_tx = std::min(max_tx + 1, static_cast<int>(world.width) - 1);
        max_ty = std::min(max_ty + 1, static_cast<int>(world.height) - 1);
        for (int ty = min_ty; ty <= max_ty; ++ty) {
            for (int tx = min_tx; tx <= max_tx; ++tx) {
                auto color = terrain_color(
                    world.tile_at(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty)));
                SDL_Rect dst = cam.tile_to_screen(tx, ty, TILE_SIZE, win_w, win_h);
                SDL_SetRenderDrawColor(renderer_, color[0], color[1], color[2], 255);
                SDL_RenderFillRect(renderer_, &dst);
            }
        }
    }

    // ── Dynamic tinting overlay (wet/snow/water) ──────────────────────────
    // Only shown when an overlay is active (show_dynamic_tint=true).
    // Drawn as semi-transparent colored rectangles on top of the terrain cache.
    if (dyn && show_dynamic_tint) {
        int min_tx, min_ty, max_tx, max_ty;
        cam.screen_to_tile(0, 0, win_w, win_h, TILE_SIZE, min_tx, min_ty);
        cam.screen_to_tile(win_w, win_h, win_w, win_h, TILE_SIZE, max_tx, max_ty);
        min_tx = std::max(min_tx - 1, 0);
        min_ty = std::max(min_ty - 1, 0);
        max_tx = std::min(max_tx + 1, static_cast<int>(world.width) - 1);
        max_ty = std::min(max_ty + 1, static_cast<int>(world.height) - 1);

        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        for (int ty = min_ty; ty <= max_ty; ++ty) {
            for (int tx = min_tx; tx <= max_tx; ++tx) {
                const TerrainTile& tile =
                    world.tile_at(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty));
                if (tile.is_ocean || tile.band == sandbox::ElevBand::Water)
                    continue;

                const auto& dt = dyn->tile_at(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty));
                SDL_Rect dst = cam.tile_to_screen(tx, ty, TILE_SIZE, win_w, win_h);

                // Snow whitens terrain
                if (dt.snow_depth > 0.01f) {
                    uint8_t snow_a =
                        static_cast<uint8_t>(std::clamp(dt.snow_depth * 200.0f, 0.0f, 180.0f));
                    SDL_SetRenderDrawColor(renderer_, 240, 240, 255, snow_a);
                    SDL_RenderFillRect(renderer_, &dst);
                }
                // Standing water tints blue
                else if (dt.surface_water > 0.1f) {
                    uint8_t water_a = static_cast<uint8_t>(
                        std::clamp((dt.surface_water - 0.1f) * 300.0f, 0.0f, 150.0f));
                    SDL_SetRenderDrawColor(renderer_, 40, 100, 200, water_a);
                    SDL_RenderFillRect(renderer_, &dst);
                }
                // Wet soil darkens
                else {
                    float wet = std::clamp(dt.soil_moisture + dt.surface_water * 0.5f, 0.0f, 1.0f);
                    if (wet > 0.2f) {
                        uint8_t wet_a = static_cast<uint8_t>((wet - 0.2f) * 80.0f);
                        SDL_SetRenderDrawColor(renderer_, 20, 40, 10, wet_a);
                        SDL_RenderFillRect(renderer_, &dst);
                    }
                }
            }
        }
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
    }
}

void Renderer::render_weather_overlay(const Terrain& world, const ClimateData& climate,
                                      const Camera& cam, int win_w, int win_h, OverlayMode mode,
                                      const DynamicState* dyn, const AtmosphereState* atmo) {
    if (mode == OverlayMode::None || mode == OverlayMode::WindDirection ||
        mode == OverlayMode::MoistureBars)
        return;

    int min_tx, min_ty, max_tx, max_ty;
    cam.screen_to_tile(0, 0, win_w, win_h, TILE_SIZE, min_tx, min_ty);
    cam.screen_to_tile(win_w, win_h, win_w, win_h, TILE_SIZE, max_tx, max_ty);

    min_tx = std::max(min_tx - 1, 0);
    min_ty = std::max(min_ty - 1, 0);
    max_tx = std::min(max_tx + 1, static_cast<int>(world.width) - 1);
    max_ty = std::min(max_ty + 1, static_cast<int>(world.height) - 1);

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    for (int ty = min_ty; ty <= max_ty; ++ty) {
        for (int tx = min_tx; tx <= max_tx; ++tx) {
            const ClimateTile& ct =
                climate.tile_at(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty));
            SDL_Rect dst = cam.tile_to_screen(tx, ty, TILE_SIZE, win_w, win_h);

            SDL_Color c;
            switch (mode) {
                case OverlayMode::Temperature: {
                    float temp_val = ct.temperature;
                    if (atmo && dyn && !dyn->paused) {
                        temp_val = atmo->sample(static_cast<uint32_t>(tx),
                                                static_cast<uint32_t>(ty), &AtmosphereCell::T);
                    }
                    c = temperature_color(temp_val);
                    break;
                }
                case OverlayMode::Precipitation: {
                    // Show dynamic precipitation from atmosphere (nearest-neighbor
                    // for sharp rain shower boundaries) or static climate fallback
                    float precip_val = ct.precipitation;
                    if (atmo && dyn && !dyn->paused) {
                        // Nearest-neighbor: rain either falls or it doesn't —
                        // bilinear smears the coarse grid into invisible gradients
                        precip_val = atmo->sample_nearest(static_cast<uint32_t>(tx),
                                                          static_cast<uint32_t>(ty),
                                                          &AtmosphereCell::precip_rate);
                        // Rescale: actual precip_rate peaks ~0.2-0.3, map to full color range
                        precip_val = std::min(precip_val * 4.0f, 1.0f);
                    }
                    c = precipitation_color(precip_val);
                    break;
                }
                case OverlayMode::Moisture: {
                    float moist_val = ct.moisture;
                    if (atmo && dyn && !dyn->paused) {
                        // Show atmospheric humidity (q) — the actual moisture in the air
                        moist_val = atmo->sample(static_cast<uint32_t>(tx),
                                                 static_cast<uint32_t>(ty), &AtmosphereCell::q);
                    }
                    c = moisture_color(moist_val);
                    break;
                }
                case OverlayMode::Evaporation: {
                    float evap_val = ct.evaporation;
                    if (dyn && !dyn->paused) {
                        const auto& dt =
                            dyn->tile_at(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty));
                        evap_val = dt.effective_evap;
                    }
                    c = evaporation_color(evap_val);
                    break;
                }
                case OverlayMode::Storminess: {
                    float storm_val = ct.storminess;
                    if (atmo && dyn && !dyn->paused) {
                        // Nearest-neighbor: storm cells have discrete boundaries
                        storm_val = atmo->sample_nearest(static_cast<uint32_t>(tx),
                                                         static_cast<uint32_t>(ty),
                                                         &AtmosphereCell::storminess);
                    }
                    c = storminess_color(storm_val);
                    break;
                }
                case OverlayMode::PrecipBudget: {
                    if (atmo) {
                        float budget =
                            atmo->sample(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty),
                                         &AtmosphereCell::precip_budget);
                        c = precip_budget_color(budget);
                    } else {
                        continue;
                    }
                    break;
                }
                case OverlayMode::Stability: {
                    if (atmo) {
                        float stab =
                            atmo->sample(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty),
                                         &AtmosphereCell::stability);
                        c = stability_color(stab);
                    } else {
                        continue;
                    }
                    break;
                }
                case OverlayMode::UpperWind: {
                    if (atmo) {
                        float u_up =
                            atmo->sample(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty),
                                         &AtmosphereCell::u_upper);
                        float v_up =
                            atmo->sample(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty),
                                         &AtmosphereCell::v_upper);
                        float spd = std::sqrt(u_up * u_up + v_up * v_up);
                        float t = std::clamp(spd / 4.0f, 0.0f, 1.0f);
                        auto r = static_cast<uint8_t>(40.0f + t * 215.0f);
                        auto g = static_cast<uint8_t>(40.0f + t * 160.0f);
                        auto b = static_cast<uint8_t>(80.0f + t * 175.0f);
                        c = {r, g, b, 160};
                    } else {
                        continue;
                    }
                    break;
                }
                case OverlayMode::RainShadow: {
                    float shadow = ct.rain_shadow;
                    // When atmosphere is running, compute dynamic rain shadow from
                    // current wind direction + terrain: leeward of high terrain
                    // with low precip compared to windward side
                    if (atmo && dyn && !dyn->paused) {
                        auto utx = static_cast<uint32_t>(tx);
                        auto uty = static_cast<uint32_t>(ty);
                        const auto& tt = world.tile_at(utx, uty);
                        if (tt.is_ocean || tt.band == sandbox::ElevBand::Water) {
                            continue;
                        }
                        float wu = atmo->sample(utx, uty, &AtmosphereCell::u);
                        float wv = atmo->sample(utx, uty, &AtmosphereCell::v);
                        float spd = std::sqrt(wu * wu + wv * wv);
                        if (spd < 0.05f) {
                            continue;
                        }
                        // Trace upwind to find mountains
                        float udx = -wu / spd;
                        float udy = -wv / spd;
                        float fx = static_cast<float>(tx);
                        float fy = static_cast<float>(ty);
                        shadow = 0.0f;
                        for (int step = 1; step <= 12; ++step) {
                            fx += udx;
                            fy += udy;
                            int ux = static_cast<int>(std::round(fx));
                            int uy = static_cast<int>(std::round(fy));
                            if (ux < 0 || ux >= static_cast<int>(world.width) || uy < 0 ||
                                uy >= static_cast<int>(world.height))
                                break;
                            const auto& uptt =
                                world.tile_at(static_cast<uint32_t>(ux), static_cast<uint32_t>(uy));
                            float elev_diff = uptt.elev01 - tt.elev01;
                            if (elev_diff > 0.08f && uptt.elev01 > 0.45f) {
                                float dist_decay = 1.0f - static_cast<float>(step - 1) / 12.0f;
                                float s = std::clamp(elev_diff * 3.0f, 0.0f, 1.0f) * dist_decay;
                                shadow = std::max(shadow, s);
                            }
                        }
                    }
                    if (shadow > 0.05f) {
                        float t = std::clamp(shadow, 0.0f, 1.0f);
                        auto a = static_cast<uint8_t>(80.0f + t * 175.0f);
                        c = {255, static_cast<uint8_t>(160.0f * (1.0f - t * 0.6f)),
                             static_cast<uint8_t>(40.0f * (1.0f - t)), a};
                    } else {
                        continue;
                    }
                    break;
                }
                case OverlayMode::Geology: {
                    const auto& tt =
                        world.tile_at(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty));
                    if (tt.is_ocean)
                        continue;
                    c = geology_color(tt.rock);
                    break;
                }
                case OverlayMode::SoilTextureOverlay: {
                    const auto& tt =
                        world.tile_at(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty));
                    if (tt.is_ocean)
                        continue;
                    c = soil_texture_color(tt.soil);
                    break;
                }
                default:
                    continue;
            }

            SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, c.a);
            SDL_RenderFillRect(renderer_, &dst);
        }
    }
}

void Renderer::render_dynamic_overlay(const Terrain& world, const DynamicState& dyn,
                                      const Camera& cam, int win_w, int win_h, OverlayMode mode) {
    if (mode != OverlayMode::SurfaceWater && mode != OverlayMode::SoilMoisture &&
        mode != OverlayMode::SnowDepth && mode != OverlayMode::Aridity &&
        mode != OverlayMode::Groundwater && mode != OverlayMode::Discharge)
        return;

    int min_tx, min_ty, max_tx, max_ty;
    cam.screen_to_tile(0, 0, win_w, win_h, TILE_SIZE, min_tx, min_ty);
    cam.screen_to_tile(win_w, win_h, win_w, win_h, TILE_SIZE, max_tx, max_ty);

    min_tx = std::max(min_tx - 1, 0);
    min_ty = std::max(min_ty - 1, 0);
    max_tx = std::min(max_tx + 1, static_cast<int>(world.width) - 1);
    max_ty = std::min(max_ty + 1, static_cast<int>(world.height) - 1);

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    for (int ty = min_ty; ty <= max_ty; ++ty) {
        for (int tx = min_tx; tx <= max_tx; ++tx) {
            const auto& tt = world.tile_at(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty));
            if (tt.is_ocean || tt.band == sandbox::ElevBand::Water)
                continue;

            const auto& dt = dyn.tile_at(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty));
            SDL_Rect dst = cam.tile_to_screen(tx, ty, TILE_SIZE, win_w, win_h);

            SDL_Color c;
            switch (mode) {
                case OverlayMode::SurfaceWater:
                    c = surface_water_color(dt.surface_water);
                    break;
                case OverlayMode::SoilMoisture:
                    c = soil_moisture_color(dt.soil_moisture);
                    break;
                case OverlayMode::SnowDepth:
                    c = snow_depth_color(dt.snow_depth);
                    if (c.a < 5)
                        continue;  // skip nearly invisible tiles
                    break;
                case OverlayMode::Aridity:
                    c = aridity_color(dt.aridity);
                    break;
                case OverlayMode::Groundwater:
                    c = groundwater_color(dt.groundwater);
                    break;
                case OverlayMode::Discharge:
                    c = discharge_color(dt.discharge);
                    if (c.a < 5)
                        continue;  // skip no-flow tiles
                    break;
                default:
                    continue;
            }

            SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, c.a);
            SDL_RenderFillRect(renderer_, &dst);
        }
    }
}

void Renderer::render_wind_arrows(const ClimateData& climate, const Camera& cam, int win_w,
                                  int win_h, const AtmosphereState* atmo) {
    // Determine arrow spacing based on zoom
    int step = std::max(1, static_cast<int>(4.0f / cam.zoom));

    int min_tx, min_ty, max_tx, max_ty;
    cam.screen_to_tile(0, 0, win_w, win_h, TILE_SIZE, min_tx, min_ty);
    cam.screen_to_tile(win_w, win_h, win_w, win_h, TILE_SIZE, max_tx, max_ty);

    min_tx = std::max(min_tx - 1, 0);
    min_ty = std::max(min_ty - 1, 0);
    max_tx = std::min(max_tx + 1, static_cast<int>(climate.width) - 1);
    max_ty = std::min(max_ty + 1, static_cast<int>(climate.height) - 1);

    // Align to step grid
    min_tx = (min_tx / step) * step;
    min_ty = (min_ty / step) * step;

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    // Arrow opacity scales down when zoomed in (arrows get large and dominant)
    float zoom_alpha_scale = std::clamp(1.5f / cam.zoom, 0.4f, 1.0f);

    // Adaptive density reduction at low zoom — skip arrows to reduce clutter
    int density_step = 1;
    if (cam.zoom < 0.6f)
        density_step = 3;
    else if (cam.zoom < 1.0f)
        density_step = 2;

    // Helper: draw a single arrow (shaft + arrowhead)
    auto draw_arrow = [&](float cx, float cy, float ex, float ey, float dx, float dy,
                          float arrow_len, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        SDL_SetRenderDrawColor(renderer_, r, g, b, a);

        SDL_RenderDrawLine(renderer_, static_cast<int>(cx), static_cast<int>(cy),
                           static_cast<int>(ex), static_cast<int>(ey));

        float head_len = arrow_len * 0.35f;
        float perp_x = -dy;
        float perp_y = dx;
        float hx1 = ex - dx * head_len + perp_x * head_len * 0.5f;
        float hy1 = ey - dy * head_len + perp_y * head_len * 0.5f;
        float hx2 = ex - dx * head_len - perp_x * head_len * 0.5f;
        float hy2 = ey - dy * head_len - perp_y * head_len * 0.5f;

        SDL_RenderDrawLine(renderer_, static_cast<int>(ex), static_cast<int>(ey),
                           static_cast<int>(hx1), static_cast<int>(hy1));
        SDL_RenderDrawLine(renderer_, static_cast<int>(ex), static_cast<int>(ey),
                           static_cast<int>(hx2), static_cast<int>(hy2));
    };

    for (int ty = min_ty; ty <= max_ty; ty += step) {
        for (int tx = min_tx; tx <= max_tx; tx += step) {
            // Spatially regular density reduction at low zoom
            if (density_step > 1 && ((tx / step + ty / step) % density_step) != 0)
                continue;
            float wu, wv;
            if (atmo) {
                wu = atmo->sample(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty),
                                  &AtmosphereCell::u);
                wv = atmo->sample(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty),
                                  &AtmosphereCell::v);
            } else {
                const ClimateTile& ct =
                    climate.tile_at(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty));
                wu = ct.wind_u;
                wv = ct.wind_v;
            }

            float speed = std::sqrt(wu * wu + wv * wv);
            if (speed < 0.01f)
                continue;

            SDL_Rect dst = cam.tile_to_screen(tx, ty, TILE_SIZE, win_w, win_h);
            float cx = static_cast<float>(dst.x) + static_cast<float>(dst.w) * 0.5f;
            float cy = static_cast<float>(dst.y) + static_cast<float>(dst.h) * 0.5f;

            float dx = wu / speed;
            float dy = wv / speed;

            // Clamp arrow length so zoomed-in arrows don't dominate
            float max_arrow = std::min(static_cast<float>(dst.w) * 0.8f, 14.0f);
            float arrow_len = max_arrow * std::min(speed, 1.5f);

            float ex = cx + dx * arrow_len;
            float ey = cy + dy * arrow_len;

            // Neutral light gray at reduced opacity — visible but doesn't compete with scalar
            // overlay
            auto base_alpha = static_cast<uint8_t>(std::clamp(speed * 160.0f, 50.0f, 120.0f));
            auto alpha = static_cast<uint8_t>(static_cast<float>(base_alpha) * zoom_alpha_scale);

            // Dark outline for visibility against any background
            draw_arrow(cx, cy, ex, ey, dx, dy, arrow_len, 0, 0, 0,
                       static_cast<uint8_t>(static_cast<float>(alpha) * 0.4f));
            // Neutral gray foreground — magnitude encoded in length, not brightness
            draw_arrow(cx, cy, ex, ey, dx, dy, arrow_len, 215, 220, 227, alpha);
        }
    }
}

void Renderer::render_moisture_bars(const Terrain& world, const ClimateData& climate,
                                    const Camera& cam, int win_w, int win_h,
                                    const DynamicState* dyn, const AtmosphereState* /*atmo*/) {
    int min_tx, min_ty, max_tx, max_ty;
    cam.screen_to_tile(0, 0, win_w, win_h, TILE_SIZE, min_tx, min_ty);
    cam.screen_to_tile(win_w, win_h, win_w, win_h, TILE_SIZE, max_tx, max_ty);

    min_tx = std::max(min_tx - 1, 0);
    min_ty = std::max(min_ty - 1, 0);
    max_tx = std::min(max_tx + 1, static_cast<int>(world.width) - 1);
    max_ty = std::min(max_ty + 1, static_cast<int>(world.height) - 1);

    bool use_dynamic = dyn && !dyn->paused && !dyn->tiles.empty();

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    for (int ty = min_ty; ty <= max_ty; ++ty) {
        for (int tx = min_tx; tx <= max_tx; ++tx) {
            auto utx = static_cast<uint32_t>(tx);
            auto uty = static_cast<uint32_t>(ty);

            const TerrainTile& tt = world.tile_at(utx, uty);
            if (tt.is_ocean || tt.band == sandbox::ElevBand::Water)
                continue;

            float moisture;
            if (use_dynamic) {
                size_t idx = static_cast<size_t>(uty) * world.width + utx;
                moisture = dyn->tiles[idx].effective_moisture;
            } else {
                moisture = climate.tile_at(utx, uty).moisture;
            }

            if (moisture <= 0.01f)
                continue;

            SDL_Rect dst = cam.tile_to_screen(tx, ty, TILE_SIZE, win_w, win_h);

            // Wider bar: 3/4 of tile width, full height scaled by moisture
            int bar_w = std::max(2, dst.w * 3 / 4);
            int bar_h = std::max(2, static_cast<int>(moisture * static_cast<float>(dst.h)));

            SDL_Rect bar = {
                dst.x + (dst.w - bar_w) / 2,
                dst.y + dst.h - bar_h,
                bar_w,
                bar_h,
            };

            // Blue gradient: brighter for more moisture
            auto blue = static_cast<uint8_t>(150.0f + moisture * 105.0f);
            SDL_SetRenderDrawColor(renderer_, 40, 80, blue, 210);
            SDL_RenderFillRect(renderer_, &bar);
        }
    }
}

// ── UI Button (same as worldgen) ────────────────────────────────────────────

ButtonRect render_button(SDL_Renderer* renderer, int x, int y, const char* label, bool hovered) {
    constexpr int SCALE = 2;
    constexpr int PAD_X = 12;
    constexpr int PAD_Y = 8;

    int text_w = text_pixel_width(label, SCALE);
    int btn_w = text_w + PAD_X * 2;
    int btn_h = 7 * SCALE + PAD_Y * 2;

    ButtonRect rect = {x, y, btn_w, btn_h};

    if (hovered) {
        SDL_SetRenderDrawColor(renderer, 60, 80, 120, 255);
    } else {
        SDL_SetRenderDrawColor(renderer, 40, 50, 70, 255);
    }
    SDL_Rect bg_rect = {x, y, btn_w, btn_h};
    SDL_RenderFillRect(renderer, &bg_rect);

    SDL_SetRenderDrawColor(renderer, 100, 120, 160, 255);
    SDL_RenderDrawRect(renderer, &bg_rect);

    draw_text(renderer, x + PAD_X, y + PAD_Y, label, SCALE, 220, 220, 240);

    return rect;
}

ButtonRect render_status_chip(SDL_Renderer* renderer, int x, int y, const char* label,
                              bool hovered) {
    constexpr int SCALE = 2;
    constexpr int PAD_X = 12;
    constexpr int PAD_Y = 8;

    int text_w = text_pixel_width(label, SCALE);
    int btn_w = text_w + PAD_X * 2;
    int btn_h = 7 * SCALE + PAD_Y * 2;

    ButtonRect rect = {x, y, btn_w, btn_h};

    // Status chip: darker, distinct from action buttons
    if (hovered) {
        SDL_SetRenderDrawColor(renderer, 30, 42, 58, 255);
    } else {
        SDL_SetRenderDrawColor(renderer, 26, 36, 50, 255);
    }
    SDL_Rect bg_rect = {x, y, btn_w, btn_h};
    SDL_RenderFillRect(renderer, &bg_rect);

    SDL_SetRenderDrawColor(renderer, 43, 58, 82, 255);
    SDL_RenderDrawRect(renderer, &bg_rect);

    // Lighter blue-white text to distinguish from action buttons
    draw_text(renderer, x + PAD_X, y + PAD_Y, label, SCALE, 207, 227, 255);

    return rect;
}

}  // namespace sandbox
