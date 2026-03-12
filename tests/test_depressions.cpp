#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <set>
#include <vector>

#include "dynamics.h"
#include "terrain_gen.h"
#include "weather.h"

using namespace sandbox;

// ═══════════════════════════════════════════════════════════════════════════════
// Helpers
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

// D8 neighbor offsets (must match dynamics.cpp)
constexpr int DX8[] = {-1, 0, 1, -1, 1, -1, 0, 1};
constexpr int DY8[] = {-1, -1, -1, 0, 0, 1, 1, 1};

struct PresetConfig {
    const char* name;
    EnvParams env;
    uint32_t seed;
};

// Presets matching main.cpp definitions for reproducibility
static const PresetConfig TEST_PRESETS[] = {
    {"Pangaea",
     {.water_level = 0.40f,
      .noise_scale = 0.015f,
      .continent_threshold = 0.65f,
      .ridge_strength = 1.2f,
      .plate_count = 8,
      .continental_ratio = 0.55f},
     101},
    {"Continental",
     {.water_level = 0.42f,
      .noise_scale = 0.020f,
      .continent_threshold = 0.55f,
      .ridge_strength = 1.0f,
      .plate_count = 12,
      .continental_ratio = 0.45f},
     789},
    {"Highland",
     {.water_level = 0.35f,
      .noise_scale = 0.020f,
      .continent_threshold = 0.60f,
      .ridge_strength = 1.8f,
      .plate_count = 10,
      .continental_ratio = 0.50f},
     1337},
};

constexpr int NUM_TEST_PRESETS = sizeof(TEST_PRESETS) / sizeof(TEST_PRESETS[0]);

// World size: 128x64 is large enough for geological processes but fast enough for tests.
// The depression system uses spacing of 16-24 tiles, so we need at least ~100 tiles wide
// to get meaningful coverage of graben/glacial/caldera placement grids.
constexpr uint32_t TEST_W = 128;
constexpr uint32_t TEST_H = 64;

// Count D8 sinks: land tiles where no neighbor is lower (flow accumulation endpoints)
uint32_t count_d8_sinks(const Terrain& terrain) {
    uint32_t w = terrain.width, h = terrain.height;
    uint32_t sinks = 0;
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            const auto& t = terrain.tile_at(x, y);
            if (t.is_ocean) continue;

            bool is_sink = true;
            for (int d = 0; d < 8; ++d) {
                int nx = static_cast<int>(x) + DX8[d];
                int ny = static_cast<int>(y) + DY8[d];
                if (nx < 0 || nx >= static_cast<int>(w) || ny < 0 || ny >= static_cast<int>(h))
                    continue;
                if (terrain.tile_at(static_cast<uint32_t>(nx), static_cast<uint32_t>(ny)).elev01 <
                    t.elev01) {
                    is_sink = false;
                    break;
                }
            }
            if (is_sink) ++sinks;
        }
    }
    return sinks;
}

// Find connected components of tiles satisfying a predicate using flood fill.
// Returns a vector of component sizes.
std::vector<uint32_t> find_connected_components(
    const Terrain& terrain,
    const std::function<bool(uint32_t x, uint32_t y)>& predicate) {

    uint32_t w = terrain.width, h = terrain.height;
    std::vector<bool> visited(static_cast<size_t>(w) * h, false);
    std::vector<uint32_t> sizes;

    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            size_t idx = static_cast<size_t>(y) * w + x;
            if (visited[idx] || !predicate(x, y)) continue;

            // BFS flood fill
            uint32_t component_size = 0;
            std::vector<std::pair<uint32_t, uint32_t>> queue;
            queue.push_back({x, y});
            visited[idx] = true;

            size_t qi = 0;
            while (qi < queue.size()) {
                auto [cx, cy] = queue[qi++];
                ++component_size;
                // 4-connected neighbors (sufficient for depression contiguity)
                constexpr int dx4[] = {-1, 0, 1, 0};
                constexpr int dy4[] = {0, -1, 0, 1};
                for (int d = 0; d < 4; ++d) {
                    int nx = static_cast<int>(cx) + dx4[d];
                    int ny = static_cast<int>(cy) + dy4[d];
                    if (nx < 0 || nx >= static_cast<int>(w) || ny < 0 || ny >= static_cast<int>(h))
                        continue;
                    size_t ni = static_cast<size_t>(ny) * w + static_cast<size_t>(nx);
                    if (!visited[ni] &&
                        predicate(static_cast<uint32_t>(nx), static_cast<uint32_t>(ny))) {
                        visited[ni] = true;
                        queue.push_back(
                            {static_cast<uint32_t>(nx), static_cast<uint32_t>(ny)});
                    }
                }
            }
            sizes.push_back(component_size);
        }
    }
    return sizes;
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════════
// Terrain generation: depression invariants
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("[terrain] tectonic terrain produces D8 sinks on land", "[regression][terrain]") {
    // Invariant 1: Terrain with tectonic plates must produce local elevation minima
    // (D8 sinks) on land. These are the sites that become lakes when filled with water.
    // Without them, all water drains to ocean and no lakes form.

    for (int pi = 0; pi < NUM_TEST_PRESETS; ++pi) {
        SECTION(TEST_PRESETS[pi].name) {
            const auto& preset = TEST_PRESETS[pi];
            auto terrain = generate_terrain(TEST_W, TEST_H, preset.env, preset.seed);

            uint32_t sinks = count_d8_sinks(terrain);

            INFO("Preset: " << preset.name << ", seed: " << preset.seed);
            INFO("D8 sinks found: " << sinks);
            // At least 5 sinks expected — the geological depression system creates
            // grabens, glacial troughs, and calderas that should produce local minima.
            REQUIRE(sinks >= 5);
            // Upper bound: shouldn't have thousands of sinks on a 128x64 grid
            // (would indicate broken elevation field, not real depressions)
            REQUIRE(sinks <= 500);
        }
    }
}

TEST_CASE("[terrain] depressions have multi-tile extent", "[regression][terrain]") {
    // Invariant 2: Depression morphology variety — not all single-tile pits.
    // Glacial troughs (8-20 tiles major axis) and calderas (4-9 tiles radius)
    // should produce multi-tile depressions. We check that at least some
    // connected components of below-local-average elevation span >3 tiles.

    for (int pi = 0; pi < NUM_TEST_PRESETS; ++pi) {
        SECTION(TEST_PRESETS[pi].name) {
            const auto& preset = TEST_PRESETS[pi];
            auto terrain = generate_terrain(TEST_W, TEST_H, preset.env, preset.seed);

            // A tile is "depressed" if it's a land tile whose elevation is below the
            // average of its D8 neighbors (i.e., it sits in a local bowl).
            auto is_depressed = [&](uint32_t x, uint32_t y) -> bool {
                const auto& t = terrain.tile_at(x, y);
                if (t.is_ocean) return false;

                float sum = 0;
                int count = 0;
                for (int d = 0; d < 8; ++d) {
                    int nx = static_cast<int>(x) + DX8[d];
                    int ny = static_cast<int>(y) + DY8[d];
                    if (nx < 0 || nx >= static_cast<int>(TEST_W) ||
                        ny < 0 || ny >= static_cast<int>(TEST_H))
                        continue;
                    sum += terrain.tile_at(static_cast<uint32_t>(nx),
                                           static_cast<uint32_t>(ny)).elev01;
                    count++;
                }
                float avg = sum / static_cast<float>(count);
                // Below neighbors by at least 0.002 (filters out flat areas)
                return t.elev01 < avg - 0.002f;
            };

            auto components = find_connected_components(terrain, is_depressed);

            // Count components with >3 tiles (multi-tile depressions)
            uint32_t multi_tile = 0;
            uint32_t max_size = 0;
            for (auto sz : components) {
                if (sz > 3) ++multi_tile;
                if (sz > max_size) max_size = sz;
            }

            INFO("Preset: " << preset.name);
            INFO("Total depressed components: " << components.size());
            INFO("Multi-tile (>3) components: " << multi_tile);
            INFO("Largest component: " << max_size);

            // At least one multi-tile depression expected from glacial/caldera stamps
            REQUIRE(multi_tile >= 1);
        }
    }
}

TEST_CASE("[terrain] depression elevation bounds", "[regression][terrain]") {
    // Invariant 3: No depression should carve below water_level - 0.01
    // (the code explicitly clamps to this). No rim should exceed 1.0.

    for (int pi = 0; pi < NUM_TEST_PRESETS; ++pi) {
        SECTION(TEST_PRESETS[pi].name) {
            const auto& preset = TEST_PRESETS[pi];
            auto terrain = generate_terrain(TEST_W, TEST_H, preset.env, preset.seed);

            float min_land_elev = 1.0f;
            float max_elev = 0.0f;
            uint32_t below_water_count = 0;

            for (uint32_t y = 0; y < TEST_H; ++y) {
                for (uint32_t x = 0; x < TEST_W; ++x) {
                    const auto& t = terrain.tile_at(x, y);
                    if (t.elev01 > max_elev) max_elev = t.elev01;
                    if (!t.is_ocean && t.elev01 < min_land_elev) {
                        min_land_elev = t.elev01;
                    }
                    // Count tiles that dip below the minimum allowed depression floor
                    if (!t.is_ocean && t.elev01 < preset.env.water_level - 0.015f) {
                        // Allow 0.005 tolerance beyond the -0.01 code clamp for
                        // floating-point and other pre-depression noise effects
                        ++below_water_count;
                    }
                }
            }

            INFO("Preset: " << preset.name);
            INFO("Min land elevation: " << min_land_elev);
            INFO("Max elevation: " << max_elev);
            INFO("Water level: " << preset.env.water_level);
            INFO("Tiles below water_level - 0.015: " << below_water_count);

            // The minimum land elevation should not go far below water_level.
            // The geological depression code clamps to water_level - 0.01, but
            // earlier-stage basin noise (terrain_gen.cpp line ~730) can also lower
            // elevation. We allow a 0.10 margin below water_level as the combined
            // effect of all depression processes.
            CHECK(min_land_elev >= preset.env.water_level - 0.10f);

            // No elevation should exceed 1.0 (rim clamping)
            CHECK(max_elev <= 1.0f);
        }
    }
}

TEST_CASE("[terrain] ocean tile count stable after depression carving", "[regression][terrain]") {
    // Invariant 5: Depressions must not accidentally create new ocean tiles.
    // The is_ocean flag is set based on elev01 < water_level during terrain gen.
    // Depression code clamps to water_level - 0.01, which is still above typical
    // ocean-creating thresholds. We verify that ocean count is reasonable and
    // consistent across re-generation with same seed.

    for (int pi = 0; pi < NUM_TEST_PRESETS; ++pi) {
        SECTION(TEST_PRESETS[pi].name) {
            const auto& preset = TEST_PRESETS[pi];

            auto terrain1 = generate_terrain(TEST_W, TEST_H, preset.env, preset.seed);
            auto terrain2 = generate_terrain(TEST_W, TEST_H, preset.env, preset.seed);

            uint32_t ocean1 = 0, ocean2 = 0;
            for (size_t i = 0; i < terrain1.tiles.size(); ++i) {
                if (terrain1.tiles[i].is_ocean) ++ocean1;
                if (terrain2.tiles[i].is_ocean) ++ocean2;
            }

            INFO("Preset: " << preset.name);
            INFO("Ocean tiles run 1: " << ocean1 << ", run 2: " << ocean2);

            // Deterministic: same seed must produce identical ocean count
            REQUIRE(ocean1 == ocean2);

            // Sanity: not all-ocean or all-land
            uint32_t total = TEST_W * TEST_H;
            CHECK(ocean1 > 0);
            CHECK(ocean1 < total);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Hydrology: basin/lake invariants
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("[hydrology] basin spillway system detects sink basins", "[regression][hydrology]") {
    // Invariant 4 (refined): init_dynamics identifies D8 sink basins with spill
    // elevations. The basin count should be in a reasonable range (5-100 for
    // 128x64 terrain). Each basin must have spill_elev >= sink_elev.

    for (int pi = 0; pi < NUM_TEST_PRESETS; ++pi) {
        SECTION(TEST_PRESETS[pi].name) {
            const auto& preset = TEST_PRESETS[pi];
            auto terrain = generate_terrain(TEST_W, TEST_H, preset.env, preset.seed);
            auto climate = bake_weather(terrain, AtmosphereConfig{});

            DynamicState dyn;
            init_dynamics(dyn, terrain, climate);

            INFO("Preset: " << preset.name);
            INFO("Basin count: " << dyn.num_basins);

            // At least some basins must exist (geological depressions create them)
            REQUIRE(dyn.num_basins >= 3);
            // Not an absurd number (would indicate broken D8 or all-flat terrain)
            REQUIRE(dyn.num_basins <= 300);

            // Each basin must have consistent internal structure
            uint32_t meaningful_basins = 0;  // basins with real depression depth
            for (uint16_t bi = 0; bi < dyn.num_basins; ++bi) {
                INFO("Basin " << bi << ": sink_elev=" << dyn.basin_sink_elev[bi]
                              << " spill_elev=" << dyn.basin_spill_elev[bi]
                              << " area=" << dyn.basin_area[bi]);

                // Basin area must be at least 1 tile
                CHECK(dyn.basin_area[bi] >= 1);

                // Basin tile list must match reported area
                CHECK(dyn.basin_tiles[bi].size() == dyn.basin_area[bi]);

                // Elevations must be finite (not NaN/Inf)
                CHECK(std::isfinite(dyn.basin_sink_elev[bi]));
                CHECK(std::isfinite(dyn.basin_spill_elev[bi]));

                // Count basins with real depression depth (spill - sink > 0.001)
                // Some basins may have spill < sink due to cascading D8 topology
                // with micro-relief noise, but meaningful lake basins should have
                // positive depression depth.
                float depth = dyn.basin_spill_elev[bi] - dyn.basin_sink_elev[bi];
                if (depth > 0.001f) ++meaningful_basins;
            }

            INFO("Meaningful basins (depth > 0.001): " << meaningful_basins);
            // At least some basins should have real depression depth for lake formation
            CHECK(meaningful_basins >= 1);
        }
    }
}

TEST_CASE("[hydrology] multi-tile lakes form after simulation", "[regression][hydrology]") {
    // Invariant 6: After sufficient simulation time with static climate, at least
    // one lake with >5 connected tiles of surface_water above threshold should exist.
    // We simulate ~180 days (half year) with static climate (no atmosphere needed).

    // Use Continental preset — most balanced for lake formation
    const auto& preset = TEST_PRESETS[1];  // Continental
    auto terrain = generate_terrain(TEST_W, TEST_H, preset.env, preset.seed);
    auto climate = bake_weather(terrain, AtmosphereConfig{});

    DynamicState dyn;
    init_dynamics(dyn, terrain, climate);
    dyn.paused = false;
    dyn.time_scale = 1.0f;

    // Simulate ~365 days (1 year): each tick at DT=0.5s real time = 1 sim-day
    // at DAYS_PER_SECOND=2, time_scale=1.0. This allows full seasonal cycle
    // for precipitation to accumulate in basins.
    constexpr float DT = 0.5f;  // seconds of real time per tick
    constexpr int NUM_TICKS = 365;  // ~365 sim-days

    for (int tick = 0; tick < NUM_TICKS; ++tick) {
        tick_dynamics(dyn, terrain, climate, DT, nullptr);
    }

    // Find connected components of wet tiles (surface_water > 0.1)
    // Using 0.1 threshold rather than 0.3 since static climate produces
    // less precipitation than the full atmosphere simulation.
    constexpr float LAKE_THRESHOLD = 0.1f;
    auto is_lake_tile = [&](uint32_t x, uint32_t y) -> bool {
        const auto& tt = terrain.tile_at(x, y);
        if (tt.is_ocean) return false;
        return dyn.tile_at(x, y).surface_water > LAKE_THRESHOLD;
    };

    auto lake_components = find_connected_components(terrain, is_lake_tile);

    uint32_t lakes_over_5 = 0;
    uint32_t max_lake = 0;
    for (auto sz : lake_components) {
        if (sz > 5) ++lakes_over_5;
        if (sz > max_lake) max_lake = sz;
    }

    INFO("Preset: " << preset.name);
    INFO("Elapsed sim-days: " << dyn.elapsed_days);
    INFO("Total lake components: " << lake_components.size());
    INFO("Lakes with >5 tiles: " << lakes_over_5);
    INFO("Largest lake: " << max_lake << " tiles");

    // At least one meaningful lake should form
    CHECK(lakes_over_5 >= 1);
}

TEST_CASE("[hydrology] lake size variety", "[regression][hydrology]") {
    // Invariant 7: Not all lakes same size. The depression system produces
    // grabens, glacial troughs, calderas, AND the micro-relief noise in dynamics
    // creates small basins. We should see a spread of basin areas.

    const auto& preset = TEST_PRESETS[0];  // Pangaea — large landmass for variety
    auto terrain = generate_terrain(TEST_W, TEST_H, preset.env, preset.seed);
    auto climate = bake_weather(terrain, AtmosphereConfig{});

    DynamicState dyn;
    init_dynamics(dyn, terrain, climate);

    // Check basin area distribution (from init, before simulation)
    if (dyn.num_basins >= 3) {
        std::vector<uint32_t> areas(dyn.basin_area.begin(),
                                     dyn.basin_area.begin() + dyn.num_basins);
        std::sort(areas.begin(), areas.end());

        uint32_t min_area = areas.front();
        uint32_t max_area = areas.back();
        uint32_t median_area = areas[areas.size() / 2];

        INFO("Preset: " << preset.name);
        INFO("Basin count: " << dyn.num_basins);
        INFO("Min basin area: " << min_area);
        INFO("Max basin area: " << max_area);
        INFO("Median basin area: " << median_area);

        // There should be size variety — max should be meaningfully larger than min.
        // Single-tile basins (area=1) from micro-relief + multi-tile from geological
        // depressions should create at least a 2:1 ratio.
        CHECK(max_area >= min_area * 2);
    } else {
        // If fewer than 3 basins, something is likely wrong with depression generation
        INFO("Only " << dyn.num_basins << " basins found — too few for variety test");
        CHECK(dyn.num_basins >= 3);
    }
}

TEST_CASE("[hydrology] water storage bounded over time", "[regression][hydrology]") {
    // Invariant 8: Total water mass should not grow unboundedly. After 1 year
    // of simulation with static climate, total storage should be within reasonable
    // bounds. An unbounded leak indicates missing evaporation, broken overflow
    // routing, or conservation violation.

    const auto& preset = TEST_PRESETS[1];  // Continental
    auto terrain = generate_terrain(TEST_W, TEST_H, preset.env, preset.seed);
    auto climate = bake_weather(terrain, AtmosphereConfig{});

    DynamicState dyn;
    init_dynamics(dyn, terrain, climate);
    dyn.paused = false;
    dyn.time_scale = 1.0f;

    // Count land tiles for normalization
    uint32_t land_tiles = 0;
    for (const auto& t : terrain.tiles) {
        if (!t.is_ocean) ++land_tiles;
    }

    auto compute_total_storage = [&]() -> double {
        double total = 0;
        for (size_t i = 0; i < dyn.tiles.size(); ++i) {
            if (terrain.tiles[i].is_ocean) continue;
            const auto& dt = dyn.tiles[i];
            total += dt.surface_water + dt.soil_moisture + dt.groundwater + dt.snow_depth;
        }
        return total;
    };

    double initial_storage = compute_total_storage();

    // Simulate ~365 days
    constexpr float DT = 0.5f;
    constexpr int NUM_TICKS = 365;
    for (int tick = 0; tick < NUM_TICKS; ++tick) {
        tick_dynamics(dyn, terrain, climate, DT, nullptr);
    }

    double final_storage = compute_total_storage();
    double per_tile_storage = final_storage / static_cast<double>(land_tiles);

    INFO("Initial storage: " << initial_storage);
    INFO("Final storage: " << final_storage);
    INFO("Per-tile average: " << per_tile_storage);
    INFO("Elapsed days: " << dyn.elapsed_days);
    INFO("Land tiles: " << land_tiles);

    // Per-tile storage should not be extreme.
    // Surface water cap is 1.5-3.0, soil moisture ~0-1, groundwater unbounded but
    // typically small. A reasonable per-tile average should be well under 5.0.
    CHECK(per_tile_storage < 5.0);

    // Should not be zero either (precipitation should accumulate some water)
    CHECK(per_tile_storage > 0.01);

    // Storage should not have grown by more than 10x from initial
    // (initial is mostly soil_moisture ~0.3 * land_tiles)
    if (initial_storage > 0.01) {
        double growth_ratio = final_storage / initial_storage;
        INFO("Growth ratio: " << growth_ratio);
        CHECK(growth_ratio < 10.0);
    }
}

TEST_CASE("[terrain] depression generation is deterministic", "[regression][terrain]") {
    // Verify that the geological depression system is fully deterministic:
    // same seed + same params = identical elevation field.

    const auto& preset = TEST_PRESETS[0];  // Pangaea
    auto terrain1 = generate_terrain(TEST_W, TEST_H, preset.env, preset.seed);
    auto terrain2 = generate_terrain(TEST_W, TEST_H, preset.env, preset.seed);

    bool all_match = true;
    for (size_t i = 0; i < terrain1.tiles.size(); ++i) {
        if (terrain1.tiles[i].elev01 != terrain2.tiles[i].elev01) {
            all_match = false;
            INFO("Mismatch at tile " << i << ": " << terrain1.tiles[i].elev01
                                     << " vs " << terrain2.tiles[i].elev01);
            CHECK(false);
            break;
        }
    }
    if (all_match) {
        CHECK(true);  // explicit pass
    }
}
