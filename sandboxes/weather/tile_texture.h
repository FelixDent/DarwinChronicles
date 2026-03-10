#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>

#include "terrain_gen.h"

namespace sandbox {

// ── Tile family classification (autotile system) ─────────────────────────────
// Each tile is exactly one of: PureTerrain, Coast, or Elevation.
// Transition tiles (Coast, Elevation) contain one continuous boundary line
// from one tile edge to another, dividing the tile into two regions.

enum class TileFamily : uint8_t {
    PureTerrain,  // No boundary — uniform material fill
    Coast,        // Land/water boundary crosses this tile
    Elevation,    // Elevation contour boundary crosses this tile
};

enum class TileEdge : uint8_t { N, E, S, W };

struct EdgePair {
    TileEdge entry, exit;
};

enum class TransitionGrade : uint8_t {
    Shallow,  // broad beach / gentle ramp
    Medium,   // moderate transition
    Steep,    // cliff / sharp escarpment
};

struct TileClassification {
    TileFamily family = TileFamily::PureTerrain;
    EdgePair edges = {TileEdge::N, TileEdge::S};
    TransitionGrade grade = TransitionGrade::Shallow;
    bool flipped_polarity = false;  // Coast: water on "left" of entry→exit
                                    // Elevation: high on "left" of entry→exit
};

// Classify a tile into its family with boundary metadata.
TileClassification classify_tile(const Terrain& terrain, int tx, int ty, float water_level = 0.45f);

// 16×16 pixel tile texture, RGBA row-major
struct TilePixels {
    static constexpr int SIZE = 16;
    uint32_t pixels[SIZE * SIZE];  // 0xAABBGGRR (SDL RGBA32)

    void set(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        pixels[y * SIZE + x] = static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) |
                               (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(a) << 24);
    }
};

// ── Material type (per-tile) ────────────────────────────────────────────────

enum class Mat : uint8_t {
    Water,
    Sand,
    SoilLoam,    // generic soil cover (former Dirt)
    SoilDry,     // thin arid soil (former DryDirt)
    SoilClay,    // clay-rich soil (former Clay)
    Gravel,      // talus/scree — broken rock fragments
    Granite,     // pink-gray speckled, coarse-grained
    Basalt,      // dark blue-gray, fine-grained
    Limestone,   // cream to light gray, subtle bedding
    Sandstone,   // tan/orange, cross-bedding strata
    Shale,       // dark gray-brown, thinly fissile
    Metamorphic, // foliated light/dark banding
    MAT_COUNT
};

Mat classify_tile_mat(const TerrainTile& tile, float water_level);

// ── LOD levels for multi-scale rendering ────────────────────────────────────

enum class TerrainLOD : uint8_t {
    Macro = 0,  // Planetary view (0.25x-0.5x)
    Meso = 1,   // Regional view (0.5x-2x)
    Micro = 2,  // Close-up view (2x-4x)
};

// ── Template Atlas ──────────────────────────────────────────────────────────
// Pre-generated 16×16 sprite templates for the autotile system.
// Generated once at startup, indexed by family + parameters.

// Mask region labels for transition templates
enum class MaskRegion : uint8_t {
    RegionA = 0,   // Coast: water / Elevation: low side
    RegionB = 1,   // Coast: land  / Elevation: high side
    Boundary = 2,  // Grade-specific shore/contour treatment
};

struct TileTemplate {
    uint32_t pixels[16 * 16];  // Pre-filled RGBA (used for PureTerrain + Water)
    MaskRegion mask[16 * 16];  // Region labels for transition compositing
};

// Canonical edge pair index (only 2 needed; others derived by rotation)
// Canonical 0: N-S (straight through)
// Canonical 1: N-E (corner/arc)
enum class CanonicalPair : uint8_t { NS = 0, NE = 1, COUNT = 2 };

// How to derive any edge pair from a canonical pair + rotation
struct RotationEntry {
    CanonicalPair canonical;
    int rotation_steps;  // 0, 1, 2, 3 (×90° clockwise)
    bool flip;           // mirror the mask horizontally before rotation
};

struct TemplateAtlas {
    // PureTerrain: one template per material × 3 variants
    static constexpr int MAT_VARIANTS = 3;
    TileTemplate pure_terrain[static_cast<int>(Mat::MAT_COUNT)][MAT_VARIANTS];

    // Transition masks: [canonical_pair][grade][shape_variant]
    static constexpr int GRADE_COUNT = 3;
    static constexpr int SHAPE_VARIANTS = 3;
    TileTemplate transition_masks[static_cast<int>(CanonicalPair::COUNT)][GRADE_COUNT]
                                 [SHAPE_VARIANTS];

    // Rotation lookup: (entry, exit) → (canonical, rotation, flip)
    RotationEntry rotation_lut[4][4];  // [entry][exit]

    uint32_t seed = 0;
    bool valid = false;
};

// Generate the template atlas. Call once at startup before baking terrain cache.
void generate_template_atlas(TemplateAtlas& atlas, uint32_t seed);

// Get the global atlas (thread-safe, generated lazily or explicitly).
// Must call generate_template_atlas() before first use.
const TemplateAtlas& get_template_atlas();

// ── Pixel evaluator (core function) ─────────────────────────────────────────

uint32_t eval_terrain_pixel(const Terrain& terrain, float world_x, float world_y,
                            float pixels_per_tile, uint32_t seed, float water_level = 0.45f);

// ── Patch rendering (fills a rectangular pixel buffer) ──────────────────────

using BakeProgressFn = std::function<void(int, int)>;

void render_terrain_region(const Terrain& terrain, float world_x0, float world_y0,
                           float world_per_pixel, int out_w, int out_h, uint32_t seed,
                           uint32_t* out_pixels, int out_stride, float water_level = 0.45f,
                           BakeProgressFn progress = nullptr,
                           const std::atomic<bool>* cancel = nullptr);

// ── Legacy API (kept for compatibility) ─────────────────────────────────────

TilePixels generate_tile_texture(const Terrain& terrain, int tile_x, int tile_y, uint32_t seed,
                                 float water_level = 0.45f);

void render_tile_patch(const Terrain& terrain, int patch_x, int patch_y, int patch_w, int patch_h,
                       uint32_t seed, uint32_t* out_pixels, int out_stride,
                       float water_level = 0.45f);

}  // namespace sandbox
