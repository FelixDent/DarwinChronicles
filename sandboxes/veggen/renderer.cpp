#include "renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>

#include "telemetry.h"

namespace veggen {

// ── Overlay names ───────────────────────────────────────────────────────────

const char* overlay_name(OverlayMode mode) {
    switch (mode) {
        case OverlayMode::None:
            return "None";
        case OverlayMode::Moisture:
            return "Root Moisture";
        case OverlayMode::Temperature:
            return "Temperature";
        case OverlayMode::Health:
            return "Health";
        case OverlayMode::Elevation:
            return "Elevation";
        case OverlayMode::SurfaceWater:
            return "Surface Water";
        case OverlayMode::Canopy:
            return "Canopy Cover";
        default:
            return "Unknown";
    }
}

// ── Renderer ────────────────────────────────────────────────────────────────

void Renderer::init(SDL_Renderer* sdl_renderer) {
    renderer_ = sdl_renderer;
}

void Renderer::shutdown() {
    flush_cache();
    renderer_ = nullptr;
}

void Renderer::flush_cache() {
    for (auto& [key, entry] : texture_cache_) {
        if (entry.tex)
            SDL_DestroyTexture(entry.tex);
    }
    texture_cache_.clear();
}

void Renderer::evict_stale_textures() {
    if (texture_cache_.size() <= CACHE_MAX_SIZE / 2)
        return;

    auto it = texture_cache_.begin();
    while (it != texture_cache_.end()) {
        if (frame_counter_ - it->second.last_used_frame > EVICT_STALE_FRAMES) {
            if (it->second.tex)
                SDL_DestroyTexture(it->second.tex);
            it = texture_cache_.erase(it);
        } else {
            ++it;
        }
    }
}

int Renderer::grid_offset_x(int win_w) const {
    return (win_w - GRID_SIZE * TILE_PX) / 2;
}

int Renderer::grid_offset_y(int win_h) const {
    return (win_h - GRID_SIZE * TILE_PX) / 2;
}

SDL_Rect Renderer::tile_rect(int tx, int ty, int win_w, int win_h) const {
    int ox = grid_offset_x(win_w);
    int oy = grid_offset_y(win_h);
    return {ox + tx * TILE_PX, oy + ty * TILE_PX, TILE_PX, TILE_PX};
}

SDL_Texture* Renderer::get_or_create_texture(const PlantTraits& traits, GrowthPhase phase,
                                             PlantHealth health) {
    SpriteCacheKey key{traits.sprite_seed, phase, health};
    auto it = texture_cache_.find(key);
    if (it != texture_cache_.end()) {
        it->second.last_used_frame = frame_counter_;
        return it->second.tex;
    }

    Sprite sprite = generate_plant_sprite(traits, phase, health);

    SDL_Texture* tex = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32,
                                         SDL_TEXTUREACCESS_STATIC, sprite.w, sprite.h);
    if (tex) {
        SDL_UpdateTexture(tex, nullptr, sprite.pixels.data(), sprite.w * 4);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    }

    texture_cache_[key] = {tex, frame_counter_};
    return tex;
}

void Renderer::render_tiles(const SimState& sim, int win_w, int win_h) {
    const auto& preset = BIOME_PRESETS[sim.preset_index];

    // Per-4px-block noise for subtle ground texture variation
    constexpr int BLOCK = 4;

    for (int ty = 0; ty < GRID_SIZE; ++ty) {
        for (int tx = 0; tx < GRID_SIZE; ++tx) {
            const auto& tile = sim.tiles[ty][tx];
            SDL_Rect tile_dst = tile_rect(tx, ty, win_w, win_h);

            // Per-tile ground color tinted by moisture and elevation
            // Wetter tiles are darker/greener, drier tiles are lighter/yellower
            // Higher tiles shift toward grey-brown (rocky)
            float wetness = std::clamp(tile.root_moisture + tile.surface_water * 0.5f, 0.0f, 1.0f);
            float elev_shift = (tile.elev01 - preset.elev01) * 2.0f;  // deviation from preset base

            int base_r = static_cast<int>(preset.ground_r);
            int base_g = static_cast<int>(preset.ground_g);
            int base_b = static_cast<int>(preset.ground_b);

            // Wetness: darken and shift green
            int wet_r = base_r - static_cast<int>(wetness * 25.0f);
            int wet_g = base_g + static_cast<int>(wetness * 10.0f);
            int wet_b = base_b - static_cast<int>(wetness * 15.0f);

            // Elevation: higher = lighter/greyer, lower = richer color
            wet_r += static_cast<int>(elev_shift * 15.0f);
            wet_g += static_cast<int>(elev_shift * 10.0f);
            wet_b += static_cast<int>(elev_shift * 12.0f);

            for (int by = 0; by < TILE_PX; by += BLOCK) {
                for (int bx = 0; bx < TILE_PX; bx += BLOCK) {
                    int abs_bx = tx * TILE_PX + bx;
                    int abs_by = ty * TILE_PX + by;
                    uint32_t h = static_cast<uint32_t>(abs_bx * 374761393 + abs_by * 668265263) +
                                 static_cast<uint32_t>(sim.preset_index) * 2654435761u;
                    h = (h ^ (h >> 13)) * 1274126177u;
                    h ^= h >> 16;

                    int dr = static_cast<int>(h & 0xF) - 8;
                    int dg = static_cast<int>((h >> 4) & 0xF) - 8;
                    int db = static_cast<int>((h >> 8) & 0xF) - 8;

                    auto r = static_cast<uint8_t>(std::clamp(wet_r + dr, 0, 255));
                    auto g = static_cast<uint8_t>(std::clamp(wet_g + dg, 0, 255));
                    auto b = static_cast<uint8_t>(std::clamp(wet_b + db, 0, 255));

                    SDL_Rect block_dst = {tile_dst.x + bx, tile_dst.y + by, BLOCK, BLOCK};
                    SDL_SetRenderDrawColor(renderer_, r, g, b, 255);
                    SDL_RenderFillRect(renderer_, &block_dst);
                }
            }
        }
    }
}

void Renderer::render_plants(const SimState& sim, int win_w, int win_h) {
    ++frame_counter_;

    // Periodically evict textures for plants that no longer exist
    if ((frame_counter_ & 0x3F) == 0)  // every 64 frames
        evict_stale_textures();

    // Sort by screen Y (tile_y * TILE_PX + offset_y) so nearer plants draw on top
    auto num_plants = sim.plants.size();
    sort_buffer_.resize(num_plants);
    std::iota(sort_buffer_.begin(), sort_buffer_.end(), 0);
    std::sort(sort_buffer_.begin(), sort_buffer_.end(), [&](size_t a, size_t b) {
        const auto& pa = sim.plants[a];
        const auto& pb = sim.plants[b];
        // Sort by screen-space base Y (bottom of sprite = foot of plant)
        float ya =
            static_cast<float>(pa.tile_y * TILE_PX) + pa.offset_y * static_cast<float>(TILE_PX);
        float yb =
            static_cast<float>(pb.tile_y * TILE_PX) + pb.offset_y * static_cast<float>(TILE_PX);
        return ya < yb;  // draw far (small Y) first, near (large Y) last
    });

    for (size_t idx : sort_buffer_) {
        const auto& plant = sim.plants[idx];

        // Skip small-phase trees — at tile scale they're just a trunk pixel
        if (plant.phase == GrowthPhase::Small &&
            (plant.traits.archetype == PlantArchetype::Conifer ||
             plant.traits.archetype == PlantArchetype::BroadleafTree)) {
            continue;
        }

        SDL_Texture* tex = get_or_create_texture(plant.traits, plant.phase, plant.health);
        if (!tex)
            continue;

        SDL_Rect tile_dst = tile_rect(plant.tile_x, plant.tile_y, win_w, win_h);

        int tex_w = 0, tex_h = 0;
        SDL_QueryTexture(tex, nullptr, nullptr, &tex_w, &tex_h);

        // Per-archetype target height as fraction of tile
        float target_frac;
        switch (plant.traits.archetype) {
            case PlantArchetype::Conifer:
            case PlantArchetype::BroadleafTree:
                target_frac = (plant.phase == GrowthPhase::Large)    ? 0.80f
                              : (plant.phase == GrowthPhase::Medium) ? 0.50f
                                                                     : 0.22f;
                break;
            case PlantArchetype::Shrub:
                target_frac = (plant.phase == GrowthPhase::Large)    ? 0.55f
                              : (plant.phase == GrowthPhase::Medium) ? 0.35f
                                                                     : 0.18f;
                break;
            case PlantArchetype::Grass:
            case PlantArchetype::Succulent:
                target_frac = (plant.phase == GrowthPhase::Large)    ? 0.35f
                              : (plant.phase == GrowthPhase::Medium) ? 0.22f
                                                                     : 0.12f;
                break;
            case PlantArchetype::Moss:
                target_frac = (plant.phase == GrowthPhase::Large)    ? 0.20f
                              : (plant.phase == GrowthPhase::Medium) ? 0.12f
                                                                     : 0.07f;
                break;
            case PlantArchetype::Palm:
                target_frac = (plant.phase == GrowthPhase::Large)    ? 0.85f
                              : (plant.phase == GrowthPhase::Medium) ? 0.55f
                                                                     : 0.25f;
                break;
            case PlantArchetype::TropicalTree:
                target_frac = (plant.phase == GrowthPhase::Large)    ? 0.85f
                              : (plant.phase == GrowthPhase::Medium) ? 0.55f
                                                                     : 0.25f;
                break;
        }
        float scale =
            (static_cast<float>(TILE_PX) * target_frac) / static_cast<float>(std::max(tex_h, 1));
        int draw_w = static_cast<int>(static_cast<float>(tex_w) * scale);
        int draw_h = static_cast<int>(static_cast<float>(tex_h) * scale);

        // Position: sprite base (bottom) anchored at the offset point
        int px = tile_dst.x + static_cast<int>(plant.offset_x * static_cast<float>(TILE_PX)) -
                 draw_w / 2;
        int py =
            tile_dst.y + static_cast<int>(plant.offset_y * static_cast<float>(TILE_PX)) - draw_h;

        // Small seedlings fade into the ground
        if (plant.phase == GrowthPhase::Small) {
            SDL_SetTextureAlphaMod(tex, 140);
        } else {
            SDL_SetTextureAlphaMod(tex, 255);
        }

        SDL_Rect sprite_dst = {px, py, draw_w, draw_h};
        SDL_RenderCopy(renderer_, tex, nullptr, &sprite_dst);
    }
}

void Renderer::render_overlay(const SimState& sim, int win_w, int win_h, OverlayMode mode) {
    if (mode == OverlayMode::None)
        return;

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    for (int ty = 0; ty < GRID_SIZE; ++ty) {
        for (int tx = 0; tx < GRID_SIZE; ++tx) {
            const auto& tile = sim.tiles[ty][tx];
            SDL_Rect dst = tile_rect(tx, ty, win_w, win_h);

            SDL_Color c;
            switch (mode) {
                case OverlayMode::Moisture:
                    c = soil_moisture_color(tile.soil_moisture());
                    break;
                case OverlayMode::Temperature:
                    c = temperature_color(tile.temperature);
                    break;
                case OverlayMode::Health: {
                    float avg_stress = 0;
                    int count = 0;
                    for (const auto& p : sim.plants) {
                        if (p.tile_x == tx && p.tile_y == ty) {
                            avg_stress += p.stress;
                            ++count;
                        }
                    }
                    if (count > 0)
                        avg_stress /= static_cast<float>(count);
                    PlantHealth h;
                    if (avg_stress < 0.3f)
                        h = PlantHealth::Healthy;
                    else if (avg_stress < 0.6f)
                        h = PlantHealth::Stressed;
                    else if (avg_stress < 0.9f)
                        h = PlantHealth::Brown;
                    else
                        h = PlantHealth::Dead;
                    c = health_color(h);
                    c.a = 120;
                    break;
                }
                case OverlayMode::Elevation:
                    c = elevation_color(tile.elev01);
                    break;
                case OverlayMode::SurfaceWater:
                    c = surface_water_color(tile.surface_water);
                    break;
                case OverlayMode::Canopy:
                    c = canopy_color(tile.canopy_cover);
                    break;
                default:
                    continue;
            }

            SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, c.a);
            SDL_RenderFillRect(renderer_, &dst);
        }
    }
}

void Renderer::render_grid(int win_w, int win_h) {
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 200, 200, 200, 80);

    int ox = grid_offset_x(win_w);
    int oy = grid_offset_y(win_h);

    // Vertical lines
    for (int tx = 0; tx <= GRID_SIZE; ++tx) {
        int x = ox + tx * TILE_PX;
        SDL_RenderDrawLine(renderer_, x, oy, x, oy + GRID_SIZE * TILE_PX);
    }
    // Horizontal lines
    for (int ty = 0; ty <= GRID_SIZE; ++ty) {
        int y = oy + ty * TILE_PX;
        SDL_RenderDrawLine(renderer_, ox, y, ox + GRID_SIZE * TILE_PX, y);
    }
}

void Renderer::render_ui(const SimState& sim, int win_w, int /*win_h*/, OverlayMode mode,
                         bool show_fps, float fps) {
    constexpr int SCALE = 2;
    constexpr int PAD = 8;
    constexpr int ROW_H = 18;
    constexpr int N_TILES = GRID_SIZE * GRID_SIZE;

    const auto& preset = BIOME_PRESETS[sim.preset_index];

    // Compute tile averages
    float avg_temp = 0, avg_precip = 0, avg_root = 0, avg_surface = 0, avg_canopy = 0;
    float min_elev = 1.0f, max_elev = 0.0f;
    for (int ty = 0; ty < GRID_SIZE; ++ty) {
        for (int tx = 0; tx < GRID_SIZE; ++tx) {
            const auto& t = sim.tiles[ty][tx];
            avg_temp += t.temperature;
            avg_precip += t.precipitation;
            avg_root += t.root_moisture;
            avg_surface += t.surface_water;
            avg_canopy += t.canopy_cover;
            min_elev = std::min(min_elev, t.elev01);
            max_elev = std::max(max_elev, t.elev01);
        }
    }
    avg_temp /= static_cast<float>(N_TILES);
    avg_precip /= static_cast<float>(N_TILES);
    avg_root /= static_cast<float>(N_TILES);
    avg_surface /= static_cast<float>(N_TILES);
    avg_canopy /= static_cast<float>(N_TILES);

    // Info panel top-left
    int panel_x = PAD;
    int panel_y = PAD;
    int num_rows = 10;
    int panel_w = text_pixel_width("SURFACE WATER: 0.00", SCALE) + PAD * 2;
    int panel_h = PAD * 2 + ROW_H * num_rows;
    draw_panel_bg(renderer_, panel_x, panel_y, panel_w, panel_h);

    int y = panel_y + PAD;
    char buf[64];

    // Biome name
    draw_text(renderer_, panel_x + PAD, y, preset.name, SCALE, 255, 220, 100);
    y += ROW_H;

    // Day and season
    int day = static_cast<int>(sim.elapsed_days);
    std::snprintf(buf, sizeof(buf), "DAY %d  %s", day, season_name(sim.elapsed_days));
    draw_text(renderer_, panel_x + PAD, y, buf, SCALE, 200, 200, 200);
    y += ROW_H;

    // Speed
    if (sim.paused)
        std::snprintf(buf, sizeof(buf), "SPEED: PAUSED");
    else
        std::snprintf(buf, sizeof(buf), "SPEED: %.1fx", static_cast<double>(sim.time_scale));
    draw_text(renderer_, panel_x + PAD, y, buf, SCALE, 180, 180, 220);
    y += ROW_H;

    // Temperature
    std::snprintf(buf, sizeof(buf), "TEMP: %.1f C", static_cast<double>(avg_temp));
    draw_text(renderer_, panel_x + PAD, y, buf, SCALE, 200, 160, 160);
    y += ROW_H;

    // Precipitation
    std::snprintf(buf, sizeof(buf), "PRECIP: %.2f", static_cast<double>(avg_precip));
    draw_text(renderer_, panel_x + PAD, y, buf, SCALE, 160, 160, 200);
    y += ROW_H;

    // Root moisture
    std::snprintf(buf, sizeof(buf), "ROOT MOISTURE: %.2f", static_cast<double>(avg_root));
    draw_text(renderer_, panel_x + PAD, y, buf, SCALE, 160, 200, 160);
    y += ROW_H;

    // Surface water
    std::snprintf(buf, sizeof(buf), "SURFACE WATER: %.2f", static_cast<double>(avg_surface));
    draw_text(renderer_, panel_x + PAD, y, buf, SCALE, 100, 180, 220);
    y += ROW_H;

    // Canopy cover
    std::snprintf(buf, sizeof(buf), "CANOPY: %.0f%%", static_cast<double>(avg_canopy * 100.0f));
    draw_text(renderer_, panel_x + PAD, y, buf, SCALE, 120, 200, 100);
    y += ROW_H;

    // Elevation range
    std::snprintf(buf, sizeof(buf), "ELEV: %.2f-%.2f", static_cast<double>(min_elev),
                  static_cast<double>(max_elev));
    draw_text(renderer_, panel_x + PAD, y, buf, SCALE, 180, 170, 140);
    y += ROW_H;

    // Plant count (living only)
    int live_count = 0;
    for (const auto& p : sim.plants) {
        if (p.health != PlantHealth::Dead)
            ++live_count;
    }
    std::snprintf(buf, sizeof(buf), "PLANTS: %d", live_count);
    draw_text(renderer_, panel_x + PAD, y, buf, SCALE, 200, 200, 200);

    // Overlay indicator top-right
    if (mode != OverlayMode::None) {
        const char* ov_name = overlay_name(mode);
        int text_w = text_pixel_width(ov_name, SCALE);
        int ov_x = win_w - text_w - PAD * 3;
        draw_panel_bg(renderer_, ov_x - PAD, PAD, text_w + PAD * 3, ROW_H + PAD * 2);
        draw_text(renderer_, ov_x, PAD + PAD, ov_name, SCALE, 200, 220, 255);
    }

    // FPS
    if (show_fps) {
        std::snprintf(buf, sizeof(buf), "FPS: %.0f", static_cast<double>(fps));
        int text_w = text_pixel_width(buf, SCALE);
        int fps_x = win_w - text_w - PAD * 2;
        int fps_y = (mode != OverlayMode::None) ? PAD + ROW_H + PAD * 3 : PAD;
        draw_panel_bg(renderer_, fps_x - PAD, fps_y, text_w + PAD * 3, ROW_H + PAD * 2);
        draw_text(renderer_, fps_x, fps_y + PAD, buf, SCALE, 200, 200, 200);
    }
}

}  // namespace veggen
