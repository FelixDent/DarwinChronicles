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

// ── LOD levels for multi-scale rendering ────────────────────────────────────

// LOD controls which noise octaves are active at each zoom level.
// Higher LOD = more detail, only used when the camera is close enough
// that the detail would be visible (Nyquist: octave wavelength >= 2-3 pixels).
enum class TerrainLOD : uint8_t {
    Macro = 0,   // Planetary view (0.25x-0.5x): hypsometric tint + broad geology + hillshade
    Meso = 1,    // Regional view (0.5x-2x): ridge/valley structure + material provinces
    Micro = 2,   // Close-up view (2x-4x): rock strata, fractures, scree, weathering detail
};

// ── Pixel evaluator (core function) ─────────────────────────────────────────

// Evaluate a single pixel at arbitrary world-space coordinates.
// world_x/world_y: position in tile-coordinate space (0..terrain.width, 0..terrain.height).
// pixels_per_tile: how many output pixels map to one terrain tile at current zoom.
//   Macro (~1-4 px/tile), Meso (~8-16 px/tile), Micro (~32-64 px/tile).
// Returns RGBA as 0xAABBGGRR.
uint32_t eval_terrain_pixel(const Terrain& terrain, float world_x, float world_y,
                            float pixels_per_tile, uint32_t seed, float water_level = 0.45f);

// ── Patch rendering (fills a rectangular pixel buffer) ──────────────────────

// Render a rectangular region of the world to a pixel buffer.
// world_x0/world_y0: top-left corner in tile coordinates.
// world_per_pixel: world-space distance per output pixel (inverse of pixels_per_tile).
// out_w/out_h: output buffer dimensions in pixels.
// out_pixels: caller-owned RGBA32 buffer, out_stride = pixels per row.
void render_terrain_region(const Terrain& terrain, float world_x0, float world_y0,
                           float world_per_pixel, int out_w, int out_h, uint32_t seed,
                           uint32_t* out_pixels, int out_stride, float water_level = 0.45f);

// ── Legacy API (kept for compatibility) ─────────────────────────────────────

TilePixels generate_tile_texture(const Terrain& terrain, int tile_x, int tile_y, uint32_t seed,
                                 float water_level = 0.45f);

void render_tile_patch(const Terrain& terrain, int patch_x, int patch_y, int patch_w, int patch_h,
                       uint32_t seed, uint32_t* out_pixels, int out_stride,
                       float water_level = 0.45f);

}  // namespace sandbox
