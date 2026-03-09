#pragma once

#include <cstdint>

#include "terrain_gen.h"

namespace sandbox {

// 16×16 pixel tile texture, RGBA row-major
struct TilePixels {
    static constexpr int SIZE = 16;
    uint32_t pixels[SIZE * SIZE];  // 0xAABBGGRR (SDL RGBA32)

    void set(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        pixels[y * SIZE + x] = static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) |
                                (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(a) << 24);
    }
};

// Generate a procedural terrain tile texture from terrain attributes.
// Samples the full terrain heightfield for per-pixel slope/aspect interpolation,
// eliminating tile-boundary artifacts. Uses world-space noise for seamless tiling.
// water_level: elevation threshold for ocean (from EnvParams).
TilePixels generate_tile_texture(const Terrain& terrain, int tile_x, int tile_y, uint32_t seed,
                                 float water_level = 0.45f);

// Render a rectangular patch of terrain tiles to a pixel buffer.
// patch_x/patch_y: top-left tile coord; patch_w/patch_h: tile count.
// Output: patch_w*16 × patch_h*16 pixel buffer (RGBA32, caller-owned).
void render_tile_patch(const Terrain& terrain, int patch_x, int patch_y, int patch_w, int patch_h,
                       uint32_t seed, uint32_t* out_pixels, int out_stride,
                       float water_level = 0.45f);

}  // namespace sandbox
