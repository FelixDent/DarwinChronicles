#pragma once

#include "world_gen.h"

#include <SDL.h>

namespace sandbox {

// ── Camera ──────────────────────────────────────────────────────────────────

struct Camera {
    float x = 0.0f;   // World-space center X
    float y = 0.0f;   // World-space center Y
    float zoom = 1.0f;

    static constexpr float MIN_ZOOM = 0.25f;
    static constexpr float MAX_ZOOM = 4.0f;
    static constexpr float PAN_SPEED = 400.0f;  // pixels/sec at zoom 1.0

    void pan(float dx, float dy, float dt);
    void zoom_at(float screen_x, float screen_y, float factor, int win_w, int win_h);
    void center_on_world(uint32_t world_w, uint32_t world_h, int tile_size);

    // Convert world tile coords to screen coords
    SDL_Rect tile_to_screen(int tile_x, int tile_y, int tile_size, int win_w, int win_h) const;

    // Convert screen coords to world tile coords
    void screen_to_tile(int screen_x, int screen_y, int win_w, int win_h, int tile_size,
                        int& tile_x, int& tile_y) const;
};

// ── Renderer ────────────────────────────────────────────────────────────────

class Renderer {
public:
    static constexpr int TILE_SIZE = 16;

    void init(SDL_Renderer* sdl_renderer);
    void shutdown();

    void render_world(const World& world, const Camera& cam, int win_w, int win_h);

private:
    SDL_Renderer* renderer_ = nullptr;
};

}  // namespace sandbox
