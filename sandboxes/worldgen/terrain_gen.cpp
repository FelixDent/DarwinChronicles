#include "terrain_gen.h"

#include <FastNoiseLite.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <numeric>
#include <tuple>
#include <vector>

namespace sandbox {

// ── ElevBand names ──────────────────────────────────────────────────────────

const char* elevband_name(ElevBand b) {
    switch (b) {
        case ElevBand::Water:
            return "Water";
        case ElevBand::Lowland:
            return "Lowland";
        case ElevBand::Hills:
            return "Hills";
        case ElevBand::Mountains:
            return "Mountains";
        default:
            return "Unknown";
    }
}

// ── Noise field helpers ─────────────────────────────────────────────────────

using Clock = std::chrono::high_resolution_clock;

static double elapsed_ms(Clock::time_point start) {
    auto end = Clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

// ── Domain warping helper ───────────────────────────────────────────────────
// Warp input coordinates using low-frequency noise for more natural landforms.

struct WarpedCoord {
    float x, y;
};

static WarpedCoord domain_warp(float x, float y, FastNoiseLite& warp_x_noise,
                               FastNoiseLite& warp_y_noise, float strength) {
    float wx = warp_x_noise.GetNoise(x * 0.008f, y * 0.008f) * strength;
    float wy = warp_y_noise.GetNoise(x * 0.008f, y * 0.008f) * strength;
    return {x + wx, y + wy};
}

// Height: terrain detail noise + low-frequency continental noise with domain warping.
// The continental noise creates irregular shapes — multiple landmasses, peninsulas,
// channels, and bays — instead of a single radial blob. A soft edge falloff ensures
// map borders are ocean.
static void generate_height_field(std::vector<float>& out, uint32_t w, uint32_t h, uint32_t seed,
                                  float scale) {
    // Terrain detail: 4-octave Perlin for local hills, valleys, mountains
    FastNoiseLite terrain;
    terrain.SetSeed(static_cast<int>(seed));
    terrain.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    terrain.SetFrequency(1.0f);

    // Continental shape: higher frequency for multiple distinct landmasses.
    FastNoiseLite continent;
    continent.SetSeed(static_cast<int>(seed + 5000));
    continent.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    continent.SetFrequency(1.0f);
    continent.SetFractalType(FastNoiseLite::FractalType_FBm);
    continent.SetFractalOctaves(2);
    continent.SetFractalLacunarity(2.0f);
    continent.SetFractalGain(0.5f);

    // Coastline detail: mid-frequency noise for peninsulas, bays, fjords
    FastNoiseLite coast;
    coast.SetSeed(static_cast<int>(seed + 6000));
    coast.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    coast.SetFrequency(1.0f);
    coast.SetFractalType(FastNoiseLite::FractalType_FBm);
    coast.SetFractalOctaves(3);
    coast.SetFractalLacunarity(2.0f);
    coast.SetFractalGain(0.5f);

    // Lake noise: creates inland depressions where noise peaks align
    FastNoiseLite lake_noise;
    lake_noise.SetSeed(static_cast<int>(seed + 7000));
    lake_noise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    lake_noise.SetFrequency(1.0f);

    // Domain warp noise layers
    FastNoiseLite warp_x_noise;
    warp_x_noise.SetSeed(static_cast<int>(seed + 10000));
    warp_x_noise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    warp_x_noise.SetFrequency(1.0f);

    FastNoiseLite warp_y_noise;
    warp_y_noise.SetSeed(static_cast<int>(seed + 11000));
    warp_y_noise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    warp_y_noise.SetFrequency(1.0f);

    constexpr float WARP_STRENGTH = 20.0f;  // tiles of displacement
    constexpr uint32_t OCTAVES = 4;
    constexpr float PERSISTENCE = 0.5f;

    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            float fx = static_cast<float>(x);
            float fy = static_cast<float>(y);

            // Apply domain warping to get organic landform shapes
            auto warped = domain_warp(fx, fy, warp_x_noise, warp_y_noise, WARP_STRENGTH);

            // Terrain detail noise [0, 1]
            float total = 0.0f;
            float amplitude = 1.0f;
            float frequency = 1.0f;
            float max_value = 0.0f;

            for (uint32_t i = 0; i < OCTAVES; ++i) {
                float nx = warped.x * scale * frequency;
                float ny = warped.y * scale * frequency;
                total += terrain.GetNoise(nx, ny) * amplitude;
                max_value += amplitude;
                amplitude *= PERSISTENCE;
                frequency *= 2.0f;
            }
            float terrain_h = (total / max_value + 1.0f) * 0.5f;

            // Continental noise at higher frequency → 3-4 separate landmasses
            float cont = continent.GetNoise(warped.x * 0.015f, warped.y * 0.015f);

            // Coastline detail: mid-scale for irregular shores
            float coast_val = coast.GetNoise(warped.x * 0.025f, warped.y * 0.025f);

            // Continental mask: blend of large-scale shape + coastline detail
            float land_signal = cont * 0.6f + coast_val * 0.4f;

            // Edge falloff: moderate push toward ocean at borders (uses original coords)
            float ex = (fx / static_cast<float>(w)) * 2.0f - 1.0f;
            float ey = (fy / static_cast<float>(h)) * 2.0f - 1.0f;
            float edge_dist = std::max(std::abs(ex), std::abs(ey));
            float edge_falloff = std::clamp((edge_dist - 0.75f) / 0.25f, 0.0f, 1.0f);
            land_signal -= edge_falloff * 0.5f;

            // Mask for continent shape
            float mask = std::clamp(land_signal * 2.0f + 0.55f, 0.0f, 1.0f);
            mask = mask * mask * (3.0f - 2.0f * mask);  // Smoothstep

            // Base height from continental mask
            float base_h = 0.28f + mask * 0.34f;

            // Terrain noise: stronger on land
            float terrain_weight = 0.10f + mask * 0.40f;
            float terrain_offset = (terrain_h - 0.5f) * terrain_weight;

            // Lake noise: carve basins on land
            float lake_val = lake_noise.GetNoise(warped.x * 0.018f, warped.y * 0.018f);
            float lake_carve = 0.0f;
            if (lake_val > 0.2f && mask > 0.6f) {
                lake_carve = (lake_val - 0.2f) * 0.6f * mask;
            }

            float final_height = base_h + terrain_offset - lake_carve;
            out[static_cast<size_t>(y) * w + x] = std::clamp(final_height, 0.0f, 1.0f);
        }
    }
}

// Ridge field: ridged multifractal noise for mountain ranges.
// Produces the ridge-only component used for: (1) merging into unified height,
// (2) renderer decoration.
static void generate_ridge_field(std::vector<float>& out, const std::vector<float>& heights,
                                 uint32_t w, uint32_t h, uint32_t seed, float water_level,
                                 float ridge_strength) {
    FastNoiseLite noise;
    noise.SetSeed(static_cast<int>(seed + 8000));
    noise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    noise.SetFrequency(1.0f);

    FastNoiseLite detail;
    detail.SetSeed(static_cast<int>(seed + 8500));
    detail.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    detail.SetFrequency(1.0f);

    FastNoiseLite region;
    region.SetSeed(static_cast<int>(seed + 9000));
    region.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    region.SetFrequency(1.0f);

    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            size_t idx = static_cast<size_t>(y) * w + x;

            if (heights[idx] < water_level) {
                out[idx] = 0.0f;
                continue;
            }

            float nx = static_cast<float>(x) * 0.02f;
            float ny = static_cast<float>(y) * 0.02f;
            float n1 = noise.GetNoise(nx, ny);
            float ridge1 = 1.0f - std::abs(n1);

            float n2 = detail.GetNoise(nx * 2.0f, ny * 2.0f);
            float ridge2 = 1.0f - std::abs(n2);

            float ridge_val = ridge1 * 0.7f + ridge2 * 0.3f;
            ridge_val = ridge_val * ridge_val * ridge_val * ridge_val;

            float region_val =
                region.GetNoise(static_cast<float>(x) * 0.01f, static_cast<float>(y) * 0.01f);
            float region_mask = std::clamp((region_val - 0.1f) * 3.0f, 0.0f, 1.0f);
            region_mask = region_mask * region_mask;

            float land_depth = (heights[idx] - water_level) / (1.0f - water_level);
            float coast_suppress = std::clamp(land_depth * 3.0f, 0.0f, 1.0f);

            float elev = ridge_val * region_mask * coast_suppress * ridge_strength;
            out[idx] = std::clamp(elev * 3.0f, 0.0f, 1.0f);
        }
    }
}

// Merge ridge field into the height field to create unified height.
// Ridge scale factor ~0.18 matches the old `elevation * 0.25` band offset.
// Stores unified height back into height_field, keeps ridge_field for decoration.
static void unify_height_field(std::vector<float>& height_field,
                               const std::vector<float>& ridge_field, size_t size,
                               float water_level) {
    constexpr float RIDGE_SCALE = 0.18f;
    for (size_t i = 0; i < size; ++i) {
        if (height_field[i] >= water_level) {
            height_field[i] =
                std::clamp(height_field[i] + ridge_field[i] * RIDGE_SCALE, 0.0f, 1.0f);
        }
    }
}

// ── Ocean/Lake flood-fill ───────────────────────────────────────────────────

static void compute_ocean_lake(const std::vector<float>& heights, uint32_t w, uint32_t h,
                               float water_level, std::vector<bool>& is_ocean,
                               std::vector<bool>& is_lake) {
    size_t size = static_cast<size_t>(w) * h;
    is_ocean.assign(size, false);
    is_lake.assign(size, false);

    std::vector<bool> is_water(size, false);
    for (size_t i = 0; i < size; ++i) {
        is_water[i] = heights[i] < water_level;
    }

    constexpr uint32_t OCEAN_MIN_TILES = 200;
    std::vector<int> component_id(size, -1);
    std::vector<std::vector<size_t>> components;
    std::vector<bool> touches_edge;

    constexpr int DX[] = {-1, 1, 0, 0};
    constexpr int DY[] = {0, 0, -1, 1};

    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            size_t idx = static_cast<size_t>(y) * w + x;
            if (component_id[idx] >= 0)
                continue;
            if (!is_water[idx])
                continue;

            int cid = static_cast<int>(components.size());
            components.emplace_back();
            touches_edge.push_back(false);

            std::deque<size_t> queue;
            component_id[idx] = cid;
            queue.push_back(idx);

            while (!queue.empty()) {
                size_t cur = queue.front();
                queue.pop_front();
                components[static_cast<size_t>(cid)].push_back(cur);

                auto cx = static_cast<uint32_t>(cur % w);
                auto cy = static_cast<uint32_t>(cur / w);
                if (cx == 0 || cx == w - 1 || cy == 0 || cy == h - 1) {
                    touches_edge[static_cast<size_t>(cid)] = true;
                }

                for (int d = 0; d < 4; ++d) {
                    int nx = static_cast<int>(cx) + DX[d];
                    int ny = static_cast<int>(cy) + DY[d];
                    if (nx < 0 || nx >= static_cast<int>(w) || ny < 0 || ny >= static_cast<int>(h))
                        continue;
                    size_t ni = static_cast<size_t>(ny) * w + static_cast<size_t>(nx);
                    if (component_id[ni] >= 0)
                        continue;
                    if (!is_water[ni])
                        continue;
                    component_id[ni] = cid;
                    queue.push_back(ni);
                }
            }
        }
    }

    for (size_t c = 0; c < components.size(); ++c) {
        bool ocean = touches_edge[c] && components[c].size() >= OCEAN_MIN_TILES;
        for (size_t idx : components[c]) {
            if (ocean) {
                is_ocean[idx] = true;
            } else {
                is_lake[idx] = true;
            }
        }
    }
}

// ── Slope + Aspect + ElevBand from unified height ───────────────────────────

static void compute_slope_aspect_bands(const std::vector<float>& heights, uint32_t w, uint32_t h,
                                       float water_level, std::vector<float>& slope,
                                       std::vector<float>& aspect, std::vector<ElevBand>& bands) {
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            size_t idx = static_cast<size_t>(y) * w + x;
            float ht = heights[idx];

            // Gradient: central difference on unified height
            float dx = 0.0f, dy_val = 0.0f;
            if (x > 0 && x < w - 1) {
                dx = (heights[idx + 1] - heights[idx - 1]) * 0.5f;
            } else if (x == 0) {
                dx = heights[idx + 1] - heights[idx];
            } else {
                dx = heights[idx] - heights[idx - 1];
            }

            if (y > 0 && y < h - 1) {
                dy_val = (heights[(y + 1) * w + x] - heights[(y - 1) * w + x]) * 0.5f;
            } else if (y == 0) {
                dy_val = heights[(y + 1) * w + x] - heights[idx];
            } else {
                dy_val = heights[idx] - heights[(y - 1) * w + x];
            }

            float gradient = std::sqrt(dx * dx + dy_val * dy_val);
            slope[idx] = std::clamp(gradient * 10.0f, 0.0f, 1.0f);

            // Aspect: direction of steepest descent
            aspect[idx] = std::atan2(dy_val, dx);

            // ElevBand from unified height directly (no separate elevation offset)
            if (ht < water_level) {
                bands[idx] = ElevBand::Water;
            } else if (ht < water_level + 0.18f) {
                bands[idx] = ElevBand::Lowland;
            } else if (ht < water_level + 0.30f) {
                bands[idx] = ElevBand::Hills;
            } else {
                bands[idx] = ElevBand::Mountains;
            }
        }
    }
}

// ── Felzenszwalb Euclidean Distance Transform (1D) ──────────────────────────

static void edt_1d(const float* f, float* d, int n) {
    // Parabola envelope algorithm for squared Euclidean distance
    std::vector<int> v(static_cast<size_t>(n));
    std::vector<float> z(static_cast<size_t>(n) + 1);
    int k = 0;
    v[0] = 0;
    z[0] = -1e10f;
    z[1] = 1e10f;

    for (int q = 1; q < n; ++q) {
        float s = ((f[q] + static_cast<float>(q) * static_cast<float>(q)) -
                   (f[v[static_cast<size_t>(k)]] +
                    static_cast<float>(v[static_cast<size_t>(k)]) *
                        static_cast<float>(v[static_cast<size_t>(k)]))) /
                  (2.0f * static_cast<float>(q) - 2.0f * static_cast<float>(v[static_cast<size_t>(k)]));
        while (k >= 0 && s <= z[static_cast<size_t>(k)]) {
            --k;
            if (k >= 0) {
                s = ((f[q] + static_cast<float>(q) * static_cast<float>(q)) -
                     (f[v[static_cast<size_t>(k)]] +
                      static_cast<float>(v[static_cast<size_t>(k)]) *
                          static_cast<float>(v[static_cast<size_t>(k)]))) /
                    (2.0f * static_cast<float>(q) -
                     2.0f * static_cast<float>(v[static_cast<size_t>(k)]));
            }
        }
        ++k;
        v[static_cast<size_t>(k)] = q;
        z[static_cast<size_t>(k)] = s;
        z[static_cast<size_t>(k) + 1] = 1e10f;
    }

    k = 0;
    for (int q = 0; q < n; ++q) {
        while (z[static_cast<size_t>(k) + 1] < static_cast<float>(q))
            ++k;
        float dq = static_cast<float>(q) - static_cast<float>(v[static_cast<size_t>(k)]);
        d[q] = dq * dq + f[v[static_cast<size_t>(k)]];
    }
}

// 2D Euclidean Distance Transform using separable 1D passes
static void compute_edt(const std::vector<bool>& sources, uint32_t w, uint32_t h,
                         std::vector<float>& dist) {
    size_t size = static_cast<size_t>(w) * h;
    constexpr float SENTINEL = 1e10f;

    // Initialize: source cells = 0, others = large sentinel
    std::vector<float> grid(size);
    for (size_t i = 0; i < size; ++i) {
        grid[i] = sources[i] ? 0.0f : SENTINEL;
    }

    // Horizontal pass (rows)
    {
        std::vector<float> row_in(w);
        std::vector<float> row_out(w);
        for (uint32_t y = 0; y < h; ++y) {
            size_t row_start = static_cast<size_t>(y) * w;
            for (uint32_t x = 0; x < w; ++x)
                row_in[x] = grid[row_start + x];
            edt_1d(row_in.data(), row_out.data(), static_cast<int>(w));
            for (uint32_t x = 0; x < w; ++x)
                grid[row_start + x] = row_out[x];
        }
    }

    // Vertical pass (columns)
    {
        std::vector<float> col_in(h);
        std::vector<float> col_out(h);
        for (uint32_t x = 0; x < w; ++x) {
            for (uint32_t y = 0; y < h; ++y)
                col_in[y] = grid[static_cast<size_t>(y) * w + x];
            edt_1d(col_in.data(), col_out.data(), static_cast<int>(h));
            for (uint32_t y = 0; y < h; ++y)
                grid[static_cast<size_t>(y) * w + x] = col_out[y];
        }
    }

    // Take sqrt to get actual Euclidean distance (grid stores squared distances)
    dist.resize(size);
    for (size_t i = 0; i < size; ++i) {
        dist[i] = std::sqrt(grid[i]);
    }
}

// ── Soil fields ─────────────────────────────────────────────────────────────

static void compute_soil_fields(std::vector<float>& fertility, std::vector<float>& hold,
                                const std::vector<float>& slopes,
                                const std::vector<float>& dist_water,
                                const std::vector<ElevBand>& bands, uint32_t w, uint32_t h,
                                uint32_t seed) {
    FastNoiseLite fert_noise;
    fert_noise.SetSeed(static_cast<int>(seed + 4000));
    fert_noise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    fert_noise.SetFrequency(1.0f);

    FastNoiseLite mat_noise;
    mat_noise.SetSeed(static_cast<int>(seed + 4500));
    mat_noise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    mat_noise.SetFrequency(1.0f);

    float max_dist = static_cast<float>(w + h) * 0.25f;

    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            size_t idx = static_cast<size_t>(y) * w + x;

            if (bands[idx] == ElevBand::Water) {
                fertility[idx] = 0.0f;
                hold[idx] = 0.0f;
                continue;
            }

            float fn =
                fert_noise.GetNoise(static_cast<float>(x) * 0.03f, static_cast<float>(y) * 0.03f);
            float fert_base = fn * 0.5f + 0.5f;
            float water_prox = 1.0f - std::clamp(dist_water[idx] / max_dist, 0.0f, 1.0f);
            float slope_pen = slopes[idx] * 0.5f;
            fertility[idx] =
                std::clamp(fert_base * 0.5f + water_prox * 0.3f - slope_pen + 0.1f, 0.0f, 1.0f);

            float mn =
                mat_noise.GetNoise(static_cast<float>(x) * 0.025f, static_cast<float>(y) * 0.025f);
            float mat_base = mn * 0.5f + 0.5f;
            float lowland_bonus = (bands[idx] == ElevBand::Lowland) ? 0.2f : 0.0f;
            float steep_pen = slopes[idx] * 0.4f;
            hold[idx] = std::clamp(mat_base * 0.5f + lowland_bonus - steep_pen + 0.2f, 0.0f, 1.0f);
        }
    }
}

// ── Roughness as local height variance (3x3 stddev) ─────────────────────────

static void compute_roughness(std::vector<float>& roughness, const std::vector<float>& heights,
                              uint32_t w, uint32_t h) {
    constexpr float ROUGHNESS_SCALE = 20.0f;  // normalize stddev to [0,1] range

    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            size_t idx = static_cast<size_t>(y) * w + x;

            // Gather 3x3 neighborhood heights
            float sum = 0.0f;
            float sq_sum = 0.0f;
            int count = 0;

            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    int nx = static_cast<int>(x) + dx;
                    int ny = static_cast<int>(y) + dy;
                    if (nx < 0 || nx >= static_cast<int>(w) || ny < 0 ||
                        ny >= static_cast<int>(h))
                        continue;
                    float val = heights[static_cast<size_t>(ny) * w + static_cast<size_t>(nx)];
                    sum += val;
                    sq_sum += val * val;
                    ++count;
                }
            }

            float mean = sum / static_cast<float>(count);
            float variance = sq_sum / static_cast<float>(count) - mean * mean;
            float stddev = std::sqrt(std::max(0.0f, variance));
            roughness[idx] = std::clamp(stddev * ROUGHNESS_SCALE, 0.0f, 1.0f);
        }
    }
}

// ── Downhill routing (8-directional steepest descent) ───────────────────────

static void compute_downhill(const std::vector<float>& heights, uint32_t w, uint32_t h,
                             std::vector<int>& downhill_x, std::vector<int>& downhill_y) {
    size_t size = static_cast<size_t>(w) * h;
    downhill_x.assign(size, -1);
    downhill_y.assign(size, -1);

    constexpr int DX[] = {-1, 0, 1, -1, 1, -1, 0, 1};
    constexpr int DY[] = {-1, -1, -1, 0, 0, 1, 1, 1};

    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            size_t idx = static_cast<size_t>(y) * w + x;
            float my_h = heights[idx];
            float best_drop = 0.0f;
            for (int d = 0; d < 8; ++d) {
                int nx = static_cast<int>(x) + DX[d];
                int ny = static_cast<int>(y) + DY[d];
                if (nx < 0 || nx >= static_cast<int>(w) || ny < 0 || ny >= static_cast<int>(h))
                    continue;
                size_t ni = static_cast<size_t>(ny) * w + static_cast<size_t>(nx);
                float drop = my_h - heights[ni];
                if (drop > best_drop) {
                    best_drop = drop;
                    downhill_x[idx] = nx;
                    downhill_y[idx] = ny;
                }
            }
        }
    }
}

// ── Lake outflows ───────────────────────────────────────────────────────────
// Find the lowest land tile adjacent to each lake component. This is the spill
// point where the lake would overflow → forms a river outlet.
// Sets downhill routing for lake tiles to point toward the spill point.

static void compute_lake_outflows(const std::vector<float>& heights,
                                  const std::vector<bool>& lake_flags, uint32_t w, uint32_t h,
                                  float water_level, std::vector<int>& downhill_x,
                                  std::vector<int>& downhill_y) {
    size_t size = static_cast<size_t>(w) * h;
    constexpr int DX[] = {-1, 0, 1, -1, 1, -1, 0, 1};
    constexpr int DY[] = {-1, -1, -1, 0, 0, 1, 1, 1};

    // Identify lake components via 4-cardinal BFS (must match compute_ocean_lake connectivity)
    constexpr int CDX[] = {-1, 1, 0, 0};
    constexpr int CDY[] = {0, 0, -1, 1};

    std::vector<int> lake_id(size, -1);
    std::vector<std::vector<size_t>> lake_components;

    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            size_t idx = static_cast<size_t>(y) * w + x;
            if (!lake_flags[idx] || lake_id[idx] >= 0)
                continue;

            int cid = static_cast<int>(lake_components.size());
            lake_components.emplace_back();

            std::deque<size_t> queue;
            lake_id[idx] = cid;
            queue.push_back(idx);

            while (!queue.empty()) {
                size_t cur = queue.front();
                queue.pop_front();
                lake_components[static_cast<size_t>(cid)].push_back(cur);

                auto cx = static_cast<uint32_t>(cur % w);
                auto cy = static_cast<uint32_t>(cur / w);
                for (int d = 0; d < 4; ++d) {
                    int nx = static_cast<int>(cx) + CDX[d];
                    int ny = static_cast<int>(cy) + CDY[d];
                    if (nx < 0 || nx >= static_cast<int>(w) || ny < 0 ||
                        ny >= static_cast<int>(h))
                        continue;
                    size_t ni = static_cast<size_t>(ny) * w + static_cast<size_t>(nx);
                    if (lake_id[ni] >= 0 || !lake_flags[ni])
                        continue;
                    lake_id[ni] = cid;
                    queue.push_back(ni);
                }
            }
        }
    }

    // For each lake component, find the lowest adjacent land tile (spill point)
    std::vector<bool> visited(size, false);  // reused across lakes
    for (size_t c = 0; c < lake_components.size(); ++c) {
        int spill_x = -1, spill_y = -1;
        float lowest_land = 1e10f;

        for (size_t idx : lake_components[c]) {
            auto lx = static_cast<uint32_t>(idx % w);
            auto ly = static_cast<uint32_t>(idx / w);
            for (int d = 0; d < 8; ++d) {
                int nx = static_cast<int>(lx) + DX[d];
                int ny = static_cast<int>(ly) + DY[d];
                if (nx < 0 || nx >= static_cast<int>(w) || ny < 0 || ny >= static_cast<int>(h))
                    continue;
                size_t ni = static_cast<size_t>(ny) * w + static_cast<size_t>(nx);
                if (lake_flags[ni])
                    continue;  // skip other lake tiles
                if (heights[ni] >= water_level && heights[ni] < lowest_land) {
                    lowest_land = heights[ni];
                    spill_x = nx;
                    spill_y = ny;
                }
            }
        }

        if (spill_x < 0)
            continue;  // fully enclosed, no land neighbor

        // Point all lake tiles toward the spill point via BFS within the lake
        // Find the lake tile closest to the spill point
        size_t closest_lake = lake_components[c][0];
        float closest_dist = 1e10f;
        for (size_t idx : lake_components[c]) {
            auto lx = static_cast<int>(idx % w);
            auto ly = static_cast<int>(idx / w);
            float d2 = static_cast<float>((lx - spill_x) * (lx - spill_x) +
                                          (ly - spill_y) * (ly - spill_y));
            if (d2 < closest_dist) {
                closest_dist = d2;
                closest_lake = idx;
            }
        }

        // Set closest lake tile's downhill to the spill point
        downhill_x[closest_lake] = spill_x;
        downhill_y[closest_lake] = spill_y;

        // BFS from closest_lake outward through lake tiles, pointing each toward parent
        visited[closest_lake] = true;
        std::deque<size_t> bfs;
        bfs.push_back(closest_lake);

        while (!bfs.empty()) {
            size_t cur = bfs.front();
            bfs.pop_front();
            auto cx = static_cast<int>(cur % w);
            auto cy = static_cast<int>(cur / w);
            for (int d = 0; d < 8; ++d) {
                int nx = cx + DX[d];
                int ny = cy + DY[d];
                if (nx < 0 || nx >= static_cast<int>(w) || ny < 0 || ny >= static_cast<int>(h))
                    continue;
                size_t ni = static_cast<size_t>(ny) * w + static_cast<size_t>(nx);
                if (visited[ni] || !lake_flags[ni] || lake_id[ni] != static_cast<int>(c))
                    continue;
                visited[ni] = true;
                downhill_x[ni] = cx;
                downhill_y[ni] = cy;
                bfs.push_back(ni);
            }
        }

        // Clear visited flags for this lake (reuse across lakes)
        visited[closest_lake] = false;
        for (size_t idx : lake_components[c])
            visited[idx] = false;
    }
}

// ── Flow accumulation → river_flow ──────────────────────────────────────────
// Sort tiles by height (highest first), accumulate flow along downhill paths.

static void compute_flow_accumulation(const std::vector<float>& heights,
                                      const std::vector<int>& downhill_x,
                                      const std::vector<int>& downhill_y, uint32_t w, uint32_t h,
                                      std::vector<float>& river_flow) {
    size_t size = static_cast<size_t>(w) * h;
    river_flow.assign(size, 1.0f);  // each tile contributes 1 unit of flow

    // Sort indices by height, highest first
    std::vector<size_t> sorted_indices(size);
    std::iota(sorted_indices.begin(), sorted_indices.end(), 0);
    std::sort(sorted_indices.begin(), sorted_indices.end(),
              [&heights](size_t a, size_t b) { return heights[a] > heights[b]; });

    // Accumulate flow from high to low
    for (size_t idx : sorted_indices) {
        int dx = downhill_x[idx];
        int dy = downhill_y[idx];
        if (dx < 0 || dy < 0)
            continue;
        size_t target = static_cast<size_t>(dy) * w + static_cast<size_t>(dx);
        river_flow[target] += river_flow[idx];
    }

    // Log-scale for display (raw values can be huge)
    for (size_t i = 0; i < size; ++i) {
        river_flow[i] = std::log2(river_flow[i]);
    }
}

// ── Terrain generation ──────────────────────────────────────────────────────

Terrain generate_terrain(uint32_t width, uint32_t height, const EnvParams& env, uint32_t seed,
                         GenerationTimings* timings) {
    auto total_start = Clock::now();
    size_t size = static_cast<size_t>(width) * height;

    std::vector<float> height_field(size);
    std::vector<float> ridge_field(size);
    std::vector<bool> ocean_flags, lake_flags;
    std::vector<float> slope_field(size);
    std::vector<float> aspect_field(size);
    std::vector<ElevBand> band_field(size);
    std::vector<float> dist_ocean_field;
    std::vector<float> dist_water_field;
    std::vector<float> fertility_field(size);
    std::vector<float> hold_field(size);
    std::vector<float> roughness_field(size);
    std::vector<int> downhill_x_field;
    std::vector<int> downhill_y_field;
    std::vector<float> river_flow_field;

    // 1. Height (continental noise with domain warping)
    auto t0 = Clock::now();
    generate_height_field(height_field, width, height, seed, env.noise_scale);
    if (timings)
        timings->height_ms = elapsed_ms(t0);

    // 2. Ridge field (ridged noise for mountain ranges)
    t0 = Clock::now();
    generate_ridge_field(ridge_field, height_field, width, height, seed, env.water_level,
                         env.ridge_strength);
    if (timings)
        timings->ridge_ms = elapsed_ms(t0);

    // 3. Unify height = continent + ridge
    t0 = Clock::now();
    unify_height_field(height_field, ridge_field, size, env.water_level);
    if (timings)
        timings->unify_ms = elapsed_ms(t0);

    // 4. Ocean/Lake flood-fill (on unified height)
    t0 = Clock::now();
    compute_ocean_lake(height_field, width, height, env.water_level, ocean_flags, lake_flags);
    if (timings)
        timings->ocean_lake_ms = elapsed_ms(t0);

    // 5. Slope + Aspect + ElevBand (from unified height)
    t0 = Clock::now();
    compute_slope_aspect_bands(height_field, width, height, env.water_level, slope_field,
                               aspect_field, band_field);
    if (timings)
        timings->slope_band_ms = elapsed_ms(t0);

    // 6. Euclidean Distance Transform (ocean + all water)
    t0 = Clock::now();
    compute_edt(ocean_flags, width, height, dist_ocean_field);
    std::vector<bool> any_water(size);
    for (size_t i = 0; i < size; ++i) {
        any_water[i] = ocean_flags[i] || lake_flags[i];
    }
    compute_edt(any_water, width, height, dist_water_field);
    if (timings)
        timings->dist_fields_ms = elapsed_ms(t0);

    // 7. Soil fields
    t0 = Clock::now();
    compute_soil_fields(fertility_field, hold_field, slope_field, dist_water_field, band_field,
                        width, height, seed);
    if (timings)
        timings->soil_ms = elapsed_ms(t0);

    // 8. Roughness (3x3 stddev of unified height)
    t0 = Clock::now();
    compute_roughness(roughness_field, height_field, width, height);
    if (timings)
        timings->roughness_ms = elapsed_ms(t0);

    // 9. Downhill routing (8-directional steepest descent on unified height)
    t0 = Clock::now();
    compute_downhill(height_field, width, height, downhill_x_field, downhill_y_field);

    // 10. Lake outflows
    compute_lake_outflows(height_field, lake_flags, width, height, env.water_level,
                          downhill_x_field, downhill_y_field);
    if (timings)
        timings->downhill_ms = elapsed_ms(t0);

    // 11. Flow accumulation → river_flow
    t0 = Clock::now();
    compute_flow_accumulation(height_field, downhill_x_field, downhill_y_field, width, height,
                              river_flow_field);
    if (timings)
        timings->river_ms = elapsed_ms(t0);

    // 12. Assemble tiles
    std::vector<TerrainTile> tiles(size);
    for (size_t i = 0; i < size; ++i) {
        tiles[i] = TerrainTile{
            .elev01 = height_field[i],
            .band = band_field[i],
            .is_ocean = ocean_flags[i],
            .is_lake = lake_flags[i],
            .slope01 = slope_field[i],
            .aspect = aspect_field[i],
            .dist_ocean = dist_ocean_field[i],
            .dist_water = dist_water_field[i],
            .river_flow = river_flow_field[i],
            .soil_fertility = fertility_field[i],
            .soil_hold = hold_field[i],
            .roughness = roughness_field[i],
            .elevation = ridge_field[i],
            .downhill_x = downhill_x_field[i],
            .downhill_y = downhill_y_field[i],
        };
    }

    if (timings)
        timings->total_ms = elapsed_ms(total_start);

    return Terrain{
        .width = width,
        .height = height,
        .tiles = std::move(tiles),
    };
}

}  // namespace sandbox
