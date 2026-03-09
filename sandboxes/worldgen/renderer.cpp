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

    if (y2 == y0)
        return;  // Degenerate

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

        if (xa > xb)
            std::swap(xa, xb);
        SDL_RenderDrawLine(renderer, xa, y, xb, y);
    }
}

// Terrain color based on TerrainTile data (browns, grays, blues)
std::array<uint8_t, 3> terrain_color(const sandbox::TerrainTile& tile) {
    if (tile.is_ocean) {
        // Ocean: dark→medium blue gradient by depth (deeper = darker)
        float depth = 1.0f - tile.elev01;  // Higher depth → darker
        uint8_t r = static_cast<uint8_t>(10.0f + depth * 5.0f);
        uint8_t g = static_cast<uint8_t>(30.0f + depth * 30.0f);
        uint8_t b = static_cast<uint8_t>(70.0f + (1.0f - depth) * 80.0f);
        return {r, g, b};
    }
    switch (tile.band) {
        case sandbox::ElevBand::Lowland: {
            // Tan/sandy tones
            float t = tile.elev01;
            uint8_t r = static_cast<uint8_t>(180.0f + t * 30.0f);
            uint8_t g = static_cast<uint8_t>(160.0f + t * 20.0f);
            uint8_t b = static_cast<uint8_t>(110.0f + t * 10.0f);
            return {r, g, b};
        }
        case sandbox::ElevBand::Hills: {
            // Olive brown tones
            float t = tile.elev01;
            uint8_t r = static_cast<uint8_t>(140.0f + t * 20.0f);
            uint8_t g = static_cast<uint8_t>(125.0f + t * 10.0f);
            uint8_t b = static_cast<uint8_t>(80.0f + t * 10.0f);
            return {r, g, b};
        }
        case sandbox::ElevBand::Mountains: {
            // Gray→white gradient by elevation
            float t = std::clamp((tile.elev01 - 0.7f) / 0.3f, 0.0f, 1.0f);
            uint8_t v = static_cast<uint8_t>(130.0f + t * 110.0f);
            return {v, v, static_cast<uint8_t>(v + 5)};
        }
        case sandbox::ElevBand::Water:
            return {70, 140, 200};  // inland basin (light blue)
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
    renderer_ = nullptr;
}

void Renderer::render_terrain(const Terrain& terrain, const Camera& cam, int win_w, int win_h) {
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

            // Elevation indicators on land tiles
            if (!tile.is_ocean && dst.w >= 8) {
                float rough = tile.roughness;
                // Darker shade for elevation features
                uint8_t dr = static_cast<uint8_t>(color[0] * 0.55f);
                uint8_t dg = static_cast<uint8_t>(color[1] * 0.55f);
                uint8_t db = static_cast<uint8_t>(color[2] * 0.55f);

                if (rough >= 0.45f) {
                    // Mountains: tall sharp triangle
                    SDL_SetRenderDrawColor(renderer_, dr, dg, db, 255);
                    int cx = dst.x + dst.w / 2;
                    int peak = dst.y + dst.h / 6;
                    int base_y = dst.y + dst.h - dst.h / 6;
                    int half_w = dst.w / 4;
                    fill_triangle(renderer_, cx, peak, cx - half_w, base_y, cx + half_w, base_y);

                    // Snow cap on the tallest peaks
                    if (rough >= 0.65f) {
                        int snow_y = peak + dst.h / 5;
                        int snow_hw = half_w / 3;
                        SDL_SetRenderDrawColor(renderer_, 240, 240, 255, 255);
                        fill_triangle(renderer_, cx, peak, cx - snow_hw, snow_y, cx + snow_hw,
                                      snow_y);
                    }
                } else if (rough >= 0.15f) {
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
                // Flat (rough < 0.15): no decoration
            }
        }
    }
}

}  // namespace sandbox
