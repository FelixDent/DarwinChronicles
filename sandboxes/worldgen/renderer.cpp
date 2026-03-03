#include "renderer.h"

#include <algorithm>
#include <cmath>

namespace {

// Draw a filled triangle using scanline fill
void fill_triangle(SDL_Renderer* renderer, int x0, int y0, int x1, int y1, int x2, int y2) {
    // Sort vertices by y-coordinate
    if (y0 > y1) {
        std::swap(x0, x1);
        std::swap(y0, y1);
    }
    if (y0 > y2) {
        std::swap(x0, x2);
        std::swap(y0, y2);
    }
    if (y1 > y2) {
        std::swap(x1, x2);
        std::swap(y1, y2);
    }

    if (y2 == y0) return;  // Degenerate

    for (int y = y0; y <= y2; ++y) {
        // Interpolate x along the two active edges
        float t_long = static_cast<float>(y - y0) / static_cast<float>(y2 - y0);
        int xa = x0 + static_cast<int>(t_long * static_cast<float>(x2 - x0));

        int xb;
        if (y < y1) {
            if (y1 == y0)
                xb = x0;
            else {
                float t = static_cast<float>(y - y0) / static_cast<float>(y1 - y0);
                xb = x0 + static_cast<int>(t * static_cast<float>(x1 - x0));
            }
        } else {
            if (y2 == y1)
                xb = x1;
            else {
                float t = static_cast<float>(y - y1) / static_cast<float>(y2 - y1);
                xb = x1 + static_cast<int>(t * static_cast<float>(x2 - x1));
            }
        }

        if (xa > xb) std::swap(xa, xb);
        SDL_RenderDrawLine(renderer, xa, y, xb, y);
    }
}

bool is_water(sandbox::Biome b) {
    return b == sandbox::Biome::Ocean || b == sandbox::Biome::Lake;
}

// Biomes where mountains/hills don't make visual sense (dense canopy covers terrain)
bool suppresses_elevation(sandbox::Biome b) {
    return b == sandbox::Biome::TropicalForest || b == sandbox::Biome::TemperateRainforest;
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

SDL_Rect Camera::tile_to_screen(int tile_x, int tile_y, int tile_size, int win_w,
                                int win_h) const {
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

void Renderer::init(SDL_Renderer* sdl_renderer) { renderer_ = sdl_renderer; }

void Renderer::shutdown() { renderer_ = nullptr; }

void Renderer::render_world(const World& world, const Camera& cam, int win_w, int win_h) {
    int min_tx, min_ty, max_tx, max_ty;
    cam.screen_to_tile(0, 0, win_w, win_h, TILE_SIZE, min_tx, min_ty);
    cam.screen_to_tile(win_w, win_h, win_w, win_h, TILE_SIZE, max_tx, max_ty);

    min_tx = std::max(min_tx - 1, 0);
    min_ty = std::max(min_ty - 1, 0);
    max_tx = std::min(max_tx + 1, static_cast<int>(world.width) - 1);
    max_ty = std::min(max_ty + 1, static_cast<int>(world.height) - 1);

    for (int ty = min_ty; ty <= max_ty; ++ty) {
        for (int tx = min_tx; tx <= max_tx; ++tx) {
            const Tile& tile = world.tile_at(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty));
            SDL_Rect dst = cam.tile_to_screen(tx, ty, TILE_SIZE, win_w, win_h);

            auto color = biome_color(tile.biome);
            SDL_SetRenderDrawColor(renderer_, color[0], color[1], color[2], 255);
            SDL_RenderFillRect(renderer_, &dst);

            // Elevation indicators on land tiles (uses dedicated elevation noise,
            // not height field — creates coherent mountain ranges and hill clusters)
            if (!is_water(tile.biome) && !suppresses_elevation(tile.biome) && dst.w >= 4) {
                float elev = tile.elevation;
                // Darker shade for elevation features
                uint8_t dr = static_cast<uint8_t>(color[0] * 0.55f);
                uint8_t dg = static_cast<uint8_t>(color[1] * 0.55f);
                uint8_t db = static_cast<uint8_t>(color[2] * 0.55f);

                if (elev >= 0.45f) {
                    // Mountains: tall sharp triangle
                    SDL_SetRenderDrawColor(renderer_, dr, dg, db, 255);
                    int cx = dst.x + dst.w / 2;
                    int peak = dst.y + dst.h / 6;
                    int base_y = dst.y + dst.h - dst.h / 6;
                    int half_w = dst.w / 4;
                    fill_triangle(renderer_, cx, peak, cx - half_w, base_y, cx + half_w, base_y);

                    // Snow cap on the tallest peaks
                    if (elev >= 0.65f) {
                        int snow_y = peak + dst.h / 5;
                        int snow_hw = half_w / 3;
                        SDL_SetRenderDrawColor(renderer_, 240, 240, 255, 255);
                        fill_triangle(renderer_, cx, peak, cx - snow_hw, snow_y, cx + snow_hw,
                                      snow_y);
                    }
                } else if (elev >= 0.15f) {
                    // Hills: 2 gentle low bumps
                    SDL_SetRenderDrawColor(renderer_, dr, dg, db, 255);
                    int base_y = dst.y + dst.h - dst.h / 5;
                    // Left bump
                    int lx = dst.x + dst.w / 3;
                    int lp = dst.y + dst.h * 2 / 5;
                    int lhw = dst.w / 5;
                    fill_triangle(renderer_, lx, lp, lx - lhw, base_y, lx + lhw, base_y);
                    // Right bump
                    int rx = dst.x + dst.w * 2 / 3;
                    int rp = dst.y + dst.h / 3;
                    int rhw = dst.w / 5;
                    fill_triangle(renderer_, rx, rp, rx - rhw, base_y, rx + rhw, base_y);
                }
                // Flat (elev < 0.15): no decoration
            }

            // Toxic overlay: purple tint on tiles with significant toxicity
            if (tile.toxins > 0.3f && dst.w >= 2) {
                float tox_alpha = std::clamp((tile.toxins - 0.3f) / 0.7f, 0.0f, 1.0f);
                auto alpha = static_cast<uint8_t>(tox_alpha * 100.0f);
                SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(renderer_, 140, 30, 160, alpha);
                SDL_RenderFillRect(renderer_, &dst);
            }
        }
    }
}

}  // namespace sandbox
