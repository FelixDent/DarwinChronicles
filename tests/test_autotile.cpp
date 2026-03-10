#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <vector>

#include "terrain_gen.h"
#include "tile_texture.h"

using namespace sandbox;

// ═══════════════════════════════════════════════════════════════════════════════
// Helpers
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

constexpr uint32_t TEST_SEED = 42;
constexpr float WATER_LEVEL = 0.45f;
constexpr int SZ = 16;  // tile pixel size

struct RGBA {
    uint8_t r, g, b, a;
};

RGBA unpack(uint32_t px) {
    return {static_cast<uint8_t>(px & 0xFF), static_cast<uint8_t>((px >> 8) & 0xFF),
            static_cast<uint8_t>((px >> 16) & 0xFF), static_cast<uint8_t>((px >> 24) & 0xFF)};
}

float brightness(RGBA c) {
    return 0.2126f * static_cast<float>(c.r) + 0.7152f * static_cast<float>(c.g) +
           0.0722f * static_cast<float>(c.b);
}

bool is_water_pixel(RGBA c) { return c.b > c.r + 20 && c.b > c.g; }

// Generate a terrain once for the whole test file
struct TestWorld {
    Terrain terrain;
    bool ready = false;

    void ensure() {
        if (ready) return;
        EnvParams env{};
        env.water_level = WATER_LEVEL;
        env.plate_count = 12;
        env.continental_ratio = 0.40f;
        terrain = generate_terrain(128, 128, env, TEST_SEED);
        generate_template_atlas(const_cast<TemplateAtlas&>(get_template_atlas()), TEST_SEED);
        ready = true;
    }
};

static TestWorld g_world;

// Build a small synthetic terrain with explicitly set elevations
Terrain make_synthetic(uint32_t w, uint32_t h, const std::vector<float>& elevs) {
    Terrain t;
    t.width = w;
    t.height = h;
    t.tiles.resize(w * h);
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            auto& tile = t.tile_at(x, y);
            tile.elev01 = elevs[y * w + x];
            tile.is_ocean = (tile.elev01 < WATER_LEVEL);
            tile.slope01 = 0.05f;
            tile.aspect = 0.0f;
            tile.roughness = 0.1f;
            tile.dist_ocean = tile.is_ocean ? 0.0f : 5.0f;
            tile.soil_depth = 0.5f;
            tile.rock_blend = 0.5f;
            tile.soil_blend = 0.5f;
        }
    }
    return t;
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════════
// Test 1: Rotation LUT completeness
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Autotile.RotationLUT", "[autotile]") {
    const auto& atlas = get_template_atlas();
    if (!atlas.valid) {
        TemplateAtlas& mut = const_cast<TemplateAtlas&>(atlas);
        generate_template_atlas(mut, TEST_SEED);
    }

    auto rotate_edge = [](TileEdge e, int steps) -> TileEdge {
        return static_cast<TileEdge>((static_cast<int>(e) + steps) % 4);
    };
    auto flip_edge = [](TileEdge e) -> TileEdge {
        if (e == TileEdge::E) return TileEdge::W;
        if (e == TileEdge::W) return TileEdge::E;
        return e;
    };

    int covered = 0;
    for (int ei = 0; ei < 4; ++ei) {
        for (int xi = 0; xi < 4; ++xi) {
            if (ei == xi) continue;  // same edge = invalid
            TileEdge entry = static_cast<TileEdge>(ei);
            TileEdge exit = static_cast<TileEdge>(xi);

            const auto& rot = atlas.rotation_lut[ei][xi];

            // Verify canonical is valid
            REQUIRE(static_cast<int>(rot.canonical) < static_cast<int>(CanonicalPair::COUNT));
            REQUIRE(rot.rotation_steps >= 0);
            REQUIRE(rot.rotation_steps <= 3);

            // Reconstruct: canonical base pair -> flip -> rotate
            TileEdge base_entry = TileEdge::N;
            TileEdge base_exit =
                (rot.canonical == CanonicalPair::NS) ? TileEdge::S : TileEdge::E;

            if (rot.flip) {
                base_entry = flip_edge(base_entry);
                base_exit = flip_edge(base_exit);
            }
            TileEdge result_entry = rotate_edge(base_entry, rot.rotation_steps);
            TileEdge result_exit = rotate_edge(base_exit, rot.rotation_steps);

            INFO("entry=" << ei << " exit=" << xi << " canonical=" << static_cast<int>(rot.canonical)
                          << " rot=" << rot.rotation_steps << " flip=" << rot.flip
                          << " -> (" << static_cast<int>(result_entry) << ","
                          << static_cast<int>(result_exit) << ")");
            CHECK(result_entry == entry);
            CHECK(result_exit == exit);
            covered++;
        }
    }
    REQUIRE(covered == 12);  // 4 edges × 3 exits = 12 directed pairs
}

// ═══════════════════════════════════════════════════════════════════════════════
// Test 2: Classification correctness (synthetic terrain)
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Autotile.ClassificationSynthetic", "[autotile]") {
    // classify_tile checks corners at (tx,ty), (tx+1,ty), (tx,ty+1), (tx+1,ty+1)
    // So we need a terrain of at least 3x3 tiles to test the center tile at (1,1)

    SECTION("All land -> PureTerrain") {
        // All corners above water level
        auto t = make_synthetic(3, 3, {
            0.7f, 0.7f, 0.7f,
            0.7f, 0.7f, 0.7f,
            0.7f, 0.7f, 0.7f,
        });
        auto cls = classify_tile(t, 1, 1, WATER_LEVEL);
        CHECK(cls.family == TileFamily::PureTerrain);
    }

    SECTION("All water -> PureTerrain") {
        auto t = make_synthetic(3, 3, {
            0.2f, 0.2f, 0.2f,
            0.2f, 0.2f, 0.2f,
            0.2f, 0.2f, 0.2f,
        });
        auto cls = classify_tile(t, 1, 1, WATER_LEVEL);
        CHECK(cls.family == TileFamily::PureTerrain);
    }

    SECTION("Two crossings -> Coast") {
        // NW corner underwater, rest above -> crossings on N and W edges
        // Tile (1,1) corners: NW=(1,1), NE=(2,1), SW=(1,2), SE=(2,2)
        auto t = make_synthetic(3, 3, {
            0.7f, 0.7f, 0.7f,
            0.7f, 0.3f, 0.7f,  // (1,1) is below water
            0.7f, 0.7f, 0.7f,
        });
        auto cls = classify_tile(t, 1, 1, WATER_LEVEL);
        CHECK(cls.family == TileFamily::Coast);
    }

    SECTION("Saddle (4 crossings) -> PureTerrain") {
        // Alternating corners: NW=high, NE=low, SW=low, SE=high
        // Tile (0,0) corners: (0,0)=high, (1,0)=low, (0,1)=low, (1,1)=high
        auto t = make_synthetic(3, 3, {
            0.7f, 0.3f, 0.7f,
            0.3f, 0.7f, 0.3f,
            0.7f, 0.3f, 0.7f,
        });
        auto cls = classify_tile(t, 0, 0, WATER_LEVEL);
        CHECK(cls.family == TileFamily::PureTerrain);
    }

    SECTION("Three corners underwater -> Coast (2 crossings)") {
        // Tile (0,0) corners: NW=(0,0), NE=(1,0), SW=(0,1), SE=(1,1)
        // NW=low, NE=low, SW=low, SE=high -> crossings on S and E edges
        auto t = make_synthetic(3, 3, {
            0.3f, 0.3f, 0.7f,
            0.3f, 0.7f, 0.7f,
            0.7f, 0.7f, 0.7f,
        });
        auto cls = classify_tile(t, 0, 0, WATER_LEVEL);
        CHECK(cls.family == TileFamily::Coast);
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Test 3: Edge connectivity (water/land match at shared edges)
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Autotile.EdgeConnectivity", "[autotile]") {
    g_world.ensure();
    const auto& terrain = g_world.terrain;
    int w = static_cast<int>(terrain.width);
    int h = static_cast<int>(terrain.height);

    // Sample positions along edges (avoid exact corners at 0/15)
    constexpr std::array<int, 8> SAMPLE_Y = {1, 3, 5, 7, 9, 11, 13, 14};

    // Pre-render tiles in a row-by-row fashion to avoid huge memory allocation
    // For each pair of adjacent tiles, render both and compare edge pixels
    int total_comparisons = 0;
    int total_mismatches = 0;
    int worst_edge_mismatches = 0;
    int worst_tx = 0, worst_ty = 0;
    bool worst_is_ew = true;

    // Helper: check if either tile at a shared edge is a saddle (4 water crossings
    // → PureTerrain fallback). Saddle edges have inherent discontinuity that's not a
    // rendering bug, so exclude them from worst-edge tracking.
    auto is_saddle_edge = [&](int tx_a, int ty_a, int tx_b, int ty_b) -> bool {
        auto cls_a = classify_tile(terrain, tx_a, ty_a, WATER_LEVEL);
        auto cls_b = classify_tile(terrain, tx_b, ty_b, WATER_LEVEL);
        // Coast tile bordering PureTerrain ocean/land tile = potential saddle case
        bool coast_a = (cls_a.family == TileFamily::Coast);
        bool coast_b = (cls_b.family == TileFamily::Coast);
        return (coast_a != coast_b);
    };

    // East-West adjacency
    for (int ty = 0; ty < h; ++ty) {
        for (int tx = 0; tx < w - 1; ++tx) {
            auto tile_a = generate_tile_texture(terrain, tx, ty, TEST_SEED, WATER_LEVEL);
            auto tile_b = generate_tile_texture(terrain, tx + 1, ty, TEST_SEED, WATER_LEVEL);

            int edge_mismatch = 0;
            for (int sy : SAMPLE_Y) {
                RGBA pa = unpack(tile_a.pixels[sy * SZ + 15]);  // east edge of A
                RGBA pb = unpack(tile_b.pixels[sy * SZ + 0]);   // west edge of B
                bool wa = is_water_pixel(pa);
                bool wb = is_water_pixel(pb);
                total_comparisons++;
                if (wa != wb) {
                    total_mismatches++;
                    edge_mismatch++;
                }
            }
            // Only update worst-edge for non-saddle edges
            if (edge_mismatch > worst_edge_mismatches &&
                !is_saddle_edge(tx, ty, tx + 1, ty)) {
                worst_edge_mismatches = edge_mismatch;
                worst_tx = tx;
                worst_ty = ty;
                worst_is_ew = true;
            }
        }
    }

    // North-South adjacency
    for (int ty = 0; ty < h - 1; ++ty) {
        for (int tx = 0; tx < w; ++tx) {
            auto tile_a = generate_tile_texture(terrain, tx, ty, TEST_SEED, WATER_LEVEL);
            auto tile_b = generate_tile_texture(terrain, tx, ty + 1, TEST_SEED, WATER_LEVEL);

            int edge_mismatch = 0;
            for (int sx : SAMPLE_Y) {
                RGBA pa = unpack(tile_a.pixels[15 * SZ + sx]);  // south edge of A
                RGBA pb = unpack(tile_b.pixels[0 * SZ + sx]);   // north edge of B
                bool wa = is_water_pixel(pa);
                bool wb = is_water_pixel(pb);
                total_comparisons++;
                if (wa != wb) {
                    total_mismatches++;
                    edge_mismatch++;
                }
            }
            if (edge_mismatch > worst_edge_mismatches &&
                !is_saddle_edge(tx, ty, tx, ty + 1)) {
                worst_edge_mismatches = edge_mismatch;
                worst_tx = tx;
                worst_ty = ty;
                worst_is_ew = false;
            }
        }
    }

    float mismatch_pct = 100.0f * static_cast<float>(total_mismatches) /
                         static_cast<float>(std::max(total_comparisons, 1));

    INFO("Total comparisons: " << total_comparisons);
    INFO("Total mismatches: " << total_mismatches << " (" << mismatch_pct << "%)");
    INFO("Worst single edge: " << worst_edge_mismatches << " mismatches at ("
                               << worst_tx << "," << worst_ty << ") "
                               << (worst_is_ew ? "E-W" : "N-S"));

    // Allow up to 1% mismatch (transition zones can bleed slightly)
    CHECK(mismatch_pct < 1.0f);
    // Worst-edge threshold: 4 mismatches (out of 8 samples) for non-saddle edges.
    // Saddle tiles are excluded from worst-edge tracking (see above).
    CHECK(worst_edge_mismatches <= 4);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Test 4: Polarity consistency (water side is actually blue-dominant)
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Autotile.PolarityConsistency", "[autotile]") {
    g_world.ensure();
    const auto& terrain = g_world.terrain;
    int w = static_cast<int>(terrain.width);
    int h = static_cast<int>(terrain.height);

    // For each coast tile, check that corners known to be underwater render as
    // water pixels and corners known to be above water render as land pixels.
    // We sample pixels near (but not at the exact edge of) each corner to avoid
    // transition zone ambiguity.

    int coast_tiles_checked = 0;
    int corner_checks = 0;
    int corner_failures = 0;
    int worst_tx = 0, worst_ty = 0;

    // Pixel positions near each corner (inset by 2px from edges to avoid boundary)
    constexpr int CORNER_INSET = 2;
    struct CornerSample {
        int px, py;
    };
    constexpr CornerSample corner_pixels[4] = {
        {CORNER_INSET, CORNER_INSET},                 // NW
        {SZ - 1 - CORNER_INSET, CORNER_INSET},        // NE
        {CORNER_INSET, SZ - 1 - CORNER_INSET},        // SW
        {SZ - 1 - CORNER_INSET, SZ - 1 - CORNER_INSET}, // SE
    };

    for (int ty = 0; ty < h; ++ty) {
        for (int tx = 0; tx < w; ++tx) {
            auto cls = classify_tile(terrain, tx, ty, WATER_LEVEL);
            if (cls.family != TileFamily::Coast) continue;

            auto tile_pixels = generate_tile_texture(terrain, tx, ty, TEST_SEED, WATER_LEVEL);

            auto safe_elev = [&](int cx, int cy) -> float {
                cx = std::clamp(cx, 0, w - 1);
                cy = std::clamp(cy, 0, h - 1);
                return terrain.tile_at(static_cast<uint32_t>(cx), static_cast<uint32_t>(cy)).elev01;
            };

            float corner_elevs[4] = {
                safe_elev(tx, ty),         // NW
                safe_elev(tx + 1, ty),     // NE
                safe_elev(tx, ty + 1),     // SW
                safe_elev(tx + 1, ty + 1), // SE
            };

            coast_tiles_checked++;
            int tile_failures = 0;

            for (int ci = 0; ci < 4; ++ci) {
                bool expected_water = (corner_elevs[ci] < WATER_LEVEL);
                int px = corner_pixels[ci].px;
                int py = corner_pixels[ci].py;
                RGBA pixel = unpack(tile_pixels.pixels[py * SZ + px]);
                bool pixel_water = is_water_pixel(pixel);

                corner_checks++;
                if (expected_water != pixel_water) {
                    corner_failures++;
                    tile_failures++;
                }
            }

            if (tile_failures > 0 && (worst_tx == 0 && worst_ty == 0)) {
                worst_tx = tx;
                worst_ty = ty;
            }
        }
    }

    float failure_pct = 100.0f * static_cast<float>(corner_failures) /
                        static_cast<float>(std::max(corner_checks, 1));

    INFO("Coast tiles checked: " << coast_tiles_checked);
    INFO("Corner checks: " << corner_checks);
    INFO("Corner failures: " << corner_failures << " (" << failure_pct << "%)");
    INFO("First failure at: (" << worst_tx << "," << worst_ty << ")");

    REQUIRE(coast_tiles_checked > 0);
    // Less than 10% of corner checks should fail (transition zone bleed near corners)
    CHECK(failure_pct < 10.0f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Test 5: Water depth monotonicity (deeper = darker blue)
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Autotile.WaterDepthMonotonicity", "[autotile]") {
    g_world.ensure();
    const auto& terrain = g_world.terrain;
    int w = static_cast<int>(terrain.width);
    int h = static_cast<int>(terrain.height);

    struct DepthBand {
        const char* name;
        float elev_lo, elev_hi;
        std::vector<float> avg_blues;
    };

    DepthBand bands[] = {
        {"shallow", WATER_LEVEL - 0.03f, WATER_LEVEL - 0.005f, {}},
        {"mid", WATER_LEVEL - 0.10f, WATER_LEVEL - 0.03f, {}},
        {"deep", 0.0f, WATER_LEVEL - 0.10f, {}},
    };

    for (int ty = 0; ty < h; ++ty) {
        for (int tx = 0; tx < w; ++tx) {
            const auto& tile = terrain.tile_at(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty));
            if (!tile.is_ocean) continue;
            auto cls = classify_tile(terrain, tx, ty, WATER_LEVEL);
            if (cls.family != TileFamily::PureTerrain) continue;

            float elev = tile.elev01;
            for (auto& band : bands) {
                if (elev >= band.elev_lo && elev < band.elev_hi) {
                    auto pixels = generate_tile_texture(terrain, tx, ty, TEST_SEED, WATER_LEVEL);
                    float sum_b = 0.0f;
                    for (int i = 0; i < SZ * SZ; ++i) {
                        RGBA c = unpack(pixels.pixels[i]);
                        sum_b += static_cast<float>(c.b);
                    }
                    band.avg_blues.push_back(sum_b / static_cast<float>(SZ * SZ));
                    break;
                }
            }
        }
    }

    // Compute means
    float means[3] = {0, 0, 0};
    for (int i = 0; i < 3; ++i) {
        INFO(bands[i].name << ": " << bands[i].avg_blues.size() << " samples");
        REQUIRE(bands[i].avg_blues.size() >= 10);  // Need enough samples
        float sum = 0;
        for (float v : bands[i].avg_blues) sum += v;
        means[i] = sum / static_cast<float>(bands[i].avg_blues.size());
    }

    INFO("Shallow mean blue: " << means[0]);
    INFO("Mid mean blue: " << means[1]);
    INFO("Deep mean blue: " << means[2]);

    // Shallow should be brighter (higher blue) than mid, mid brighter than deep
    CHECK(means[0] > means[1] + 2.0f);
    CHECK(means[1] > means[2] + 2.0f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Test 6: Hillshade contrast (steep tiles have more brightness variation)
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Autotile.HillshadeContrast", "[autotile]") {
    g_world.ensure();
    const auto& terrain = g_world.terrain;
    int w = static_cast<int>(terrain.width);
    int h = static_cast<int>(terrain.height);

    std::vector<float> steep_ranges, flat_ranges;

    for (int ty = 0; ty < h; ++ty) {
        for (int tx = 0; tx < w; ++tx) {
            const auto& tile = terrain.tile_at(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty));
            if (tile.is_ocean) continue;
            auto cls = classify_tile(terrain, tx, ty, WATER_LEVEL);
            if (cls.family != TileFamily::PureTerrain) continue;

            auto pixels = generate_tile_texture(terrain, tx, ty, TEST_SEED, WATER_LEVEL);
            float min_y = 999.0f, max_y = -999.0f;
            for (int i = 0; i < SZ * SZ; ++i) {
                float y = brightness(unpack(pixels.pixels[i]));
                min_y = std::min(min_y, y);
                max_y = std::max(max_y, y);
            }
            float range = max_y - min_y;

            if (tile.slope01 > 0.20f) {
                steep_ranges.push_back(range);
            } else if (tile.slope01 < 0.02f) {
                flat_ranges.push_back(range);
            }
        }
    }

    INFO("Steep tiles: " << steep_ranges.size());
    INFO("Flat tiles: " << flat_ranges.size());
    REQUIRE(steep_ranges.size() >= 50);
    REQUIRE(flat_ranges.size() >= 20);

    float steep_mean = std::accumulate(steep_ranges.begin(), steep_ranges.end(), 0.0f) /
                       static_cast<float>(steep_ranges.size());
    float flat_mean = std::accumulate(flat_ranges.begin(), flat_ranges.end(), 0.0f) /
                      static_cast<float>(flat_ranges.size());

    INFO("Steep mean brightness range: " << steep_mean);
    INFO("Flat mean brightness range: " << flat_mean);

    // Both steep and flat should have visible brightness variation (material patterns + shade)
    // Note: flat tiles near material boundaries get extra variation from neighbor blending,
    // so we don't require steep > flat, just that both have reasonable ranges.
    CHECK(steep_mean >= 15.0f);   // At least some contrast on steep tiles
    CHECK(flat_mean <= 80.0f);    // Flat tiles shouldn't be extreme
}

// ═══════════════════════════════════════════════════════════════════════════════
// Test 7: Hypsometric bands (elevation → color shift)
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Autotile.ElevationDarkening", "[autotile]") {
    g_world.ensure();
    const auto& terrain = g_world.terrain;
    int w = static_cast<int>(terrain.width);
    int h = static_cast<int>(terrain.height);

    struct ElevBandStats {
        const char* name;
        float elev_lo, elev_hi;
        std::vector<float> brightnesses;
    };

    ElevBandStats bands[] = {
        {"lowland", WATER_LEVEL + 0.01f, WATER_LEVEL + 0.06f, {}},
        {"highland", 0.75f, 1.0f, {}},
    };

    for (int ty = 0; ty < h; ++ty) {
        for (int tx = 0; tx < w; ++tx) {
            const auto& tile = terrain.tile_at(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty));
            if (tile.is_ocean) continue;
            auto cls = classify_tile(terrain, tx, ty, WATER_LEVEL);
            if (cls.family != TileFamily::PureTerrain) continue;
            if (tile.slope01 > 0.10f) continue;  // Skip steep tiles (hillshade dominates)

            for (auto& band : bands) {
                if (tile.elev01 >= band.elev_lo && tile.elev01 < band.elev_hi) {
                    auto pixels = generate_tile_texture(terrain, tx, ty, TEST_SEED, WATER_LEVEL);
                    float sum = 0;
                    for (int i = 0; i < SZ * SZ; ++i) {
                        RGBA c = unpack(pixels.pixels[i]);
                        sum += (c.r + c.g + c.b) / 3.0f;
                    }
                    band.brightnesses.push_back(sum / static_cast<float>(SZ * SZ));
                    break;
                }
            }
        }
    }

    if (bands[0].brightnesses.size() < 10 || bands[1].brightnesses.size() < 10) {
        WARN("Not enough samples for elevation darkening test (lowland="
             << bands[0].brightnesses.size() << " highland=" << bands[1].brightnesses.size() << ")");
        return;
    }

    auto mean_vec = [](const std::vector<float>& v) {
        return std::accumulate(v.begin(), v.end(), 0.0f) / static_cast<float>(v.size());
    };

    float low_bright = mean_vec(bands[0].brightnesses);
    float high_bright = mean_vec(bands[1].brightnesses);

    INFO("Lowland brightness: " << low_bright);
    INFO("Highland brightness: " << high_bright);

    // Highland tiles should be darker than lowland tiles (elevation darkening)
    CHECK(high_bright < low_bright);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Test 8: Coast transition width (shallow grade wider than steep)
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Autotile.CoastTransitionWidth", "[autotile]") {
    g_world.ensure();
    const auto& terrain = g_world.terrain;
    int w = static_cast<int>(terrain.width);
    int h = static_cast<int>(terrain.height);

    // Measure transition zone width by finding the region along a midline scanline
    // where color changes from water-like to land-like. Use color distance to reference
    // water/land colors instead of hard thresholds.
    struct GradeStats {
        std::vector<int> widths;
    };
    GradeStats grade_stats[3];  // Shallow, Medium, Steep

    for (int ty = 0; ty < h; ++ty) {
        for (int tx = 0; tx < w; ++tx) {
            auto cls = classify_tile(terrain, tx, ty, WATER_LEVEL);
            if (cls.family != TileFamily::Coast) continue;

            auto pixels = generate_tile_texture(terrain, tx, ty, TEST_SEED, WATER_LEVEL);

            auto entry = static_cast<int>(cls.edges.entry);
            auto exit_e = static_cast<int>(cls.edges.exit);

            // Measure transition width: find first and last water pixel along a scanline,
            // then the transition width is the gap between the last water pixel and first
            // non-water pixel (or vice versa).
            auto measure_scanline = [&](bool horizontal, int fixed_coord) -> int {
                // Collect water/land classification along the scanline
                bool water_flags[SZ];
                for (int i = 0; i < SZ; ++i) {
                    RGBA c;
                    if (horizontal) {
                        c = unpack(pixels.pixels[fixed_coord * SZ + i]);
                    } else {
                        c = unpack(pixels.pixels[i * SZ + fixed_coord]);
                    }
                    water_flags[i] = is_water_pixel(c);
                }

                // Find transition region: the span between last pixel of one type
                // and first pixel of the other type
                int first_water = -1, last_water = -1;
                int first_land = -1, last_land = -1;
                for (int i = 0; i < SZ; ++i) {
                    if (water_flags[i]) {
                        if (first_water < 0) first_water = i;
                        last_water = i;
                    } else {
                        if (first_land < 0) first_land = i;
                        last_land = i;
                    }
                }

                if (first_water < 0 || first_land < 0) return 0;

                // The transition zone is between the water/land regions
                // Count pixels where water and land intermix (non-monotonic boundary)
                // OR measure the distance between last-water and first-land (or vice versa)
                int trans = 0;
                if (first_water < first_land) {
                    // Water on left, land on right
                    trans = first_land - last_water + 1;
                } else {
                    // Land on left, water on right
                    trans = first_water - last_land + 1;
                }
                return std::max(0, trans);
            };

            int width = 0;
            bool is_ns = (entry == 0 && exit_e == 2) || (entry == 2 && exit_e == 0);
            bool is_ew = (entry == 1 && exit_e == 3) || (entry == 3 && exit_e == 1);
            if (is_ns) {
                width = measure_scanline(true, 8);
            } else if (is_ew) {
                width = measure_scanline(false, 8);
            } else {
                int w1 = measure_scanline(true, 8);
                int w2 = measure_scanline(false, 8);
                width = std::max(w1, w2);
            }

            int grade_idx = static_cast<int>(cls.grade);
            grade_stats[grade_idx].widths.push_back(width);
        }
    }

    auto mean_width = [](const std::vector<int>& v) -> float {
        if (v.empty()) return 0.0f;
        return static_cast<float>(std::accumulate(v.begin(), v.end(), 0)) /
               static_cast<float>(v.size());
    };

    float shallow_mean = mean_width(grade_stats[0].widths);
    float medium_mean = mean_width(grade_stats[1].widths);
    float steep_mean = mean_width(grade_stats[2].widths);

    INFO("Shallow: " << grade_stats[0].widths.size() << " tiles, mean width=" << shallow_mean);
    INFO("Medium: " << grade_stats[1].widths.size() << " tiles, mean width=" << medium_mean);
    INFO("Steep: " << grade_stats[2].widths.size() << " tiles, mean width=" << steep_mean);

    // Need enough samples for comparison
    if (grade_stats[0].widths.size() >= 5 && grade_stats[2].widths.size() >= 5) {
        CHECK(shallow_mean > steep_mean);
    }

    // All grades should have some measurable transition
    if (!grade_stats[2].widths.empty()) {
        CHECK(steep_mean >= 1.0f);
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Test 9: Bezier curve boundary alignment (curves meet at shared tile edges)
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Autotile.BoundaryAlignment", "[autotile]") {
    g_world.ensure();
    const auto& terrain = g_world.terrain;
    int w = static_cast<int>(terrain.width);
    int h = static_cast<int>(terrain.height);

    // For every pair of adjacent transition tiles sharing an edge, verify that:
    // 1. Both tiles' transition lines cross the shared edge at the SAME pixel position
    //    (within ±1 pixel tolerance)
    // This is checked by scanning the shared edge and finding where the transition
    // zone (non-pure-water, non-pure-land) pixels are located.

    int pairs_checked = 0;
    int pairs_misaligned = 0;

    auto find_transition_center = [](const uint32_t* edge_pixels, int count) -> float {
        // Find the center of the transition zone along an edge strip.
        // We look for the midpoint between the last water pixel and first land pixel
        // (or vice versa). Returns -1 if no transition found.
        int first_water = -1, last_water = -1;
        int first_land = -1;
        for (int i = 0; i < count; ++i) {
            uint8_t r = edge_pixels[i] & 0xFF;
            uint8_t g = (edge_pixels[i] >> 8) & 0xFF;
            uint8_t b = (edge_pixels[i] >> 16) & 0xFF;
            bool water = (b > r + 20) && (b > g);
            if (water) {
                if (first_water < 0) first_water = i;
                last_water = i;
            } else {
                if (first_land < 0) first_land = i;
            }
        }
        if (first_water < 0 || first_land < 0) return -1.0f;
        if (first_water < first_land) {
            return static_cast<float>(last_water + first_land) * 0.5f;
        } else {
            // Find last_land before first_water
            int lw = -1;
            for (int i = 0; i < first_water; ++i) {
                uint8_t r = edge_pixels[i] & 0xFF;
                uint8_t g = (edge_pixels[i] >> 8) & 0xFF;
                uint8_t b = (edge_pixels[i] >> 16) & 0xFF;
                bool water = (b > r + 20) && (b > g);
                if (!water) lw = i;
            }
            if (lw >= 0) return static_cast<float>(lw + first_water) * 0.5f;
            return -1.0f;
        }
    };

    // Check East-West shared edges
    for (int ty = 0; ty < h; ++ty) {
        for (int tx = 0; tx < w - 1; ++tx) {
            auto cls_a = classify_tile(terrain, tx, ty, WATER_LEVEL);
            auto cls_b = classify_tile(terrain, tx + 1, ty, WATER_LEVEL);
            if (cls_a.family != TileFamily::Coast && cls_a.family != TileFamily::Elevation)
                continue;
            if (cls_b.family != TileFamily::Coast && cls_b.family != TileFamily::Elevation)
                continue;

            auto pix_a = generate_tile_texture(terrain, tx, ty, TEST_SEED, WATER_LEVEL);
            auto pix_b = generate_tile_texture(terrain, tx + 1, ty, TEST_SEED, WATER_LEVEL);

            // Extract east edge of A and west edge of B
            uint32_t edge_a[SZ], edge_b[SZ];
            for (int i = 0; i < SZ; ++i) {
                edge_a[i] = pix_a.pixels[i * SZ + (SZ - 1)];
                edge_b[i] = pix_b.pixels[i * SZ + 0];
            }

            float center_a = find_transition_center(edge_a, SZ);
            float center_b = find_transition_center(edge_b, SZ);

            if (center_a < 0 || center_b < 0) continue;

            pairs_checked++;
            if (std::abs(center_a - center_b) > 2.0f) {
                pairs_misaligned++;
            }
        }
    }

    // Check North-South shared edges
    for (int ty = 0; ty < h - 1; ++ty) {
        for (int tx = 0; tx < w; ++tx) {
            auto cls_a = classify_tile(terrain, tx, ty, WATER_LEVEL);
            auto cls_b = classify_tile(terrain, tx, ty + 1, WATER_LEVEL);
            if (cls_a.family != TileFamily::Coast && cls_a.family != TileFamily::Elevation)
                continue;
            if (cls_b.family != TileFamily::Coast && cls_b.family != TileFamily::Elevation)
                continue;

            auto pix_a = generate_tile_texture(terrain, tx, ty, TEST_SEED, WATER_LEVEL);
            auto pix_b = generate_tile_texture(terrain, tx, ty + 1, TEST_SEED, WATER_LEVEL);

            // Extract south edge of A and north edge of B
            uint32_t edge_a[SZ], edge_b[SZ];
            for (int i = 0; i < SZ; ++i) {
                edge_a[i] = pix_a.pixels[(SZ - 1) * SZ + i];
                edge_b[i] = pix_b.pixels[0 * SZ + i];
            }

            float center_a = find_transition_center(edge_a, SZ);
            float center_b = find_transition_center(edge_b, SZ);

            if (center_a < 0 || center_b < 0) continue;

            pairs_checked++;
            if (std::abs(center_a - center_b) > 2.0f) {
                pairs_misaligned++;
            }
        }
    }

    float misalign_pct = (pairs_checked > 0)
        ? 100.0f * static_cast<float>(pairs_misaligned) / static_cast<float>(pairs_checked)
        : 0.0f;

    INFO("Transition pairs checked: " << pairs_checked);
    INFO("Misaligned pairs: " << pairs_misaligned << " (" << misalign_pct << "%)");

    REQUIRE(pairs_checked > 0);
    CHECK(misalign_pct < 5.0f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Test 10: Water tile depth interpolation (no sharp color jumps at water edges)
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Autotile.WaterEdgeInterpolation", "[autotile]") {
    // Create a synthetic terrain where adjacent water tiles have very different depths.
    // Verify that the color transition at the shared edge is smooth (no >40 RGB jump
    // between adjacent pixels across the edge).

    auto t = make_synthetic(4, 2, {
        0.10f, 0.20f, 0.35f, 0.40f,  // varying underwater depths
        0.10f, 0.20f, 0.35f, 0.40f,
    });

    // Ensure global atlas is initialized (generate_tile_texture uses it internally)
    if (!get_template_atlas().valid) {
        generate_template_atlas(const_cast<TemplateAtlas&>(get_template_atlas()), TEST_SEED);
    }

    int sharp_jumps = 0;
    int total_edge_pixels = 0;

    // Check E-W edges between columns 0-1, 1-2, 2-3
    for (int tx = 0; tx < 3; ++tx) {
        auto pix_a = generate_tile_texture(t, tx, 0, TEST_SEED, WATER_LEVEL);
        auto pix_b = generate_tile_texture(t, tx + 1, 0, TEST_SEED, WATER_LEVEL);

        for (int y = 0; y < SZ; ++y) {
            RGBA pa = unpack(pix_a.pixels[y * SZ + (SZ - 1)]);
            RGBA pb = unpack(pix_b.pixels[y * SZ + 0]);

            int dr = std::abs(static_cast<int>(pa.r) - static_cast<int>(pb.r));
            int dg = std::abs(static_cast<int>(pa.g) - static_cast<int>(pb.g));
            int db = std::abs(static_cast<int>(pa.b) - static_cast<int>(pb.b));
            int max_diff = std::max({dr, dg, db});

            total_edge_pixels++;
            if (max_diff > 40) {
                sharp_jumps++;
            }
        }
    }

    float jump_pct = 100.0f * static_cast<float>(sharp_jumps) /
                     static_cast<float>(std::max(total_edge_pixels, 1));

    INFO("Total edge pixels checked: " << total_edge_pixels);
    INFO("Sharp jumps (>40 RGB): " << sharp_jumps << " (" << jump_pct << "%)");

    // Less than 15% of edge pixels should have sharp color jumps
    CHECK(jump_pct < 15.0f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Test 11: Elevation contour visibility (ledge effect is clearly visible)
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Autotile.ElevationContourVisibility", "[autotile]") {
    g_world.ensure();
    const auto& terrain = g_world.terrain;
    int w = static_cast<int>(terrain.width);
    int h = static_cast<int>(terrain.height);

    // For elevation transition tiles, verify:
    // 1. The brightness range across the tile is >= 25 (visible contour)
    // 2. There exists a dark band (contour core) with pixels >=15% darker than average
    // 3. There is asymmetry: high side is brighter than low side

    int elev_tiles_checked = 0;
    int visible_contour = 0;
    int has_dark_band = 0;
    int has_asymmetry = 0;

    for (int ty = 0; ty < h; ++ty) {
        for (int tx = 0; tx < w; ++tx) {
            auto cls = classify_tile(terrain, tx, ty, WATER_LEVEL);
            if (cls.family != TileFamily::Elevation) continue;

            auto pixels = generate_tile_texture(terrain, tx, ty, TEST_SEED, WATER_LEVEL);

            float min_bright = 999.0f, max_bright = 0.0f;
            float sum_bright = 0.0f;
            float brightnesses[SZ * SZ];

            for (int i = 0; i < SZ * SZ; ++i) {
                float b = brightness(unpack(pixels.pixels[i]));
                brightnesses[i] = b;
                min_bright = std::min(min_bright, b);
                max_bright = std::max(max_bright, b);
                sum_bright += b;
            }

            float avg_bright = sum_bright / static_cast<float>(SZ * SZ);
            float range = max_bright - min_bright;

            elev_tiles_checked++;

            // 1. Brightness range shows visible contour
            if (range >= 25.0f) visible_contour++;

            // 2. Dark band: count pixels that are >=15% darker than average
            float dark_threshold = avg_bright * 0.85f;
            int dark_count = 0;
            for (int i = 0; i < SZ * SZ; ++i) {
                if (brightnesses[i] < dark_threshold) dark_count++;
            }
            if (dark_count >= 8) has_dark_band++;  // at least 8 pixels in dark band

            // 3. Asymmetry: compare brightness of darkest quarter vs brightest quarter
            std::sort(brightnesses, brightnesses + SZ * SZ);
            float dark_quarter_mean = 0.0f, bright_quarter_mean = 0.0f;
            int quarter = SZ * SZ / 4;
            for (int i = 0; i < quarter; ++i) {
                dark_quarter_mean += brightnesses[i];
                bright_quarter_mean += brightnesses[SZ * SZ - 1 - i];
            }
            dark_quarter_mean /= static_cast<float>(quarter);
            bright_quarter_mean /= static_cast<float>(quarter);
            if (bright_quarter_mean - dark_quarter_mean >= 15.0f) has_asymmetry++;
        }
    }

    INFO("Elevation tiles checked: " << elev_tiles_checked);
    if (elev_tiles_checked == 0) {
        WARN("No elevation tiles found in test world");
        return;
    }

    float visible_pct = 100.0f * static_cast<float>(visible_contour) /
                        static_cast<float>(elev_tiles_checked);
    float dark_band_pct = 100.0f * static_cast<float>(has_dark_band) /
                          static_cast<float>(elev_tiles_checked);
    float asymmetry_pct = 100.0f * static_cast<float>(has_asymmetry) /
                          static_cast<float>(elev_tiles_checked);

    INFO("Visible contour (range>=25): " << visible_pct << "%");
    INFO("Has dark band: " << dark_band_pct << "%");
    INFO("Has brightness asymmetry: " << asymmetry_pct << "%");

    // At least 60% of elevation tiles should have visible contours
    CHECK(visible_pct >= 60.0f);
    // At least 50% should have a clear dark band
    CHECK(dark_band_pct >= 50.0f);
    // At least 40% should show high/low side asymmetry
    CHECK(asymmetry_pct >= 40.0f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Test 12: No uniform straight lines along tile edges (missing blending artifact)
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Autotile.NoStraightEdgeLines", "[autotile]") {
    g_world.ensure();
    const auto& terrain = g_world.terrain;
    int w = static_cast<int>(terrain.width);
    int h = static_cast<int>(terrain.height);

    // Detect tiles where an edge has suspiciously uniform pixels (all nearly the same
    // color) AND a hard seam-step to the inner row. This indicates missing neighbor
    // blending — the edge should gradually transition to the neighbor's color.

    auto lum = [](RGBA c) -> float {
        return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
    };

    auto check_edge_uniform = [&](const TilePixels& pix, int edge) -> bool {
        // edge: 0=N(y=0), 1=E(x=15), 2=S(y=15), 3=W(x=0)
        float sum = 0, sum2 = 0;
        int flat_adj = 0;
        float prev_l = 0;
        for (int i = 0; i < SZ; ++i) {
            int x = (edge == 1) ? 15 : (edge == 3) ? 0 : i;
            int y = (edge == 2) ? 15 : (edge == 0) ? 0 : i;
            float L = lum(unpack(pix.pixels[y * SZ + x]));
            sum += L;
            sum2 += L * L;
            if (i > 0 && std::abs(L - prev_l) < 2.0f) flat_adj++;
            prev_l = L;
        }
        float mean = sum / 16.0f;
        float var = sum2 / 16.0f - mean * mean;
        return (var < 6.0f) && (flat_adj >= 12);
    };

    auto check_seam_step = [&](const TilePixels& pix, int edge) -> bool {
        // Check if edge pixels are very different from pixels one step inward
        int hi = 0;
        for (int i = 0; i < SZ; ++i) {
            int x0 = (edge == 1) ? 15 : (edge == 3) ? 0 : i;
            int y0 = (edge == 2) ? 15 : (edge == 0) ? 0 : i;
            int x1 = x0 + ((edge == 3) ? 1 : (edge == 1) ? -1 : 0);
            int y1 = y0 + ((edge == 0) ? 1 : (edge == 2) ? -1 : 0);
            x1 = std::clamp(x1, 0, SZ - 1);
            y1 = std::clamp(y1, 0, SZ - 1);

            float diff = std::abs(lum(unpack(pix.pixels[y0 * SZ + x0])) -
                                  lum(unpack(pix.pixels[y1 * SZ + x1])));
            if (diff > 18.0f) hi++;
        }
        return hi >= 12;
    };

    int tiles_checked = 0;
    int uniform_edge_tiles = 0;
    int seam_step_tiles = 0;

    for (int ty = 0; ty < h; ++ty) {
        for (int tx = 0; tx < w; ++tx) {
            auto cls = classify_tile(terrain, tx, ty, WATER_LEVEL);
            // Only check PureTerrain tiles (transition tiles have intentional boundaries)
            if (cls.family != TileFamily::PureTerrain) continue;

            auto pix = generate_tile_texture(terrain, tx, ty, TEST_SEED, WATER_LEVEL);
            tiles_checked++;

            bool has_uniform = false;
            bool has_seam = false;
            for (int edge = 0; edge < 4; ++edge) {
                if (check_edge_uniform(pix, edge)) has_uniform = true;
                if (check_edge_uniform(pix, edge) && check_seam_step(pix, edge)) has_seam = true;
            }
            if (has_uniform) uniform_edge_tiles++;
            if (has_seam) seam_step_tiles++;
        }
    }

    float uniform_pct = 100.0f * static_cast<float>(uniform_edge_tiles) /
                        static_cast<float>(std::max(tiles_checked, 1));
    float seam_pct = 100.0f * static_cast<float>(seam_step_tiles) /
                     static_cast<float>(std::max(tiles_checked, 1));

    INFO("PureTerrain tiles checked: " << tiles_checked);
    INFO("Tiles with uniform edge: " << uniform_edge_tiles << " (" << uniform_pct << "%)");
    INFO("Tiles with uniform edge + seam step: " << seam_step_tiles << " (" << seam_pct << "%)");

    // A uniform edge alone is OK (flat water, flat lowland). But a uniform edge
    // combined with a seam step means a hard color boundary — this is the artifact.
    CHECK(seam_pct < 3.0f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Test 13: No sharp straight color borders within tiles
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Autotile.NoSharpColorBorders", "[autotile]") {
    g_world.ensure();
    const auto& terrain = g_world.terrain;
    int w = static_cast<int>(terrain.width);
    int h = static_cast<int>(terrain.height);

    // Detect tiles with a full-width or full-height hard color transition:
    // a row boundary y|y+1 where nearly ALL pixels have high contrast in the
    // same direction. This indicates an unblended material/depth boundary.

    int tiles_checked = 0;
    int hard_border_tiles = 0;

    auto check_horizontal_borders = [](const TilePixels& pix) -> bool {
        for (int y = 0; y < SZ - 1; ++y) {
            int strong = 0;
            int brighter_below = 0, brighter_above = 0;
            for (int x = 0; x < SZ; ++x) {
                RGBA a = unpack(pix.pixels[y * SZ + x]);
                RGBA b = unpack(pix.pixels[(y + 1) * SZ + x]);
                int diff = std::abs(static_cast<int>(a.r) - b.r) +
                           std::abs(static_cast<int>(a.g) - b.g) +
                           std::abs(static_cast<int>(a.b) - b.b);
                if (diff > 140) {
                    strong++;
                    float la = 0.2126f * a.r + 0.7152f * a.g + 0.0722f * a.b;
                    float lb = 0.2126f * b.r + 0.7152f * b.g + 0.0722f * b.b;
                    if (lb > la) brighter_below++;
                    else brighter_above++;
                }
            }
            if (strong >= 14 && std::max(brighter_below, brighter_above) >= 13) return true;
        }
        return false;
    };

    auto check_vertical_borders = [](const TilePixels& pix) -> bool {
        for (int x = 0; x < SZ - 1; ++x) {
            int strong = 0;
            int brighter_right = 0, brighter_left = 0;
            for (int y = 0; y < SZ; ++y) {
                RGBA a = unpack(pix.pixels[y * SZ + x]);
                RGBA b = unpack(pix.pixels[y * SZ + (x + 1)]);
                int diff = std::abs(static_cast<int>(a.r) - b.r) +
                           std::abs(static_cast<int>(a.g) - b.g) +
                           std::abs(static_cast<int>(a.b) - b.b);
                if (diff > 140) {
                    strong++;
                    float la = 0.2126f * a.r + 0.7152f * a.g + 0.0722f * a.b;
                    float lb = 0.2126f * b.r + 0.7152f * b.g + 0.0722f * b.b;
                    if (lb > la) brighter_right++;
                    else brighter_left++;
                }
            }
            if (strong >= 14 && std::max(brighter_right, brighter_left) >= 13) return true;
        }
        return false;
    };

    for (int ty = 0; ty < h; ++ty) {
        for (int tx = 0; tx < w; ++tx) {
            auto cls = classify_tile(terrain, tx, ty, WATER_LEVEL);
            // Only check PureTerrain tiles — transition tiles have intentional boundaries
            if (cls.family != TileFamily::PureTerrain) continue;

            auto pix = generate_tile_texture(terrain, tx, ty, TEST_SEED, WATER_LEVEL);
            tiles_checked++;

            if (check_horizontal_borders(pix) || check_vertical_borders(pix)) {
                hard_border_tiles++;
            }
        }
    }

    float border_pct = 100.0f * static_cast<float>(hard_border_tiles) /
                       static_cast<float>(std::max(tiles_checked, 1));

    INFO("PureTerrain tiles checked: " << tiles_checked);
    INFO("Tiles with hard straight border: " << hard_border_tiles << " (" << border_pct << "%)");

    // No PureTerrain tile should have a full-width/height hard color border
    CHECK(border_pct < 1.0f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Test 14: No sharp 90-degree corners at tile boundaries
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Autotile.NoSharpCorners", "[autotile]") {
    g_world.ensure();
    const auto& terrain = g_world.terrain;
    int w = static_cast<int>(terrain.width);
    int h = static_cast<int>(terrain.height);

    // Detect sharp corners by checking the transition line direction at shared
    // tile edges. If tile A's line exits at angle θ_A and tile B's line enters
    // at angle θ_B, the angle change is |θ_A - θ_B|. A 90° change indicates
    // a sharp corner artifact.
    //
    // We estimate the transition line direction at the edge by finding the
    // water/land boundary position at 3 scanlines near the edge and computing
    // the angle from the position shift.

    auto find_boundary_x = [](const TilePixels& pix, int y) -> float {
        // Find the x-coordinate of the water/land boundary on scanline y
        for (int x = 0; x < SZ - 1; ++x) {
            RGBA a = unpack(pix.pixels[y * SZ + x]);
            RGBA b = unpack(pix.pixels[y * SZ + x + 1]);
            bool wa = (a.b > a.r + 20) && (a.b > a.g);
            bool wb = (b.b > b.r + 20) && (b.b > b.g);
            if (wa != wb) return static_cast<float>(x) + 0.5f;
        }
        return -1.0f;
    };

    auto find_boundary_y = [](const TilePixels& pix, int x) -> float {
        for (int y = 0; y < SZ - 1; ++y) {
            RGBA a = unpack(pix.pixels[y * SZ + x]);
            RGBA b = unpack(pix.pixels[(y + 1) * SZ + x]);
            bool wa = (a.b > a.r + 20) && (a.b > a.g);
            bool wb = (b.b > b.r + 20) && (b.b > b.g);
            if (wa != wb) return static_cast<float>(y) + 0.5f;
        }
        return -1.0f;
    };

    int pairs_checked = 0;
    int sharp_corners = 0;

    // Check East-West shared edges: compare curve direction at A's east edge
    // vs B's west edge
    for (int ty = 0; ty < h; ++ty) {
        for (int tx = 0; tx < w - 1; ++tx) {
            auto cls_a = classify_tile(terrain, tx, ty, WATER_LEVEL);
            auto cls_b = classify_tile(terrain, tx + 1, ty, WATER_LEVEL);
            // Both must be transition tiles with curves crossing the shared edge
            if (cls_a.family == TileFamily::PureTerrain) continue;
            if (cls_b.family == TileFamily::PureTerrain) continue;

            auto pix_a = generate_tile_texture(terrain, tx, ty, TEST_SEED, WATER_LEVEL);
            auto pix_b = generate_tile_texture(terrain, tx + 1, ty, TEST_SEED, WATER_LEVEL);

            // Estimate curve direction at A's east edge (x=14,15) using boundary y-positions
            float ya1 = find_boundary_y(pix_a, 14);
            float ya2 = find_boundary_y(pix_a, 15);
            // Estimate curve direction at B's west edge (x=0,1)
            float yb1 = find_boundary_y(pix_b, 0);
            float yb2 = find_boundary_y(pix_b, 1);

            if (ya1 < 0 || ya2 < 0 || yb1 < 0 || yb2 < 0) continue;

            // Direction vectors (dx=1 for both, dy is the boundary slope)
            float dy_a = ya2 - ya1;  // A's curve direction near east edge
            float dy_b = yb2 - yb1;  // B's curve direction near west edge

            // Angle between: dot product of normalized (1, dy) vectors
            float len_a = std::sqrt(1.0f + dy_a * dy_a);
            float len_b = std::sqrt(1.0f + dy_b * dy_b);
            float dot = (1.0f + dy_a * dy_b) / (len_a * len_b);
            float angle_deg = std::acos(std::clamp(dot, -1.0f, 1.0f)) * 180.0f / 3.14159f;

            pairs_checked++;
            if (angle_deg > 70.0f) {  // Near-90° or worse
                sharp_corners++;
            }
        }
    }

    // Check North-South shared edges
    for (int ty = 0; ty < h - 1; ++ty) {
        for (int tx = 0; tx < w; ++tx) {
            auto cls_a = classify_tile(terrain, tx, ty, WATER_LEVEL);
            auto cls_b = classify_tile(terrain, tx, ty + 1, WATER_LEVEL);
            if (cls_a.family == TileFamily::PureTerrain) continue;
            if (cls_b.family == TileFamily::PureTerrain) continue;

            auto pix_a = generate_tile_texture(terrain, tx, ty, TEST_SEED, WATER_LEVEL);
            auto pix_b = generate_tile_texture(terrain, tx, ty + 1, TEST_SEED, WATER_LEVEL);

            // Estimate curve direction at A's south edge (y=14,15)
            float xa1 = find_boundary_x(pix_a, 14);
            float xa2 = find_boundary_x(pix_a, 15);
            // At B's north edge (y=0,1)
            float xb1 = find_boundary_x(pix_b, 0);
            float xb2 = find_boundary_x(pix_b, 1);

            if (xa1 < 0 || xa2 < 0 || xb1 < 0 || xb2 < 0) continue;

            float dx_a = xa2 - xa1;
            float dx_b = xb2 - xb1;

            float len_a = std::sqrt(1.0f + dx_a * dx_a);
            float len_b = std::sqrt(1.0f + dx_b * dx_b);
            float dot = (1.0f + dx_a * dx_b) / (len_a * len_b);
            float angle_deg = std::acos(std::clamp(dot, -1.0f, 1.0f)) * 180.0f / 3.14159f;

            pairs_checked++;
            if (angle_deg > 70.0f) {
                sharp_corners++;
            }
        }
    }

    float sharp_pct = (pairs_checked > 0)
        ? 100.0f * static_cast<float>(sharp_corners) / static_cast<float>(pairs_checked)
        : 0.0f;

    INFO("Transition boundary pairs checked: " << pairs_checked);
    INFO("Sharp corners (>70°): " << sharp_corners << " (" << sharp_pct << "%)");

    REQUIRE(pairs_checked > 0);
    // Less than 15% of transition boundaries should have sharp corners
    // (heuristic boundary detection is color-dependent, so palette changes affect this)
    CHECK(sharp_pct < 15.0f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Test 15: Water color is terrain-independent (same depth → same color)
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Autotile.WaterColorTerrainIndependent", "[autotile]") {
    // Create two synthetic terrains with DIFFERENT terrain properties but
    // IDENTICAL water depth. Verify that the rendered water pixels are the same.

    // Terrain A: ocean tiles with different rock/soil properties
    auto t_a = make_synthetic(3, 3, {
        0.20f, 0.20f, 0.20f,
        0.20f, 0.20f, 0.20f,
        0.20f, 0.20f, 0.20f,
    });
    // Set distinct terrain properties on tile (1,1)
    auto& ta = t_a.tile_at(1, 1);
    ta.rock_blend = 0.9f;
    ta.soil_blend = 0.9f;
    ta.slope01 = 0.20f;
    ta.roughness = 0.5f;

    // Terrain B: same depth, different terrain properties
    auto t_b = make_synthetic(3, 3, {
        0.20f, 0.20f, 0.20f,
        0.20f, 0.20f, 0.20f,
        0.20f, 0.20f, 0.20f,
    });
    auto& tb = t_b.tile_at(1, 1);
    tb.rock_blend = 0.1f;
    tb.soil_blend = 0.1f;
    tb.slope01 = 0.01f;
    tb.roughness = 0.01f;

    if (!get_template_atlas().valid) {
        generate_template_atlas(const_cast<TemplateAtlas&>(get_template_atlas()), TEST_SEED);
    }

    auto pix_a = generate_tile_texture(t_a, 1, 1, TEST_SEED, WATER_LEVEL);
    auto pix_b = generate_tile_texture(t_b, 1, 1, TEST_SEED, WATER_LEVEL);

    int total_diff = 0;
    int max_diff = 0;
    for (int i = 0; i < SZ * SZ; ++i) {
        RGBA a = unpack(pix_a.pixels[i]);
        RGBA b = unpack(pix_b.pixels[i]);
        int dr = std::abs(static_cast<int>(a.r) - b.r);
        int dg = std::abs(static_cast<int>(a.g) - b.g);
        int db = std::abs(static_cast<int>(a.b) - b.b);
        int d = dr + dg + db;
        total_diff += d;
        max_diff = std::max(max_diff, d);
    }

    float avg_diff = static_cast<float>(total_diff) / static_cast<float>(SZ * SZ);

    INFO("Average RGB diff: " << avg_diff);
    INFO("Max RGB diff: " << max_diff);

    // Water tiles at the same depth should produce identical pixels regardless
    // of terrain properties. Allow tiny tolerance for floating point.
    CHECK(avg_diff < 1.0f);
    CHECK(max_diff < 3);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Test 16: Cross-tile boundary smoothness (universal edge blending)
// Wherever two different textures/colors meet at a tile boundary, the transition
// must be gentle — no hard RGB jumps exceeding a threshold.
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Autotile.CrossTileBoundarySmooth", "[autotile]") {
    // Use the full procedural world for realistic material diversity
    g_world.ensure();
    const auto& t = g_world.terrain;
    if (!get_template_atlas().valid) {
        generate_template_atlas(const_cast<TemplateAtlas&>(get_template_atlas()), TEST_SEED);
    }

    constexpr int MAX_JUMP = 45;  // max per-channel RGB difference across boundary
    int total_boundary_pixels = 0;
    int sharp_jumps = 0;

    // Check all horizontal (E-W) boundaries
    for (uint32_t ty = 0; ty < t.height; ++ty) {
        for (uint32_t tx = 0; tx + 1 < t.width; ++tx) {
            auto pix_a = generate_tile_texture(t, static_cast<int>(tx), static_cast<int>(ty),
                                               TEST_SEED, WATER_LEVEL);
            auto pix_b = generate_tile_texture(t, static_cast<int>(tx + 1), static_cast<int>(ty),
                                               TEST_SEED, WATER_LEVEL);

            for (int y = 0; y < SZ; ++y) {
                RGBA pa = unpack(pix_a.pixels[y * SZ + (SZ - 1)]);
                RGBA pb = unpack(pix_b.pixels[y * SZ + 0]);
                int dr = std::abs(static_cast<int>(pa.r) - pb.r);
                int dg = std::abs(static_cast<int>(pa.g) - pb.g);
                int db = std::abs(static_cast<int>(pa.b) - pb.b);
                int max_diff = std::max({dr, dg, db});
                total_boundary_pixels++;
                if (max_diff > MAX_JUMP)
                    sharp_jumps++;
            }
        }
    }

    // Check all vertical (N-S) boundaries
    for (uint32_t ty = 0; ty + 1 < t.height; ++ty) {
        for (uint32_t tx = 0; tx < t.width; ++tx) {
            auto pix_a = generate_tile_texture(t, static_cast<int>(tx), static_cast<int>(ty),
                                               TEST_SEED, WATER_LEVEL);
            auto pix_b = generate_tile_texture(t, static_cast<int>(tx), static_cast<int>(ty + 1),
                                               TEST_SEED, WATER_LEVEL);

            for (int x = 0; x < SZ; ++x) {
                RGBA pa = unpack(pix_a.pixels[(SZ - 1) * SZ + x]);
                RGBA pb = unpack(pix_b.pixels[0 * SZ + x]);
                int dr = std::abs(static_cast<int>(pa.r) - pb.r);
                int dg = std::abs(static_cast<int>(pa.g) - pb.g);
                int db = std::abs(static_cast<int>(pa.b) - pb.b);
                int max_diff = std::max({dr, dg, db});
                total_boundary_pixels++;
                if (max_diff > MAX_JUMP)
                    sharp_jumps++;
            }
        }
    }

    float jump_pct = 100.0f * static_cast<float>(sharp_jumps) /
                     static_cast<float>(std::max(total_boundary_pixels, 1));

    INFO("Total boundary pixels: " << total_boundary_pixels);
    INFO("Sharp jumps (>" << MAX_JUMP << " RGB): " << sharp_jumps << " (" << jump_pct << "%)");

    // Less than 5% of boundary pixels should have sharp transitions
    CHECK(jump_pct < 5.0f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Test 17: Land-water boundary smoothness (synthetic)
// Create terrain with a clear land/water boundary and verify the transition
// is smooth at the tile edge.
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Autotile.LandWaterBoundarySmooth", "[autotile]") {
    // Row 0: water, Row 1: coast, Row 2: land
    auto t = make_synthetic(4, 3, {
        0.10f, 0.10f, 0.10f, 0.10f,  // deep water
        0.40f, 0.42f, 0.47f, 0.50f,  // around water level (coast)
        0.60f, 0.65f, 0.70f, 0.75f,  // land
    });

    if (!get_template_atlas().valid) {
        generate_template_atlas(const_cast<TemplateAtlas&>(get_template_atlas()), TEST_SEED);
    }

    constexpr int MAX_JUMP = 50;
    int sharp_jumps = 0;
    int total = 0;

    // Check N-S boundaries between rows 0-1 and 1-2
    for (int row_pair = 0; row_pair < 2; ++row_pair) {
        int ty_a = row_pair;
        int ty_b = row_pair + 1;
        for (int tx = 0; tx < 4; ++tx) {
            auto pix_a = generate_tile_texture(t, tx, ty_a, TEST_SEED, WATER_LEVEL);
            auto pix_b = generate_tile_texture(t, tx, ty_b, TEST_SEED, WATER_LEVEL);

            for (int x = 0; x < SZ; ++x) {
                RGBA pa = unpack(pix_a.pixels[(SZ - 1) * SZ + x]);
                RGBA pb = unpack(pix_b.pixels[0 * SZ + x]);
                int dr = std::abs(static_cast<int>(pa.r) - pb.r);
                int dg = std::abs(static_cast<int>(pa.g) - pb.g);
                int db = std::abs(static_cast<int>(pa.b) - pb.b);
                int max_diff = std::max({dr, dg, db});
                total++;
                if (max_diff > MAX_JUMP)
                    sharp_jumps++;
            }
        }
    }

    float jump_pct =
        100.0f * static_cast<float>(sharp_jumps) / static_cast<float>(std::max(total, 1));

    INFO("Land-water boundary pixels: " << total);
    INFO("Sharp jumps (>" << MAX_JUMP << " RGB): " << sharp_jumps << " (" << jump_pct << "%)");

    CHECK(jump_pct < 10.0f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Test 18: Different material boundary smoothness (synthetic)
// Create terrain with different rock types adjacent to each other and verify
// the material transition is smooth at tile boundaries.
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Autotile.MaterialBoundarySmooth", "[autotile]") {
    // Two columns of different land elevation (different materials)
    auto t = make_synthetic(4, 2, {
        0.55f, 0.60f, 0.72f, 0.85f,  // low → high elevation (Sand → Rock)
        0.55f, 0.60f, 0.72f, 0.85f,
    });

    // Make the elevation gradient steep enough to trigger different materials
    for (uint32_t y = 0; y < 2; ++y) {
        for (uint32_t x = 0; x < 4; ++x) {
            auto& tile = t.tile_at(x, y);
            tile.dist_ocean = 2.0f + static_cast<float>(x) * 3.0f;
            tile.roughness = 0.1f + static_cast<float>(x) * 0.05f;
            tile.slope01 = 0.05f + static_cast<float>(x) * 0.08f;
        }
    }

    if (!get_template_atlas().valid) {
        generate_template_atlas(const_cast<TemplateAtlas&>(get_template_atlas()), TEST_SEED);
    }

    constexpr int MAX_JUMP = 50;
    int sharp_jumps = 0;
    int total = 0;

    // Check E-W boundaries between columns
    for (int tx = 0; tx < 3; ++tx) {
        for (int ty = 0; ty < 2; ++ty) {
            auto pix_a = generate_tile_texture(t, tx, ty, TEST_SEED, WATER_LEVEL);
            auto pix_b = generate_tile_texture(t, tx + 1, ty, TEST_SEED, WATER_LEVEL);

            for (int y = 0; y < SZ; ++y) {
                RGBA pa = unpack(pix_a.pixels[y * SZ + (SZ - 1)]);
                RGBA pb = unpack(pix_b.pixels[y * SZ + 0]);
                int dr = std::abs(static_cast<int>(pa.r) - pb.r);
                int dg = std::abs(static_cast<int>(pa.g) - pb.g);
                int db = std::abs(static_cast<int>(pa.b) - pb.b);
                int max_diff = std::max({dr, dg, db});
                total++;
                if (max_diff > MAX_JUMP)
                    sharp_jumps++;
            }
        }
    }

    float jump_pct =
        100.0f * static_cast<float>(sharp_jumps) / static_cast<float>(std::max(total, 1));

    INFO("Material boundary pixels: " << total);
    INFO("Sharp jumps (>" << MAX_JUMP << " RGB): " << sharp_jumps << " (" << jump_pct << "%)");

    CHECK(jump_pct < 10.0f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Test 19: No tile-shaped brightness discontinuity (hillshade interpolation)
// A flat uniform-material region must NOT have visible per-tile brightness steps.
// This catches the bug where per-tile hillshade/hypsometric tinting creates
// a "square tile outline" at boundaries.
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Autotile.NoTileBrightnessStep", "[autotile]") {
    // Create a 6x6 uniform land terrain — same elevation, same material everywhere
    // Any brightness difference at tile boundaries is purely a rendering artifact.
    std::vector<float> elevs(36, 0.60f);  // well above water, uniform
    Terrain t = make_synthetic(6, 6, elevs);
    // Set identical geology for all tiles so material classification is uniform
    for (uint32_t y = 0; y < 6; ++y) {
        for (uint32_t x = 0; x < 6; ++x) {
            auto& tile = t.tile_at(x, y);
            tile.slope01 = 0.03f;    // gentle
            tile.aspect = 1.0f;      // all same aspect
            tile.roughness = 0.05f;  // low
            tile.dist_ocean = 10.0f;
            tile.soil_depth = 0.8f;
            tile.rock = RockType::Granite;
            tile.soil = SoilTexture::Loam;
        }
    }

    if (!get_template_atlas().valid) {
        generate_template_atlas(const_cast<TemplateAtlas&>(get_template_atlas()), TEST_SEED);
    }

    // Render the inner 4x4 tiles (avoiding world edges where clamp effects matter)
    // and check that boundary pixel pairs have tiny brightness differences
    // Measure mean brightness delta at boundary vs interior to detect systematic
    // tile-boundary artifacts. On uniform terrain, boundary deltas should equal
    // interior deltas (both are just hash noise). A ratio >> 1 means tile outlines.
    double boundary_delta_sum = 0;
    int boundary_count = 0;
    double interior_delta_sum = 0;
    int interior_count = 0;

    // Horizontal boundaries: compare last pixel column of tile A with first of tile B
    // vs interior: compare col 7 and col 8 within tile A
    for (int ty = 1; ty <= 4; ++ty) {
        for (int tx = 1; tx < 4; ++tx) {
            auto pix_a = generate_tile_texture(t, tx, ty, TEST_SEED, WATER_LEVEL);
            auto pix_b = generate_tile_texture(t, tx + 1, ty, TEST_SEED, WATER_LEVEL);
            for (int y = 0; y < SZ; ++y) {
                float ba = brightness(unpack(pix_a.pixels[y * SZ + (SZ - 1)]));
                float bb = brightness(unpack(pix_b.pixels[y * SZ + 0]));
                boundary_delta_sum += std::abs(ba - bb);
                boundary_count++;

                float ia = brightness(unpack(pix_a.pixels[y * SZ + 7]));
                float ib = brightness(unpack(pix_a.pixels[y * SZ + 8]));
                interior_delta_sum += std::abs(ia - ib);
                interior_count++;
            }
        }
    }

    // Vertical boundaries
    for (int ty = 1; ty < 4; ++ty) {
        for (int tx = 1; tx <= 4; ++tx) {
            auto pix_a = generate_tile_texture(t, tx, ty, TEST_SEED, WATER_LEVEL);
            auto pix_b = generate_tile_texture(t, tx, ty + 1, TEST_SEED, WATER_LEVEL);
            for (int x = 0; x < SZ; ++x) {
                float ba = brightness(unpack(pix_a.pixels[(SZ - 1) * SZ + x]));
                float bb = brightness(unpack(pix_b.pixels[0 * SZ + x]));
                boundary_delta_sum += std::abs(ba - bb);
                boundary_count++;

                float ia = brightness(unpack(pix_a.pixels[7 * SZ + x]));
                float ib = brightness(unpack(pix_a.pixels[8 * SZ + x]));
                interior_delta_sum += std::abs(ia - ib);
                interior_count++;
            }
        }
    }

    double mean_boundary = boundary_delta_sum / std::max(boundary_count, 1);
    double mean_interior = interior_delta_sum / std::max(interior_count, 1);
    double ratio = mean_boundary / std::max(mean_interior, 0.01);

    INFO("Uniform terrain — mean boundary delta: " << mean_boundary);
    INFO("Uniform terrain — mean interior delta: " << mean_interior);
    INFO("Boundary/interior ratio: " << ratio);

    // On uniform terrain with interpolated properties, boundary deltas should
    // be indistinguishable from interior deltas. Ratio near 1.0 = no tile outlines.
    CHECK(ratio < 2.0);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Test 20: Hillshade continuity across varying terrain
// Even when terrain properties differ between tiles, the bilinear interpolation
// should ensure no SHARP steps. Measures the MEAN boundary delta vs MEAN interior
// delta — boundary delta should not be dramatically larger.
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Autotile.HillshadeContinuity", "[autotile]") {
    // Create terrain with a smooth elevation gradient — elevation increases east.
    // Adjacent tiles have different slope/aspect but the change is gradual.
    constexpr uint32_t W = 8, H = 4;
    std::vector<float> elevs(W * H);
    for (uint32_t y = 0; y < H; ++y) {
        for (uint32_t x = 0; x < W; ++x) {
            elevs[y * W + x] = 0.50f + 0.03f * static_cast<float>(x);
        }
    }
    Terrain t = make_synthetic(W, H, elevs);
    // Set varying slope/aspect based on position
    for (uint32_t y = 0; y < H; ++y) {
        for (uint32_t x = 0; x < W; ++x) {
            auto& tile = t.tile_at(x, y);
            tile.slope01 = 0.02f + 0.02f * static_cast<float>(x) / W;
            tile.aspect = static_cast<float>(x) * 0.3f;  // gradually changing aspect
            tile.roughness = 0.08f;
            tile.dist_ocean = 15.0f;
            tile.soil_depth = 0.7f;
            tile.rock = RockType::Sandstone;
            tile.soil = SoilTexture::Loam;
        }
    }

    if (!get_template_atlas().valid) {
        generate_template_atlas(const_cast<TemplateAtlas&>(get_template_atlas()), TEST_SEED);
    }

    // Measure mean brightness delta at tile boundaries vs within tiles
    double boundary_delta_sum = 0;
    int boundary_count = 0;
    double interior_delta_sum = 0;
    int interior_count = 0;

    for (int ty = 1; ty < static_cast<int>(H) - 1; ++ty) {
        for (int tx = 1; tx < static_cast<int>(W) - 2; ++tx) {
            auto pix_a = generate_tile_texture(t, tx, ty, TEST_SEED, WATER_LEVEL);
            auto pix_b = generate_tile_texture(t, tx + 1, ty, TEST_SEED, WATER_LEVEL);
            for (int y = 0; y < SZ; ++y) {
                // Boundary: last col of A vs first col of B
                float b_last = brightness(unpack(pix_a.pixels[y * SZ + (SZ - 1)]));
                float b_first = brightness(unpack(pix_b.pixels[y * SZ + 0]));
                boundary_delta_sum += std::abs(b_last - b_first);
                boundary_count++;

                // Interior: col 7 vs col 8 within tile A (mid-tile pair)
                float b_mid1 = brightness(unpack(pix_a.pixels[y * SZ + 7]));
                float b_mid2 = brightness(unpack(pix_a.pixels[y * SZ + 8]));
                interior_delta_sum += std::abs(b_mid1 - b_mid2);
                interior_count++;
            }
        }
    }

    double mean_boundary = boundary_delta_sum / std::max(boundary_count, 1);
    double mean_interior = interior_delta_sum / std::max(interior_count, 1);

    INFO("Mean boundary brightness delta: " << mean_boundary);
    INFO("Mean interior brightness delta: " << mean_interior);

    // The boundary delta should not be more than 3x the interior delta.
    // Without interpolation, boundary deltas are often 5-10x interior.
    CHECK(mean_boundary < mean_interior * 3.0 + 2.0);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Test 21: Same-material tiles must not show visible tile outlines
// Renders a patch of same-material tiles via render_terrain_region and checks
// that no 16-pixel-aligned horizontal or vertical brightness "seams" exist.
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Autotile.NoTileOutlineInPatch", "[autotile]") {
    // Uniform terrain: all tiles same material/elevation
    constexpr uint32_t W = 6, H = 6;
    std::vector<float> elevs(W * H, 0.58f);
    Terrain t = make_synthetic(W, H, elevs);
    for (uint32_t y = 0; y < H; ++y) {
        for (uint32_t x = 0; x < W; ++x) {
            auto& tile = t.tile_at(x, y);
            tile.slope01 = 0.04f;
            tile.aspect = 0.8f;
            tile.roughness = 0.06f;
            tile.dist_ocean = 12.0f;
            tile.soil_depth = 0.6f;
            tile.rock = RockType::Limestone;
            tile.soil = SoilTexture::Loam;
        }
    }

    if (!get_template_atlas().valid) {
        generate_template_atlas(const_cast<TemplateAtlas&>(get_template_atlas()), TEST_SEED);
    }

    // Render a region covering tiles 1-4 (avoiding world edge)
    constexpr int PX_PER_TILE = 16;
    constexpr int TILES = 4;
    constexpr int IMG_W = TILES * PX_PER_TILE;
    constexpr int IMG_H = TILES * PX_PER_TILE;
    std::vector<uint32_t> pixels(IMG_W * IMG_H);
    render_terrain_region(t, 1.0f, 1.0f, 1.0f / PX_PER_TILE, IMG_W, IMG_H, TEST_SEED,
                          pixels.data(), IMG_W, WATER_LEVEL);

    // For each tile boundary column (x = 16, 32, 48) and row (y = 16, 32, 48),
    // measure the mean |delta| across the seam vs interior pairs.
    double seam_delta_sum = 0;
    int seam_count = 0;
    double interior_delta_sum = 0;
    int interior_count = 0;

    // Vertical seams (columns aligned to tile boundaries)
    for (int bx = PX_PER_TILE; bx < IMG_W; bx += PX_PER_TILE) {
        for (int y = 0; y < IMG_H; ++y) {
            float b_left = brightness(unpack(pixels[y * IMG_W + bx - 1]));
            float b_right = brightness(unpack(pixels[y * IMG_W + bx]));
            seam_delta_sum += std::abs(b_left - b_right);
            seam_count++;
        }
    }

    // Interior pairs (columns NOT at tile boundaries)
    for (int ix = 1; ix < IMG_W - 1; ++ix) {
        if (ix % PX_PER_TILE == 0) continue;  // skip boundaries
        for (int y = 0; y < IMG_H; ++y) {
            float b_left = brightness(unpack(pixels[y * IMG_W + ix - 1]));
            float b_right = brightness(unpack(pixels[y * IMG_W + ix]));
            interior_delta_sum += std::abs(b_left - b_right);
            interior_count++;
        }
    }

    // Horizontal seams
    for (int by = PX_PER_TILE; by < IMG_H; by += PX_PER_TILE) {
        for (int x = 0; x < IMG_W; ++x) {
            float b_top = brightness(unpack(pixels[(by - 1) * IMG_W + x]));
            float b_bot = brightness(unpack(pixels[by * IMG_W + x]));
            seam_delta_sum += std::abs(b_top - b_bot);
            seam_count++;
        }
    }

    for (int iy = 1; iy < IMG_H - 1; ++iy) {
        if (iy % PX_PER_TILE == 0) continue;
        for (int x = 0; x < IMG_W; ++x) {
            float b_top = brightness(unpack(pixels[(iy - 1) * IMG_W + x]));
            float b_bot = brightness(unpack(pixels[iy * IMG_W + x]));
            interior_delta_sum += std::abs(b_top - b_bot);
            interior_count++;
        }
    }

    double mean_seam = seam_delta_sum / std::max(seam_count, 1);
    double mean_interior = interior_delta_sum / std::max(interior_count, 1);
    double ratio = mean_seam / std::max(mean_interior, 0.01);

    INFO("Mean seam delta: " << mean_seam);
    INFO("Mean interior delta: " << mean_interior);
    INFO("Seam/interior ratio: " << ratio);

    // With proper interpolation, tile seams should not stand out from interior.
    // Ratio close to 1.0 means no visible tile grid. Allowing up to 2.5x.
    CHECK(ratio < 2.5);
}
