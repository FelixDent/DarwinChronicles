#include "terrain_gen.h"

#include <FastNoiseLite.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <numeric>
#include <queue>
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

// ── RockType names ──────────────────────────────────────────────────────────

const char* rock_type_name(RockType r) {
    switch (r) {
        case RockType::Granite:
            return "Granite";
        case RockType::Basalt:
            return "Basalt";
        case RockType::Limestone:
            return "Limestone";
        case RockType::Sandstone:
            return "Sandstone";
        case RockType::Shale:
            return "Shale";
        case RockType::Metamorphic:
            return "Metamorphic";
        default:
            return "Unknown";
    }
}

// ── SoilTexture names ───────────────────────────────────────────────────────

const char* soil_texture_name(SoilTexture s) {
    switch (s) {
        case SoilTexture::Sand:
            return "Sand";
        case SoilTexture::Loam:
            return "Loam";
        case SoilTexture::Silt:
            return "Silt";
        case SoilTexture::Clay:
            return "Clay";
        case SoilTexture::Peat:
            return "Peat";
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

// ── Tectonic plate generation ───────────────────────────────────────────────
// Generates terrain from simulated tectonic plates: Voronoi partition → plate
// motion → boundary classification → height from collision/spreading physics.
// Produces long mountain chains, continental shields, ocean ridges, and
// realistic drainage basins that noise alone cannot create.

struct TectonicPlate {
    float cx, cy;              // center position (tile coordinates)
    float vx, vy;              // velocity vector
    bool is_continental;       // true = continental, false = oceanic
    float spine_dx, spine_dy;  // normalized spine direction (perpendicular to velocity)
    float spine_offset;        // random offset for spine position variation
};

// Simple seeded random: splitmix-style hash
static uint32_t plate_hash(uint32_t x) {
    x ^= x >> 16;
    x *= 0x45d9f3bU;
    x ^= x >> 16;
    x *= 0x45d9f3bU;
    x ^= x >> 16;
    return x;
}

static float plate_randf(uint32_t& state) {
    state = plate_hash(state);
    return static_cast<float>(state & 0xFFFF) / 65535.0f;
}

static void generate_tectonic_terrain(std::vector<float>& height_field,
                                      std::vector<float>& ridge_field, uint32_t w, uint32_t h,
                                      uint32_t seed, const EnvParams& env) {
    uint32_t n_plates = std::clamp(env.plate_count, 4u, 30u);
    float fw = static_cast<float>(w);
    float fh = static_cast<float>(h);
    size_t total = static_cast<size_t>(w) * h;

    // ── 1. Generate plate centers and properties ────────────────────────
    std::vector<TectonicPlate> plates(n_plates);
    uint32_t rng = seed + 77777;

    // Ensure a minimum number of continental plates (at least 2)
    uint32_t target_cont = std::max(
        2u,
        static_cast<uint32_t>(std::round(static_cast<float>(n_plates) * env.continental_ratio)));
    uint32_t cont_assigned = 0;

    for (uint32_t i = 0; i < n_plates; ++i) {
        auto& p = plates[i];
        p.cx = (plate_randf(rng) * 0.8f + 0.1f) * fw;
        p.cy = (plate_randf(rng) * 0.8f + 0.1f) * fh;
        float angle = plate_randf(rng) * 6.2831853f;
        float speed = 1.0f + plate_randf(rng) * 3.0f;
        p.vx = std::cos(angle) * speed;
        p.vy = std::sin(angle) * speed;

        // Deterministic continental assignment: first target_cont plates are continental
        if (cont_assigned < target_cont) {
            p.is_continental = true;
            ++cont_assigned;
        } else {
            p.is_continental = plate_randf(rng) < 0.1f;
        }
    }

    // Compute spine direction for each continental plate
    // Spine runs perpendicular to the plate's drift direction (like the Andes/Rockies)
    for (uint32_t i = 0; i < n_plates; ++i) {
        auto& p = plates[i];
        float vmag = std::sqrt(p.vx * p.vx + p.vy * p.vy);
        if (vmag > 0.01f && p.is_continental) {
            // Spine perpendicular to velocity
            p.spine_dx = -p.vy / vmag;
            p.spine_dy = p.vx / vmag;
        } else {
            // Random spine direction for static plates
            float angle = plate_randf(rng) * 6.2831853f;
            p.spine_dx = std::cos(angle);
            p.spine_dy = std::sin(angle);
        }
        p.spine_offset = (plate_randf(rng) - 0.5f) * 30.0f;
    }

    // Bias continental plate velocities toward the map center to create convergence.
    // This simulates supercontinent assembly — continental plates drift inward.
    float cx_mean = fw * 0.5f;
    float cy_mean = fh * 0.5f;
    for (uint32_t i = 0; i < n_plates; ++i) {
        if (!plates[i].is_continental)
            continue;
        float to_cx = cx_mean - plates[i].cx;
        float to_cy = cy_mean - plates[i].cy;
        float dist = std::sqrt(to_cx * to_cx + to_cy * to_cy);
        if (dist > 1.0f) {
            float bias = 1.8f;
            plates[i].vx += (to_cx / dist) * bias;
            plates[i].vy += (to_cy / dist) * bias;
        }
    }

    // Also bias oceanic plates slightly outward (spreading) to enhance divergence
    for (uint32_t i = 0; i < n_plates; ++i) {
        if (plates[i].is_continental)
            continue;
        float from_cx = plates[i].cx - cx_mean;
        float from_cy = plates[i].cy - cy_mean;
        float dist = std::sqrt(from_cx * from_cx + from_cy * from_cy);
        if (dist > 1.0f) {
            plates[i].vx += (from_cx / dist) * 0.6f;
            plates[i].vy += (from_cy / dist) * 0.6f;
        }
    }

    // ── 2. Domain-warped Voronoi: warp coordinates BEFORE plate lookup ──
    // Two layers of low-frequency warp to curve plate boundaries.
    FastNoiseLite warp1a, warp1b, warp2a, warp2b;
    warp1a.SetSeed(static_cast<int>(seed + 10000));
    warp1a.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    warp1a.SetFrequency(1.0f);
    warp1b.SetSeed(static_cast<int>(seed + 11000));
    warp1b.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    warp1b.SetFrequency(1.0f);
    warp2a.SetSeed(static_cast<int>(seed + 12000));
    warp2a.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    warp2a.SetFrequency(1.0f);
    warp2b.SetSeed(static_cast<int>(seed + 13000));
    warp2b.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    warp2b.SetFrequency(1.0f);

    constexpr float WARP1_STRENGTH =
        65.0f;                            // stronger continental warping to break Voronoi geometry
    constexpr float WARP1_FREQ = 0.004f;  // very low frequency
    constexpr float WARP2_STRENGTH = 25.0f;  // stronger secondary warping
    constexpr float WARP2_FREQ = 0.010f;     // medium frequency

    // Per-tile: warped plate lookup, boundary distance, boundary info
    std::vector<float> warped_x(total);  // warped x coord for plate lookup
    std::vector<float> warped_y(total);  // warped y coord for plate lookup
    std::vector<uint32_t> plate_id(total);
    std::vector<uint32_t> second_plate_id(total);
    std::vector<float> boundary_dist(total);
    std::vector<float> boundary_nx(total);        // boundary normal x
    std::vector<float> boundary_ny(total);        // boundary normal y
    std::vector<float> boundary_tx(total);        // boundary tangent x
    std::vector<float> boundary_ty(total);        // boundary tangent y
    std::vector<float> convergence_field(total);  // signed convergence at each tile

    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            size_t idx = static_cast<size_t>(y) * w + x;
            float fx = static_cast<float>(x);
            float fy = static_cast<float>(y);

            // Two-layer domain warp: warp plate-space coordinates
            float w1x = warp1a.GetNoise(fx * WARP1_FREQ, fy * WARP1_FREQ) * WARP1_STRENGTH;
            float w1y = warp1b.GetNoise(fx * WARP1_FREQ, fy * WARP1_FREQ) * WARP1_STRENGTH;
            float wpx = fx + w1x;
            float wpy = fy + w1y;
            float w2x = warp2a.GetNoise(wpx * WARP2_FREQ, wpy * WARP2_FREQ) * WARP2_STRENGTH;
            float w2y = warp2b.GetNoise(wpx * WARP2_FREQ, wpy * WARP2_FREQ) * WARP2_STRENGTH;
            wpx += w2x;
            wpy += w2y;
            warped_x[idx] = wpx;
            warped_y[idx] = wpy;

            // Find closest and second-closest plate at warped coordinates
            float best_d2 = 1e18f;
            float second_d2 = 1e18f;
            uint32_t best_id = 0;
            uint32_t second_id = 0;  // overwritten in first loop iteration

            for (uint32_t i = 0; i < n_plates; ++i) {
                float dx = wpx - plates[i].cx;
                float dy = wpy - plates[i].cy;
                float d2 = dx * dx + dy * dy;
                if (d2 < best_d2) {
                    second_d2 = best_d2;
                    second_id = best_id;
                    best_d2 = d2;
                    best_id = i;
                } else if (d2 < second_d2) {
                    second_d2 = d2;
                    second_id = i;
                }
            }

            plate_id[idx] = best_id;
            second_plate_id[idx] = second_id;
            float d1 = std::sqrt(best_d2);
            float d2s = std::sqrt(second_d2);
            float raw_bd = d2s - d1;
            // Moderate noise on boundary distance to break linearity without scattering
            float bd_noise = warp1a.GetNoise(fx * 0.010f, fy * 0.010f) * 5.0f +
                             warp2a.GetNoise(fx * 0.022f, fy * 0.022f) * 2.0f;
            boundary_dist[idx] = std::max(0.0f, raw_bd + bd_noise);

            // Boundary normal and tangent (from plate A center → plate B center)
            const auto& pa = plates[best_id];
            const auto& pb = plates[second_id];
            float bnx = pb.cx - pa.cx;
            float bny = pb.cy - pa.cy;
            float bn_len = std::sqrt(bnx * bnx + bny * bny);
            if (bn_len > 0.01f) {
                bnx /= bn_len;
                bny /= bn_len;
            }
            // Gently rotate boundary normal/tangent to break straight-edge alignment
            // Moderate rotation preserves structural coherence while reducing linearity
            float rot_angle = warp1b.GetNoise(fx * 0.007f, fy * 0.007f) * 0.4f +
                              warp2b.GetNoise(fx * 0.020f, fy * 0.020f) * 0.2f;  // up to ~35° total
            float cos_r = std::cos(rot_angle);
            float sin_r = std::sin(rot_angle);
            float bnx_rot = bnx * cos_r - bny * sin_r;
            float bny_rot = bnx * sin_r + bny * cos_r;
            boundary_nx[idx] = bnx_rot;
            boundary_ny[idx] = bny_rot;
            boundary_tx[idx] = -bny_rot;
            boundary_ty[idx] = bnx_rot;

            // Convergence: relative velocity projected onto boundary normal
            float rel_vx = pa.vx - pb.vx;
            float rel_vy = pa.vy - pb.vy;
            float conv = rel_vx * bnx + rel_vy * bny;
            convergence_field[idx] = std::clamp(conv / 8.0f, -1.0f, 1.0f);
        }
    }

    // ── 3. Build continuous tectonic fields ──────────────────────────────

    const float CONT_BASE = std::max(0.58f, env.water_level + 0.12f);
    constexpr float OCEAN_BASE = 0.22f;
    constexpr float MOUNTAIN_WIDTH = 35.0f;
    constexpr float RIFT_WIDTH = 20.0f;
    constexpr float MOUNTAIN_PEAK = 0.75f;  // boosted for more dramatic mountains
    constexpr float RIFT_DEPTH = 0.10f;
    constexpr float PLATEAU_WIDTH = 65.0f;
    constexpr float TRENCH_DEPTH = 0.06f;

    // Ridged noise for mountain peaks
    FastNoiseLite ridge_noise;
    ridge_noise.SetSeed(static_cast<int>(seed + 5000));
    ridge_noise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    ridge_noise.SetFrequency(1.0f);
    ridge_noise.SetFractalType(FastNoiseLite::FractalType_Ridged);
    ridge_noise.SetFractalOctaves(4);
    ridge_noise.SetFractalLacunarity(2.2f);
    ridge_noise.SetFractalGain(0.5f);

    // Curl noise for warping ridge direction field (breaks straight ridges)
    FastNoiseLite curl_warp_x, curl_warp_y;
    curl_warp_x.SetSeed(static_cast<int>(seed + 26000));
    curl_warp_x.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    curl_warp_x.SetFractalType(FastNoiseLite::FractalType_FBm);
    curl_warp_x.SetFractalOctaves(2);
    curl_warp_x.SetFrequency(0.008f);
    curl_warp_y.SetSeed(static_cast<int>(seed + 26001));
    curl_warp_y.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    curl_warp_y.SetFractalType(FastNoiseLite::FractalType_FBm);
    curl_warp_y.SetFractalOctaves(2);
    curl_warp_y.SetFrequency(0.008f);

    // Lithology noise: spatially varies ridge frequency (different rock types)
    FastNoiseLite lithology_noise;
    lithology_noise.SetSeed(static_cast<int>(seed + 27000));
    lithology_noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    lithology_noise.SetFractalType(FastNoiseLite::FractalType_FBm);
    lithology_noise.SetFractalOctaves(2);
    lithology_noise.SetFrequency(0.005f);

    // Piedmont roughness noise (foothills at mountain margins)
    FastNoiseLite piedmont_noise;
    piedmont_noise.SetSeed(static_cast<int>(seed + 28000));
    piedmont_noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    piedmont_noise.SetFractalType(FastNoiseLite::FractalType_FBm);
    piedmont_noise.SetFractalOctaves(3);
    piedmont_noise.SetFrequency(0.025f);

    // Continental crustal thickness noise — large amplitude to break plate shapes
    FastNoiseLite crust_noise;
    crust_noise.SetSeed(static_cast<int>(seed + 3000));
    crust_noise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    crust_noise.SetFrequency(1.0f);
    crust_noise.SetFractalType(FastNoiseLite::FractalType_FBm);
    crust_noise.SetFractalOctaves(4);
    crust_noise.SetFractalLacunarity(2.0f);
    crust_noise.SetFractalGain(0.5f);

    // Secondary continental warp noise — deforms continent boundaries
    FastNoiseLite cont_warp_noise;
    cont_warp_noise.SetSeed(static_cast<int>(seed + 4000));
    cont_warp_noise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    cont_warp_noise.SetFrequency(1.0f);
    cont_warp_noise.SetFractalType(FastNoiseLite::FractalType_FBm);
    cont_warp_noise.SetFractalOctaves(3);
    cont_warp_noise.SetFractalLacunarity(2.0f);
    cont_warp_noise.SetFractalGain(0.5f);

    // Micro-detail noise (multi-octave FBm)
    FastNoiseLite detail_noise;
    detail_noise.SetSeed(static_cast<int>(seed));
    detail_noise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    detail_noise.SetFrequency(1.0f);
    detail_noise.SetFractalType(FastNoiseLite::FractalType_FBm);
    detail_noise.SetFractalOctaves(6);
    detail_noise.SetFractalLacunarity(2.0f);
    detail_noise.SetFractalGain(0.45f);

    // Large-scale terrain variation — breaks flat continental interiors
    FastNoiseLite terrain_var_noise;
    terrain_var_noise.SetSeed(static_cast<int>(seed + 6000));
    terrain_var_noise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    terrain_var_noise.SetFrequency(1.0f);
    terrain_var_noise.SetFractalType(FastNoiseLite::FractalType_FBm);
    terrain_var_noise.SetFractalOctaves(3);
    terrain_var_noise.SetFractalLacunarity(2.0f);
    terrain_var_noise.SetFractalGain(0.5f);

    // Basin noise — creates inland depressions for lakes and valleys
    FastNoiseLite basin_noise;
    basin_noise.SetSeed(static_cast<int>(seed + 7000));
    basin_noise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    basin_noise.SetFrequency(1.0f);
    basin_noise.SetFractalType(FastNoiseLite::FractalType_FBm);
    basin_noise.SetFractalOctaves(3);
    basin_noise.SetFractalLacunarity(2.2f);
    basin_noise.SetFractalGain(0.5f);

    // Meso-scale ridge noise — interior escarpments, secondary ridges
    FastNoiseLite meso_ridge_noise;
    meso_ridge_noise.SetSeed(static_cast<int>(seed + 8000));
    meso_ridge_noise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    meso_ridge_noise.SetFrequency(1.0f);
    meso_ridge_noise.SetFractalType(FastNoiseLite::FractalType_Ridged);
    meso_ridge_noise.SetFractalOctaves(3);
    meso_ridge_noise.SetFractalLacunarity(2.0f);
    meso_ridge_noise.SetFractalGain(0.5f);

    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            size_t idx = static_cast<size_t>(y) * w + x;
            float fx = static_cast<float>(x);
            float fy = static_cast<float>(y);

            uint32_t pid = plate_id[idx];
            uint32_t sid = second_plate_id[idx];
            const auto& pa = plates[pid];
            const auto& pb = plates[sid];

            float bd = boundary_dist[idx];
            float conv = convergence_field[idx];
            float bnx = boundary_nx[idx];
            float bny = boundary_ny[idx];
            float btx = boundary_tx[idx];
            float bty = boundary_ty[idx];

            // ── Province mask: craton vs active zone ─────────────────────
            // Areas far from plate boundaries with low convergence are stable
            // cratons — they get much less noise (true plains/shields)
            // Short length scale (20) so suppression kicks in close to boundaries
            float tectonic_activity = std::abs(conv) * std::exp(-bd / 20.0f);
            float province_mask = std::clamp(tectonic_activity * 15.0f, 0.02f, 1.0f);

            // ── Macro: continuous continental field ──────────────────────
            // Instead of hard plate→continent mapping, blend plate type with
            // multiple noise layers so continent shapes diverge from plate polygons.
            float plate_cont = pa.is_continental ? 1.0f : 0.0f;
            float second_cont = pb.is_continental ? 1.0f : 0.0f;
            // Boundary blending: smooth transition between plate types
            float blend = std::exp(-bd * bd / (35.0f * 35.0f));
            float blended_cont =
                plate_cont * (1.0f - blend * 0.35f) + second_cont * (blend * 0.35f);

            // Crustal thickness noise — large amplitude breaks plate polygon shapes
            float crust = crust_noise.GetNoise(fx * 0.006f, fy * 0.006f);
            // Continental warp: a second independent noise that deforms continent edges
            float cont_warp = cont_warp_noise.GetNoise(fx * 0.008f, fy * 0.008f);

            // Continentality: plate type + noise layers = land/ocean probability
            float continentality = blended_cont * 0.55f + crust * 0.25f + cont_warp * 0.20f;
            // Map to elevation: continental threshold determines coastline
            float cont_threshold = env.continent_threshold * 0.55f;
            float base;
            if (continentality > cont_threshold) {
                // Land: map [threshold, 1] → [water_level+margin, CONT_BASE+headroom]
                float t = (continentality - cont_threshold) / (1.0f - cont_threshold);
                float land_ceil = CONT_BASE + 0.05f;
                base = env.water_level + 0.04f + t * (land_ceil - env.water_level - 0.04f);
            } else {
                // Ocean: map [min, threshold] → [OCEAN_BASE, water_level-margin]
                float t = continentality / std::max(cont_threshold, 0.01f);
                base = OCEAN_BASE + t * (env.water_level - 0.06f - OCEAN_BASE);
            }

            // ── Continental interior macro-structure ─────────────────────
            // Each continent gets: shield uplift, spine ridge, meso variation
            if (pa.is_continental && base > env.water_level) {
                float to_cx = fx - pa.cx;
                float to_cy = fy - pa.cy;
                float dist_center = std::sqrt(to_cx * to_cx + to_cy * to_cy);

                // Shield: broad interior plateau
                float shield = 0.06f * std::exp(-dist_center * dist_center / (90.0f * 90.0f));
                base += shield;

                // Spine: directional ridge through continental interior
                // Distance from the spine line through the plate center
                float spine_dist =
                    std::abs(to_cx * pa.spine_dx + to_cy * pa.spine_dy - pa.spine_offset);
                // Spine envelope: wider Gaussian (σ=35) for broader mountain belt
                float spine_env = std::exp(-spine_dist * spine_dist / (35.0f * 35.0f));
                // Modulate with ridge noise along spine for peaks/gaps
                float spine_coord = to_cx * pa.spine_dy - to_cy * pa.spine_dx;
                float spine_mod = ridge_noise.GetNoise(spine_coord * 0.015f, spine_dist * 0.04f);
                spine_mod = 0.3f + 0.7f * std::max(0.0f, spine_mod);
                // Along-spine segmentation: creates gaps and intensity variation
                float spine_seg =
                    0.4f + 0.6f * (0.5f + 0.5f * crust_noise.GetNoise(spine_coord * 0.005f, 0.0f));
                // Scale spine by distance from plate center (weaker at edges)
                float center_fade = std::exp(-dist_center * dist_center / (100.0f * 100.0f));
                float spine_uplift =
                    0.12f * spine_env * spine_mod * spine_seg * center_fade * env.ridge_strength;
                base += spine_uplift;

                // Meso-scale ridges: interior escarpments and secondary ridge arcs
                // Suppress near coast + in stable interiors (only develop in active zones)
                float meso = meso_ridge_noise.GetNoise(fx * 0.012f, fy * 0.012f);
                float meso_coast = std::clamp((base - env.water_level) / 0.12f, 0.0f, 1.0f);
                float meso_contrib =
                    std::max(0.0f, meso) * 0.08f * center_fade * meso_coast * province_mask;
                base += meso_contrib;
            }

            // ── Large-scale terrain variation ────────────────────────────
            // Scale by province mask so cratons have smooth broad surfaces
            float terrain_var = terrain_var_noise.GetNoise(fx * 0.004f, fy * 0.004f);
            float var_scale =
                (base > env.water_level)
                    ? 0.02f + 0.10f * province_mask  // 0.02-0.12: cratons get gentle rolls
                    : 0.04f;
            base += terrain_var * var_scale;

            // ── Basin depressions: inland lakes and valleys ──────────────
            if (base > env.water_level) {
                float basin_val = basin_noise.GetNoise(fx * 0.012f, fy * 0.012f);
                if (basin_val < -0.2f) {
                    float basin_strength = (-basin_val - 0.2f) / 0.8f;
                    float inland = std::min(bd / 25.0f, 1.0f);
                    float depression = basin_strength * inland * 0.14f;
                    base -= depression;
                } else if (basin_val < 0.0f) {
                    base -= (-basin_val) * 0.04f;
                }
            }

            // ── Boundary stress → mountains / rifts / trenches ──────────
            float boundary_uplift = 0.0f;
            float rv = 0.0f;

            float wx = warped_x[idx];
            float wy = warped_y[idx];
            // Curl-warp the ridge coordinate space to bend straight features
            float curl_dx = curl_warp_x.GetNoise(fx, fy) * 22.0f;
            float curl_dy = curl_warp_y.GetNoise(fx, fy) * 22.0f;
            float cwx = wx + curl_dx;
            float cwy = wy + curl_dy;
            float ridge_x = cwx * btx + cwy * bty;
            float ridge_y = cwx * bnx + cwy * bny;
            // Spatially varying ridge frequency (different "lithologies")
            float litho = lithology_noise.GetNoise(fx, fy);
            float freq_mod = 0.7f + litho * 0.6f;  // 0.4 to 1.3x
            // Multi-direction ridge noise with curl-warped coords + varied frequency
            float aniso1 = ridge_noise.GetNoise(ridge_x * env.noise_scale * 0.6f * freq_mod,
                                                ridge_y * env.noise_scale * 3.0f * freq_mod);
            float rot_x = ridge_x * 0.866f + ridge_y * 0.5f;
            float rot_y = -ridge_x * 0.5f + ridge_y * 0.866f;
            float aniso2 = ridge_noise.GetNoise(rot_x * env.noise_scale * 0.8f * freq_mod,
                                                rot_y * env.noise_scale * 2.5f * freq_mod);
            float iso_noise =
                detail_noise.GetNoise(fx * env.noise_scale * 1.5f, fy * env.noise_scale * 1.5f);
            float aniso_noise = aniso1 * 0.40f + aniso2 * 0.25f + iso_noise * 0.35f;
            float belt_var = 0.60f + 0.40f * crust_noise.GetNoise(ridge_x * env.noise_scale * 0.2f,
                                                                  ridge_y * env.noise_scale * 0.2f);
            // Along-strike segmentation: modulate uplift along boundary tangent
            // Creates gaps, bends, and intensity variation in mountain belts.
            // Two scales: (1) broad segmentation (40-90 tile wavelength) divides
            // ranges into massifs and basins, (2) transfer faults (sharp narrow
            // corridors perpendicular to strike) create cross-cutting valleys.
            float along_strike = btx * wx + bty * wy;  // warped coords, consistent with ridge noise
            // Broad segmentation: valleys between major range segments
            float strike_broad =
                crust_noise.GetNoise(along_strike * 0.005f, bd * 0.01f);
            // Transfer fault corridors: perpendicular to strike, sharp and narrow.
            // Real orogens have transform offsets every 200-500km (San Andreas,
            // Alpine Fault, Dead Sea Transform). These create deep transverse
            // valleys that break ranges into separate massifs.
            float across_strike = -bty * wx + btx * wy;
            float transfer_noise =
                detail_noise.GetNoise(along_strike * 0.008f, across_strike * 0.003f);
            // Lower threshold = more frequent gaps; steeper ramp = sharper corridors
            float transfer_gap = (transfer_noise > 0.40f) ? std::clamp((transfer_noise - 0.40f) * 3.5f, 0.0f, 0.70f) : 0.0f;
            // Combined: broad segments create 20-80% uplift variation + transfer faults
            float strike_mod = 0.20f + 0.80f * std::clamp(strike_broad * 0.5f + 0.5f, 0.0f, 1.0f);
            strike_mod *= (1.0f - transfer_gap);
            belt_var *= strike_mod;

            if (conv > 0.05f) {
                float strength = conv * env.ridge_strength * belt_var;
                bool a_cont = pa.is_continental;
                bool b_cont = pb.is_continental;
                float ridge_mod =
                    0.15f + 0.85f * std::max(0.0f, aniso_noise);  // sharp but with some base

                if (a_cont && b_cont) {
                    // Continent-continent collision: THRUST BELT MODEL
                    // Real fold-thrust belts (Himalayas, Alps, Rockies) consist of
                    // 3-5 parallel thrust sheets at increasing distance from the
                    // suture zone. Each sheet is a narrow ridge (σ=8-15 tiles),
                    // creating alternating ranges and intermontane valleys.
                    //
                    // Himalayan analog:
                    //   Sheet 0 (bd~0): Main Frontal Thrust — highest peaks
                    //   Sheet 1 (bd~15): Main Boundary Thrust — second range
                    //   Sheet 2 (bd~30): Main Central Thrust — inner range
                    //   Sheet 3 (bd~50): Tibetan Plateau — broader, lower uplift
                    //
                    // Each sheet has its own ridge modulation so peaks/saddles
                    // don't align across sheets (offset noise phases).
                    constexpr int N_SHEETS = 4;
                    constexpr float sheet_dist[N_SHEETS] = {0.0f, 15.0f, 30.0f, 50.0f};
                    constexpr float sheet_sigma[N_SHEETS] = {10.0f, 12.0f, 14.0f, 25.0f};
                    constexpr float sheet_amp[N_SHEETS] = {1.0f, 0.75f, 0.55f, 0.35f};

                    float total_uplift = 0.0f;
                    float max_rv = 0.0f;
                    for (int s = 0; s < N_SHEETS; ++s) {
                        float d = bd - sheet_dist[s];
                        float env_s = std::exp(-d * d / (sheet_sigma[s] * sheet_sigma[s]));
                        // Each sheet gets its own ridge modulation (offset noise phase)
                        float sheet_ridge = 0.15f + 0.85f * std::max(0.0f,
                            ridge_noise.GetNoise(
                                ridge_x * env.noise_scale * (1.0f + s * 0.3f),
                                ridge_y * env.noise_scale * (1.0f + s * 0.15f)));
                        float sheet_uplift = MOUNTAIN_PEAK * sheet_amp[s] * strength *
                                             env_s * sheet_ridge;
                        total_uplift += sheet_uplift;
                        if (s == 0) max_rv = env_s * strength;
                    }
                    boundary_uplift = total_uplift;
                    rv = max_rv;

                    // Foreland basin: depression in front of thrust belt
                    float basin_zone = std::exp(-(bd - 65.0f) * (bd - 65.0f) / (15.0f * 15.0f));
                    boundary_uplift -= 0.06f * strength * basin_zone;
                } else if (a_cont != b_cont) {
                    // Subduction: TWO-RIDGE CORDILLERA model (Andes analog)
                    // Real subduction produces a coastal range (volcanic arc) and
                    // a secondary inner range with a back-arc basin between them.
                    // Andes: Western Cordillera (volcanic) + Eastern Cordillera
                    //        + Altiplano between them.
                    float offset_bd = bd;
                    if (!a_cont) offset_bd += 8.0f;

                    // Primary volcanic arc: narrow, high, near the trench
                    float arc1_env = std::exp(-offset_bd * offset_bd / (12.0f * 12.0f));
                    float arc1 = MOUNTAIN_PEAK * 1.1f * strength * arc1_env * ridge_mod;

                    // Secondary range: broader, lower, further inland
                    float d2 = offset_bd - 25.0f;
                    float arc2_env = std::exp(-d2 * d2 / (15.0f * 15.0f));
                    float arc2_ridge = 0.15f + 0.85f * std::max(0.0f,
                        ridge_noise.GetNoise(
                            ridge_x * env.noise_scale * 1.4f,
                            ridge_y * env.noise_scale * 0.8f));
                    float arc2 = MOUNTAIN_PEAK * 0.65f * strength * arc2_env * arc2_ridge;

                    boundary_uplift = arc1 + arc2;

                    // Back-arc basin between the two ranges
                    float backarc_d = offset_bd - 15.0f;
                    float backarc_env = std::exp(-backarc_d * backarc_d / (8.0f * 8.0f));
                    boundary_uplift -= 0.08f * strength * backarc_env;

                    // Offshore trench
                    if (bd < 10.0f && !a_cont) {
                        float trench_falloff = std::exp(-bd / 5.0f);
                        boundary_uplift -= TRENCH_DEPTH * strength * trench_falloff;
                    }
                    rv = arc1_env * strength;
                } else {
                    // Ocean-ocean: island arc
                    float falloff = std::exp(-bd / (MOUNTAIN_WIDTH * 0.6f));
                    float arc = MOUNTAIN_PEAK * 0.6f * strength * falloff * ridge_mod;
                    boundary_uplift = arc;
                    rv = falloff * strength * 0.5f;
                }
            } else if (conv < -0.05f) {
                float strength = -conv;
                bool a_cont = pa.is_continental;
                bool b_cont = pb.is_continental;

                if (a_cont && b_cont) {
                    float falloff = std::exp(-bd / RIFT_WIDTH);
                    boundary_uplift = -RIFT_DEPTH * 1.5f * strength * falloff;
                    float shoulder = std::exp(-(bd - 15.0f) * (bd - 15.0f) / (10.0f * 10.0f));
                    boundary_uplift += 0.04f * strength * shoulder;
                } else if (!a_cont && !b_cont) {
                    float falloff = std::exp(-bd / RIFT_WIDTH);
                    boundary_uplift = -RIFT_DEPTH * 0.5f * strength * falloff;
                    if (bd < 5.0f) {
                        boundary_uplift += 0.06f * strength * std::exp(-bd / 3.0f);
                        rv = 0.3f * std::exp(-bd / 3.0f);
                    }
                } else {
                    float falloff = std::exp(-bd / RIFT_WIDTH);
                    boundary_uplift = -RIFT_DEPTH * strength * falloff;
                }
            } else {
                float falloff = std::exp(-bd / 12.0f);
                boundary_uplift = -0.03f * falloff;
                boundary_uplift += 0.01f * aniso_noise * falloff;
            }

            // ── Coast-aware suppression: shift mountains well inland ─────────
            // Stronger suppression near coasts to prevent coast-hugging ridges
            {
                float coast_margin = std::clamp((base - env.water_level) / 0.20f, 0.0f, 1.0f);
                coast_margin =
                    coast_margin * coast_margin * coast_margin;  // cubic: very strong near coast
                boundary_uplift *= coast_margin;
                rv *= coast_margin;
            }

            // ── Secondary ridges: foothills branching off main chain ────
            if (boundary_uplift > 0.03f) {
                float spur_noise = ridge_noise.GetNoise(ridge_x * env.noise_scale * 2.5f,
                                                        ridge_y * env.noise_scale * 1.5f);
                float spur = std::max(0.0f, spur_noise) * boundary_uplift * 0.4f;
                float spur_zone = std::exp(-bd / (MOUNTAIN_WIDTH * 1.8f)) -
                                  std::exp(-bd / (MOUNTAIN_WIDTH * 0.4f));
                spur_zone = std::max(0.0f, spur_zone);
                boundary_uplift += spur * spur_zone;
            }

            // ── Piedmont foothills: rough terrain at mountain margins ──────
            // Where boundary uplift is moderate (foothill zone), add broken terrain
            if (boundary_uplift > 0.02f && boundary_uplift < 0.30f) {
                float piedmont_zone =
                    std::clamp(boundary_uplift / 0.12f, 0.0f, 1.0f) *
                    std::clamp(1.0f - (boundary_uplift - 0.12f) / 0.18f, 0.0f, 1.0f);
                float pn = piedmont_noise.GetNoise(fx, fy);
                boundary_uplift += pn * 0.06f * piedmont_zone * province_mask;
            }

            // ── Micro: detail noise (stronger in mountains for ridge/valley texture) ──
            float detail = detail_noise.GetNoise(fx * env.noise_scale, fy * env.noise_scale);
            float detail_hi =
                detail_noise.GetNoise(fx * env.noise_scale * 2.5f, fy * env.noise_scale * 2.5f);
            float elev_so_far = base + boundary_uplift;
            // Mountain boost: ramps from 0 at lowland to 1.0 at high peaks.
            // Starts at 0.48 (just above water level + continental base) so
            // mid-elevation hills also get tectonic deformation.
            float mountain_boost = std::clamp((elev_so_far - 0.48f) * 3.0f, 0.0f, 1.0f);
            // Detail scale: tectonic deformation creates local folding, faulting,
            // and compositional variation. Stronger in mountain areas where
            // compression intensifies deformation — gives erosion features to carve.
            float detail_ceil = 0.16f + mountain_boost * 0.12f;  // 0.16 lowland, 0.28 mountains
            float detail_scale = std::clamp((elev_so_far - 0.35f) * 2.5f, 0.005f, detail_ceil);
            float detail_mask = std::max(province_mask, 0.15f);
            detail_scale *= detail_mask;
            float detail_contrib = detail * detail_scale + detail_hi * 0.04f * mountain_boost;

            // Mountain-zone tight deformation: in mountain belts, tectonic compression
            // creates closely-spaced thrust faults and tight folds at scales much
            // shorter than the broad tectonic pattern. This gives stream-power erosion
            // features to carve into — without it, mountains are smooth plateaus.
            // NOT gated by province_mask: the ENTIRE mountain belt has tight folds,
            // not just near plate boundaries. Province mask is for craton suppression
            // (flatlands), not mountain belts.
            if (mountain_boost > 0.05f) {
                // Tectonic compression creates multi-scale deformation in mountain belts:
                // - Major thrust sheets: ~17-tile wavelength, high amplitude (main ridges)
                // - Secondary folds: ~8-tile wavelength, moderate amplitude
                // - Fracture zones: ~4-tile wavelength, creates local ravines
                // - Micro-relief: ~2-3 tile wavelength, eliminates flat ridge crests
                float thrust_sheet = meso_ridge_noise.GetNoise(fx * 0.06f, fy * 0.06f);
                float secondary = meso_ridge_noise.GetNoise(fx * 0.12f, fy * 0.12f);
                float fracture = detail_noise.GetNoise(fx * env.noise_scale * 6.0f,
                                                        fy * env.noise_scale * 6.0f);
                // Micro-relief: very high frequency noise at 2-3 tile scale ensures
                // no flat areas remain on ridge crests or valley floors. Models
                // outcrop-scale variation: boulders, small cliffs, talus chutes.
                // Uses a different noise to avoid correlation with other scales.
                float micro1 = detail_noise.GetNoise(fx * 0.35f, fy * 0.35f);
                float micro2 = detail_noise.GetNoise(fx * 0.50f, fy * 0.45f);

                float mtn_detail = thrust_sheet * 0.20f   // major ridges/valleys
                                 + secondary * 0.16f      // secondary folds
                                 + fracture * 0.10f       // fracture zones
                                 + (micro1 + micro2) * 0.04f;  // micro-relief
                detail_contrib += mtn_detail * mountain_boost;
            }

            // No high-frequency coastline noise — coastlines emerge naturally
            // from the continuous continental field + broad warping

            // ── Edge falloff: push map borders toward ocean ─────────────
            float ex = (fx / fw) * 2.0f - 1.0f;
            float ey = (fy / fh) * 2.0f - 1.0f;
            float edge_dist = std::max(std::abs(ex), std::abs(ey));
            float edge_falloff = std::clamp((edge_dist - 0.70f) / 0.30f, 0.0f, 1.0f);

            float final_h = base + boundary_uplift + detail_contrib - edge_falloff * 0.4f;
            height_field[idx] = std::clamp(final_h, 0.0f, 1.0f);
            ridge_field[idx] = std::clamp(rv, 0.0f, 1.0f);

        }
    }

    // ── 3b. Global relief domain warping ────────────────────────────────
    // Aggressively bend straight plate boundary artifacts into natural curves.
    // Three scales: broad continental bends, medium ridge curvature, fine
    // breakup of any remaining geometric/linear patterns. Stronger in
    // high-relief areas where Voronoi edge artifacts are most visible.
    {
        FastNoiseLite relief_warp_x, relief_warp_y;
        relief_warp_x.SetSeed(static_cast<int>(seed + 25000));
        relief_warp_x.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        relief_warp_x.SetFractalType(FastNoiseLite::FractalType_FBm);
        relief_warp_x.SetFractalOctaves(3);
        relief_warp_x.SetFrequency(0.005f);  // broad bends
        relief_warp_y.SetSeed(static_cast<int>(seed + 25001));
        relief_warp_y.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        relief_warp_y.SetFractalType(FastNoiseLite::FractalType_FBm);
        relief_warp_y.SetFractalOctaves(3);
        relief_warp_y.SetFrequency(0.005f);

        FastNoiseLite relief_warp2_x, relief_warp2_y;
        relief_warp2_x.SetSeed(static_cast<int>(seed + 25010));
        relief_warp2_x.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        relief_warp2_x.SetFractalType(FastNoiseLite::FractalType_FBm);
        relief_warp2_x.SetFractalOctaves(2);
        relief_warp2_x.SetFrequency(0.015f);  // medium bends
        relief_warp2_y.SetSeed(static_cast<int>(seed + 25011));
        relief_warp2_y.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        relief_warp2_y.SetFractalType(FastNoiseLite::FractalType_FBm);
        relief_warp2_y.SetFractalOctaves(2);
        relief_warp2_y.SetFrequency(0.015f);

        // Third layer: fine breakup of geometric plate junction artifacts
        FastNoiseLite relief_warp3_x, relief_warp3_y;
        relief_warp3_x.SetSeed(static_cast<int>(seed + 25020));
        relief_warp3_x.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        relief_warp3_x.SetFractalType(FastNoiseLite::FractalType_FBm);
        relief_warp3_x.SetFractalOctaves(2);
        relief_warp3_x.SetFrequency(0.035f);  // fine-scale
        relief_warp3_y.SetSeed(static_cast<int>(seed + 25021));
        relief_warp3_y.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        relief_warp3_y.SetFractalType(FastNoiseLite::FractalType_FBm);
        relief_warp3_y.SetFractalOctaves(2);
        relief_warp3_y.SetFrequency(0.035f);

        std::vector<float> warped_h(height_field);
        for (uint32_t y = 3; y < h - 3; ++y) {
            for (uint32_t x = 3; x < w - 3; ++x) {
                size_t idx = static_cast<size_t>(y) * w + x;
                float fx = static_cast<float>(x);
                float fy = static_cast<float>(y);

                float land_margin = height_field[idx] - env.water_level;
                if (land_margin < 0.02f)
                    continue;
                // Stronger warping in high-relief areas where plate geometry is most visible
                float strength = std::clamp(land_margin / 0.18f, 0.0f, 1.0f);

                // Three-scale displacement (much stronger than before)
                float dx1 = relief_warp_x.GetNoise(fx, fy) * 12.0f;  // broad: up to 12 tiles
                float dy1 = relief_warp_y.GetNoise(fx, fy) * 12.0f;
                float dx2 = relief_warp2_x.GetNoise(fx, fy) * 5.0f;  // medium: up to 5 tiles
                float dy2 = relief_warp2_y.GetNoise(fx, fy) * 5.0f;
                float dx3 = relief_warp3_x.GetNoise(fx, fy) * 2.0f;  // fine: up to 2 tiles
                float dy3 = relief_warp3_y.GetNoise(fx, fy) * 2.0f;
                float dx_total = (dx1 + dx2 + dx3) * strength;
                float dy_total = (dy1 + dy2 + dy3) * strength;

                float sx = std::clamp(fx + dx_total, 1.0f, static_cast<float>(w) - 2.0f);
                float sy = std::clamp(fy + dy_total, 1.0f, static_cast<float>(h) - 2.0f);
                auto ix = static_cast<uint32_t>(sx);
                auto iy = static_cast<uint32_t>(sy);
                float bx = sx - static_cast<float>(ix);
                float by = sy - static_cast<float>(iy);
                size_t i00 = static_cast<size_t>(iy) * w + ix;
                float sampled = height_field[i00] * (1 - bx) * (1 - by) +
                                height_field[i00 + 1] * bx * (1 - by) +
                                height_field[i00 + w] * (1 - bx) * by +
                                height_field[i00 + w + 1] * bx * by;

                // Full-strength blend in mountains (no 0.85 cap)
                warped_h[idx] =
                    height_field[idx] * (1.0f - strength * 0.92f) + sampled * strength * 0.92f;
            }
        }
        std::copy(warped_h.begin(), warped_h.end(), height_field.begin());
    }

    // ── 4. Erosion pass: thermal erosion + downhill smoothing ────────────
    // Few iterations — this is just pre-conditioning for stream-power.
    // Mountain rock (metamorphic/granite) supports steeper angles than
    // lowland sediment, so talus angle is elevation-dependent.
    constexpr int EROSION_ITERS = 2;             // minimal pre-conditioning only
    constexpr float THERMAL_RATE = 0.08f;        // gentle per-step
    constexpr float MAX_STABLE_LOW = 0.05f;      // lowland talus angle
    constexpr float MAX_STABLE_HIGH = 0.15f;     // mountain rock holds steeper angles

    std::vector<float> eroded(height_field);
    std::vector<float> temp_buf(total);
    std::vector<float> deposit(total, 0.0f);

    for (int iter = 0; iter < EROSION_ITERS; ++iter) {
        // Thermal erosion: move material from steep cells to lower neighbors
        // Two-pass: compute transfers, then deposit to neighbors (conserves material)
        std::fill(deposit.begin(), deposit.end(), 0.0f);
        std::copy(eroded.begin(), eroded.end(), temp_buf.begin());

        for (uint32_t y = 1; y < h - 1; ++y) {
            for (uint32_t x = 1; x < w - 1; ++x) {
                size_t idx = static_cast<size_t>(y) * w + x;
                float center = eroded[idx];

                // Elevation-dependent talus angle: mountain rock holds steeper slopes
                float mtn_frac = std::clamp((center - 0.50f) * 3.5f, 0.0f, 1.0f);
                float talus = MAX_STABLE_LOW + mtn_frac * (MAX_STABLE_HIGH - MAX_STABLE_LOW);

                // Collect lower neighbors with excess slope
                struct LowerNbr {
                    size_t nidx;
                    float excess;
                };
                LowerNbr nbrs[8];
                int n_lower = 0;
                float total_excess = 0.0f;
                float max_diff = 0.0f;

                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dy == 0)
                            continue;
                        size_t nidx = static_cast<size_t>(y + dy) * w + (x + dx);
                        float diff = center - eroded[nidx];
                        if (diff > talus) {
                            nbrs[n_lower++] = {nidx, diff - talus};
                            total_excess += diff - talus;
                            max_diff = std::max(max_diff, diff);
                        }
                    }
                }

                if (total_excess <= 0.0f)
                    continue;

                // High terrain erodes less (rock hardness increases with elevation)
                float hardness = std::clamp((center - 0.5f) * 2.0f, 0.0f, 0.7f);
                float effective_rate = THERMAL_RATE * (1.0f - hardness);
                float transfer = std::min(total_excess * effective_rate, max_diff * 0.5f);

                // Subtract from source
                temp_buf[idx] -= transfer;

                // Deposit proportionally to lower neighbors
                for (int n = 0; n < n_lower; ++n) {
                    float share = nbrs[n].excess / total_excess;
                    deposit[nbrs[n].nidx] += transfer * share;
                }
            }
        }

        // Apply deposits
        for (size_t i = 0; i < total; ++i) {
            eroded[i] = temp_buf[i] + deposit[i];
        }

        // Deposit smoothing: simulates sediment deposition in lowlands.
        // Only applied below mountain elevation — mountain valleys are
        // transport-limited (high gradient clears sediment), not deposition zones.
        for (uint32_t y = 1; y < h - 1; ++y) {
            for (uint32_t x = 1; x < w - 1; ++x) {
                size_t idx = static_cast<size_t>(y) * w + x;
                float elev = eroded[idx];
                // Skip mountains: sediment deposits in lowlands, not steep valleys
                float mtn = std::clamp((elev - 0.50f) * 4.0f, 0.0f, 1.0f);
                float blend = 0.12f * (1.0f - mtn);  // 12% in lowlands, 0% in mountains
                if (blend < 0.01f) continue;
                float sum = eroded[idx] * 4.0f;
                sum += eroded[idx - 1] + eroded[idx + 1];
                sum += eroded[idx - w] + eroded[idx + w];
                float smoothed = sum / 8.0f;
                eroded[idx] = eroded[idx] * (1.0f - blend) + smoothed * blend;
            }
        }
    }

    // ── 4b. Tectonic depressions (grabens) ──────────────────────────────
    // Create inland depressions that become natural lake basins.
    // Placed far from coasts using low-frequency noise to select locations.
    {
        FastNoiseLite graben_noise;
        graben_noise.SetSeed(static_cast<int>(seed + 500));
        graben_noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        graben_noise.SetFrequency(0.006f);  // lower freq = larger depressions

        // Elongated shape noise — different x/y frequencies create rift-like shapes
        FastNoiseLite graben_shape;
        graben_shape.SetSeed(static_cast<int>(seed + 501));
        graben_shape.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        graben_shape.SetFrequency(0.018f);

        // Third noise for irregular edges
        FastNoiseLite graben_edge;
        graben_edge.SetSeed(static_cast<int>(seed + 502));
        graben_edge.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        graben_edge.SetFrequency(0.04f);

        for (uint32_t y = 3; y < h - 3; ++y) {
            for (uint32_t x = 3; x < w - 3; ++x) {
                size_t idx = static_cast<size_t>(y) * w + x;
                float ht = eroded[idx];
                if (ht < env.water_level + 0.10f)
                    continue;  // skip near-coast

                float fx = static_cast<float>(x);
                float fy = static_cast<float>(y);

                // Must be well inland: check 7x7 neighborhood for any water
                float min_nbr = 1.0f;
                bool near_water = false;
                for (int dy = -3; dy <= 3 && !near_water; ++dy) {
                    for (int dx = -3; dx <= 3 && !near_water; ++dx) {
                        auto ny2 = static_cast<uint32_t>(static_cast<int>(y) + dy);
                        auto nx2 = static_cast<uint32_t>(static_cast<int>(x) + dx);
                        if (ny2 < h && nx2 < w) {
                            if (eroded[static_cast<size_t>(ny2) * w + nx2] < env.water_level)
                                near_water = true;
                        }
                    }
                }
                if (near_water)
                    continue;

                // Elongated rift-like depressions: different x/y scales for anisotropy
                float gn = graben_noise.GetNoise(fx * 0.8f, fy * 1.2f);
                float gs = graben_shape.GetNoise(fx * 1.3f, fy * 0.7f);
                float ge = graben_edge.GetNoise(fx, fy);

                // Depression forms where noise fields align + irregular edges
                if (gn > 0.15f && gs > 0.05f) {
                    float raw = (gn - 0.15f) * (gs - 0.05f) * 15.0f;
                    // Edge noise creates irregular boundaries
                    float edge_mod = 0.6f + 0.4f * ge;
                    float strength = raw * edge_mod;
                    strength = std::clamp(strength, 0.0f, 0.12f);
                    eroded[idx] -= strength;
                }
            }
        }
    }

    // ── 4c. Flow-guided plateau dissection ──────────────────────────────
    // Flat high-elevation surfaces dissect through drainage-network erosion:
    // water accumulates in nascent channels, carving valleys while leaving
    // interfluves as ridges. Physical basis: periglacial frost weathering
    // + glacial cirque carving + fluvial headwater incision. The key insight
    // is that dissection follows drainage patterns (dendritic), NOT elevation
    // contours. We compute D8 flow accumulation each iteration and weight
    // incision by (flatness × elevation × flow_accumulation), producing
    // branching valley networks that break plateaus into ridge-and-valley
    // topography instead of elevation-banded contour artifacts.
    {
        // Multi-scale dissection noise guides WHERE valleys form within
        // the drainage network (lithologic variation in rock weakness).
        FastNoiseLite dissect_coarse;
        dissect_coarse.SetSeed(static_cast<int>(seed + 31000));
        dissect_coarse.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
        dissect_coarse.SetFractalType(FastNoiseLite::FractalType_Ridged);
        dissect_coarse.SetFractalOctaves(3);
        dissect_coarse.SetFrequency(0.08f);  // ~12-tile valleys

        FastNoiseLite dissect_fine;
        dissect_fine.SetSeed(static_cast<int>(seed + 31001));
        dissect_fine.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
        dissect_fine.SetFractalType(FastNoiseLite::FractalType_Ridged);
        dissect_fine.SetFractalOctaves(2);
        dissect_fine.SetFrequency(0.20f);  // ~5-tile cirques

        constexpr float DISSECT_MAX = 0.14f;       // max incision depth (channels)
        constexpr float FLAT_THRESHOLD = 0.018f;    // gradient below this = "flat"
        constexpr float FLOW_SCALE = 40.0f;         // A0 normalization for flow weighting
        constexpr int DISSECT_ITERS = 4;

        std::vector<size_t> d_flow_to(total);
        std::vector<float> d_flow(total);
        std::vector<size_t> d_order(total);

        for (int di = 0; di < DISSECT_ITERS; ++di) {
            // D8 flow directions on current surface
            std::fill(d_flow_to.begin(), d_flow_to.end(), SIZE_MAX);
            for (uint32_t y = 1; y + 1 < h; ++y) {
                for (uint32_t x = 1; x + 1 < w; ++x) {
                    size_t idx = static_cast<size_t>(y) * w + x;
                    float center = eroded[idx];
                    float steepest = 0.0f;
                    for (int dy = -1; dy <= 1; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            if (dx == 0 && dy == 0) continue;
                            size_t nidx = static_cast<size_t>(y + dy) * w + (x + dx);
                            float dist = (dx != 0 && dy != 0) ? 1.414f : 1.0f;
                            float sl = (center - eroded[nidx]) / dist;
                            if (sl > steepest) {
                                steepest = sl;
                                d_flow_to[idx] = nidx;
                            }
                        }
                    }
                }
            }

            // Flow accumulation (highest to lowest)
            std::fill(d_flow.begin(), d_flow.end(), 1.0f);
            std::iota(d_order.begin(), d_order.end(), 0u);
            std::sort(d_order.begin(), d_order.end(),
                      [&](size_t a, size_t b) { return eroded[a] > eroded[b]; });
            for (size_t idx : d_order) {
                if (d_flow_to[idx] != SIZE_MAX)
                    d_flow[d_flow_to[idx]] += d_flow[idx];
            }

            // Compute local gradient
            std::vector<float> local_grad(total, 0.0f);
            for (uint32_t y = 1; y + 1 < h; ++y) {
                for (uint32_t x = 1; x + 1 < w; ++x) {
                    size_t idx = static_cast<size_t>(y) * w + x;
                    float ex = eroded[idx + 1] - eroded[idx - 1];
                    float ey = eroded[idx + w] - eroded[idx - w];
                    local_grad[idx] = std::sqrt(ex * ex + ey * ey) * 0.5f;
                }
            }

            for (uint32_t y = 1; y + 1 < h; ++y) {
                for (uint32_t x = 1; x + 1 < w; ++x) {
                    size_t idx = static_cast<size_t>(y) * w + x;
                    if (eroded[idx] < env.water_level + 0.05f) continue;

                    float fx = static_cast<float>(x);
                    float fy = static_cast<float>(y);

                    // Flatness factor
                    float flatness = std::clamp(1.0f - local_grad[idx] / FLAT_THRESHOLD, 0.0f, 1.0f);
                    if (flatness < 0.05f) continue;

                    // Elevation factor
                    float elev_above = eroded[idx] - env.water_level;
                    float elev_factor = std::clamp(elev_above / 0.25f, 0.0f, 1.0f);

                    // Flow factor: channels erode MUCH more than divides (dendritic pattern).
                    // Using log1p for gradual hierarchy: A=1→0, A=50→0.56, A=200→0.76, A=1000→1.0
                    // Low floor (0.08) keeps interfluves mostly intact while channels carve deep.
                    float flow_factor = std::clamp(std::log1p(d_flow[idx]) / std::log1p(FLOW_SCALE * 20.0f), 0.0f, 1.0f);
                    flow_factor = 0.08f + 0.92f * flow_factor;

                    // Noise-guided incision (lithologic weakness patterns)
                    float vc = dissect_coarse.GetNoise(fx, fy);
                    float vf = dissect_fine.GetNoise(fx, fy);
                    float incision = vc * 0.55f + vf * 0.45f;

                    // Scale by all factors
                    incision *= flatness * elev_factor * flow_factor * DISSECT_MAX / DISSECT_ITERS;
                    eroded[idx] -= incision;
                }
            }
        }
    }

    // ── 5a. Stream-power erosion ──────────────────────────────────────────
    // E = K * A^m * S^n (detachment-limited stream power law)
    // After differential weathering has created initial relief structure,
    // stream-power exploits these pre-existing valleys to carve deep gorges.
    // Physical basis: Himalayas incision ~4mm/yr, Colorado Plateau ~0.3mm/yr.
    {
        constexpr int SPL_ITERS = 60;
        constexpr float K_FLUVIAL = 0.0007f;          // erodibility coefficient (slightly lower to compensate higher m)
        constexpr float M_EXP = 0.6f;                 // drainage area exponent (higher = trunk rivers dominate)
        constexpr float N_EXP = 1.0f;                 // slope exponent
        constexpr float K_DEPOSIT = 0.3f;             // deposition rate
        constexpr float K_THERMAL = 0.02f;            // landslide threshold rate
        constexpr float TALUS_SLOPE_LOW = 0.04f;      // lowland talus angle
        constexpr float TALUS_SLOPE_HIGH = 0.14f;     // hard rock stability limit
        constexpr float MAX_INCISION = 0.008f;        // per-step incision cap
        constexpr float SED_CAPACITY_COEFF = 0.01f;   // transport capacity coefficient

        // Pre-allocate buffers
        std::vector<size_t> flow_to(total);
        std::vector<float> flow(total);
        std::vector<size_t> order(total);
        std::vector<float> sed_load(total, 0.0f);

        // Heterogeneous erodibility noise: models variable lithology within
        // mountain belts (alternating hard granites and soft schists/limestones).
        // This creates differential erosion — resistant ridges persist while
        // weak zones erode into deep valleys, producing the steep relief that
        // uniform-erodibility models cannot achieve.
        FastNoiseLite erod_noise;
        erod_noise.SetSeed(static_cast<int>(seed + 30000));
        erod_noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        erod_noise.SetFrequency(0.015f);  // ~65-tile provinces of hard/soft rock
        erod_noise.SetFractalType(FastNoiseLite::FractalType_FBm);
        erod_noise.SetFractalOctaves(2);
        FastNoiseLite erod_fine;
        erod_fine.SetSeed(static_cast<int>(seed + 30001));
        erod_fine.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        erod_fine.SetFrequency(0.06f);  // ~16-tile local variation (bed-scale)

        for (int iter = 0; iter < SPL_ITERS; ++iter) {
            // D8 flow directions
            std::fill(flow_to.begin(), flow_to.end(), SIZE_MAX);
            for (uint32_t y = 1; y < h - 1; ++y) {
                for (uint32_t x = 1; x < w - 1; ++x) {
                    size_t idx = static_cast<size_t>(y) * w + x;
                    float center = eroded[idx];
                    float steepest = 0.0f;
                    size_t best = SIZE_MAX;
                    for (int dy = -1; dy <= 1; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            if (dx == 0 && dy == 0)
                                continue;
                            size_t nidx = static_cast<size_t>(y + dy) * w + (x + dx);
                            float dist = (dx != 0 && dy != 0) ? 1.414f : 1.0f;
                            float sl = (center - eroded[nidx]) / dist;
                            if (sl > steepest) {
                                steepest = sl;
                                best = nidx;
                            }
                        }
                    }
                    flow_to[idx] = best;
                }
            }

            // Flow accumulation (highest to lowest)
            std::fill(flow.begin(), flow.end(), 1.0f);
            std::iota(order.begin(), order.end(), 0u);
            std::sort(order.begin(), order.end(),
                      [&](size_t a, size_t b) { return eroded[a] > eroded[b]; });
            for (size_t idx : order) {
                if (flow_to[idx] != SIZE_MAX)
                    flow[flow_to[idx]] += flow[idx];
            }

            // Process cells in downstream order (highest to lowest)
            // This ensures sediment from upstream arrives before we process downstream
            std::copy(eroded.begin(), eroded.end(), temp_buf.begin());

            for (size_t idx : order) {
                auto x = static_cast<uint32_t>(idx % w);
                auto y = static_cast<uint32_t>(idx / w);
                if (x == 0 || x >= w - 1 || y == 0 || y >= h - 1)
                    continue;
                if (eroded[idx] < env.water_level)
                    continue;

                // Stream-power incision: E = K * A^m * S^n
                size_t ds = flow_to[idx];
                float slope_val = 0.0f;
                if (ds != SIZE_MAX) {
                    auto dsx = static_cast<uint32_t>(ds % w);
                    auto dsy = static_cast<uint32_t>(ds / w);
                    float dist = (std::abs(static_cast<int>(dsx) - static_cast<int>(x)) +
                                      std::abs(static_cast<int>(dsy) - static_cast<int>(y)) >
                                  1)
                                     ? 1.414f
                                     : 1.0f;
                    slope_val = std::max(0.0f, (eroded[idx] - eroded[ds]) / dist);
                }
                float area_term = std::pow(flow[idx], M_EXP);

                // Mountain erosion boost: steeper terrain + more precipitation =
                // dramatically stronger fluvial incision. Rivers in the Himalayas
                // incise 2-10mm/yr vs 0.01mm/yr on cratons — 100-1000x difference.
                float mtn_boost_e = std::clamp((eroded[idx] - 0.55f) * 3.0f, 0.0f, 1.0f);
                // Major river boost: antecedent rivers (large catchments established
                // before uplift) erode faster because they concentrate massive discharge
                // through narrow gorges. The Colorado River incises ~0.3mm/yr through
                // the rising Colorado Plateau; Kosi ~4mm/yr through the Himalayas.
                // Flow > 50 = major river; flow > 200 = master drainage.
                float river_boost = std::clamp((flow[idx] - 50.0f) / 150.0f, 0.0f, 1.0f);
                float eff_k = K_FLUVIAL * (1.0f + mtn_boost_e * 2.0f + river_boost * 3.0f);
                float eff_max = MAX_INCISION * (1.0f + mtn_boost_e * 1.5f + river_boost * 2.0f);
                float erosion = eff_k * area_term * std::pow(slope_val, N_EXP);

                // Heterogeneous rock resistance: combines tectonic ridge hardness with
                // stochastic lithology variation. Hard zones resist, soft zones erode.
                // Wider contrast range creates visible differential erosion:
                // soft shale/limestone zones erode into broad valleys,
                // hard granite/metamorphic ridges persist as resistant hogbacks.
                float fx_e = static_cast<float>(idx % w);
                float fy_e = static_cast<float>(idx / w);
                float tectonic_hard = ridge_field[idx] * 0.35f;
                float litho_hard = (erod_noise.GetNoise(fx_e, fy_e) * 0.5f + 0.5f) * 0.30f;
                float local_hard = (erod_fine.GetNoise(fx_e, fy_e) * 0.5f + 0.5f) * 0.15f;
                float hard_rock = std::clamp(tectonic_hard + litho_hard + local_hard, 0.0f, 0.65f);
                // Soft rock erodes up to ~2.8× harder than hard rock (0.35 vs 1.0)
                erosion *= (1.0f - hard_rock);
                erosion = std::min(erosion, eff_max);

                // Add eroded material to sediment load
                sed_load[idx] += erosion;
                temp_buf[idx] -= erosion;

                // Sediment capacity: proportional to flow * slope
                float capacity = SED_CAPACITY_COEFF * flow[idx] * slope_val;

                // If sediment exceeds capacity, deposit the excess (creates floodplains)
                if (sed_load[idx] > capacity) {
                    float deposit = (sed_load[idx] - capacity) * K_DEPOSIT;
                    deposit = std::min(deposit, sed_load[idx]);
                    temp_buf[idx] += deposit;
                    sed_load[idx] -= deposit;
                }

                // Pass remaining sediment downstream
                if (ds != SIZE_MAX) {
                    sed_load[ds] += sed_load[idx];
                    sed_load[idx] = 0.0f;
                }

                // Landslide threshold: ONLY moves material where slope exceeds
                // hard rock stability limit. Unlike continuous thermal creep,
                // this preserves steep-but-stable valley walls and ridge flanks.
                // Only triggers in lowlands where unconsolidated sediment fails.
                float center_h = eroded[idx];
                float mtn_frac = std::clamp((center_h - 0.50f) * 4.0f, 0.0f, 1.0f);
                if (mtn_frac < 0.5f) {  // landslide only in lowlands/foothills
                    float talus = TALUS_SLOPE_LOW;
                    float total_excess = 0.0f;
                    struct TNbr {
                        size_t nidx;
                        float excess;
                    };
                    TNbr tnbrs[8];
                    int n_lower = 0;
                    for (int dy = -1; dy <= 1; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            if (dx == 0 && dy == 0)
                                continue;
                            size_t nidx = static_cast<size_t>(y + dy) * w + (x + dx);
                            float diff = center_h - eroded[nidx];
                            if (diff > talus) {
                                tnbrs[n_lower++] = {nidx, diff - talus};
                                total_excess += diff - talus;
                            }
                        }
                    }
                    if (total_excess > 0.0f) {
                        float transfer = std::min(total_excess * K_THERMAL, 0.003f);
                        temp_buf[idx] -= transfer;
                        for (int n = 0; n < n_lower; ++n) {
                            temp_buf[tnbrs[n].nidx] += transfer * (tnbrs[n].excess / total_excess);
                        }
                    }
                }
            }

            std::swap(eroded, temp_buf);
        }
    }

    // ── 5a-ii. Micro-catchment incision for interior plateaus ────────────
    // Post-SPE pass targeting only flat high-elevation surfaces that the main
    // stream-power erosion missed (because they have low slope → low S^n term).
    // Uses higher m exponent (0.85) to strongly favor flow accumulation networks,
    // creating dendritic dissection on otherwise featureless plateaus.
    // Gated by slope < threshold AND elevation > threshold to avoid touching
    // already-incised terrain or lowlands.
    {
        constexpr float MICRO_K = 0.00025f;    // erodibility (gentle)
        constexpr float MICRO_M = 0.85f;       // high area exponent → strong hierarchy
        constexpr float MICRO_N = 1.0f;
        constexpr float SLOPE_GATE = 0.04f;    // only affect low-slope areas
        constexpr float ELEV_GATE = 0.55f;     // only affect highlands
        constexpr float MAX_MICRO_INCISION = 0.004f;
        constexpr int MICRO_ITERS = 15;

        std::vector<size_t> mflow_to(total);
        std::vector<float> mflow(total);
        std::vector<size_t> morder(total);

        for (int mi = 0; mi < MICRO_ITERS; ++mi) {
            // D8 flow directions
            std::fill(mflow_to.begin(), mflow_to.end(), SIZE_MAX);
            for (uint32_t y = 1; y < h - 1; ++y) {
                for (uint32_t x = 1; x < w - 1; ++x) {
                    size_t idx = static_cast<size_t>(y) * w + x;
                    float center = eroded[idx];
                    float steepest = 0.0f;
                    for (int dy = -1; dy <= 1; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            if (dx == 0 && dy == 0) continue;
                            size_t nidx = static_cast<size_t>(y + dy) * w + (x + dx);
                            float dist = (dx != 0 && dy != 0) ? 1.414f : 1.0f;
                            float sl = (center - eroded[nidx]) / dist;
                            if (sl > steepest) { steepest = sl; mflow_to[idx] = nidx; }
                        }
                    }
                }
            }
            // Flow accumulation
            std::fill(mflow.begin(), mflow.end(), 1.0f);
            std::iota(morder.begin(), morder.end(), 0u);
            std::sort(morder.begin(), morder.end(),
                      [&](size_t a, size_t b) { return eroded[a] > eroded[b]; });
            for (size_t idx : morder) {
                if (mflow_to[idx] != SIZE_MAX) mflow[mflow_to[idx]] += mflow[idx];
            }

            // Incise only flat highlands
            std::copy(eroded.begin(), eroded.end(), temp_buf.begin());
            for (size_t idx : morder) {
                auto x = static_cast<uint32_t>(idx % w);
                auto y = static_cast<uint32_t>(idx / w);
                if (x == 0 || x >= w - 1 || y == 0 || y >= h - 1) continue;
                if (eroded[idx] < env.water_level + ELEV_GATE - env.water_level) continue;
                // Compute local slope
                float local_grad = 0.0f;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dy == 0) continue;
                        float diff = std::abs(eroded[idx] -
                            eroded[static_cast<size_t>(y + dy) * w + (x + dx)]);
                        local_grad = std::max(local_grad, diff);
                    }
                }
                // Gate: only dissect flat highlands
                float slope_factor = std::clamp(1.0f - local_grad / SLOPE_GATE, 0.0f, 1.0f);
                float elev_factor = std::clamp((eroded[idx] - ELEV_GATE) / 0.15f, 0.0f, 1.0f);
                if (slope_factor < 0.05f || elev_factor < 0.05f) continue;

                size_t ds = mflow_to[idx];
                float ds_slope = 0.0f;
                if (ds != SIZE_MAX) {
                    auto dsx = static_cast<uint32_t>(ds % w);
                    auto dsy = static_cast<uint32_t>(ds / w);
                    float dist = (std::abs(static_cast<int>(dsx) - static_cast<int>(x)) +
                                  std::abs(static_cast<int>(dsy) - static_cast<int>(y)) > 1)
                                     ? 1.414f : 1.0f;
                    ds_slope = std::max(0.0f, (eroded[idx] - eroded[ds]) / dist);
                }
                float erosion = MICRO_K * std::pow(mflow[idx], MICRO_M)
                              * std::pow(ds_slope, MICRO_N)
                              * slope_factor * elev_factor;
                erosion = std::min(erosion, MAX_MICRO_INCISION);
                temp_buf[idx] -= erosion;
            }
            std::swap(eroded, temp_buf);
        }
    }

    // ── 5b. Valley-floor smoothing ───────────────────────────────────────
    // High-flow areas get smoothed to create visible floodplains.
    // Uses the final flow accumulation from the erosion pass.
    {
        // Compute final D8 flow for smoothing
        std::vector<size_t> vflow_to(total, SIZE_MAX);
        for (uint32_t y = 1; y < h - 1; ++y) {
            for (uint32_t x = 1; x < w - 1; ++x) {
                size_t idx = static_cast<size_t>(y) * w + x;
                float center = eroded[idx];
                float steepest = 0.0f;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dy == 0)
                            continue;
                        size_t nidx = static_cast<size_t>(y + dy) * w + (x + dx);
                        float sl = center - eroded[nidx];
                        if (sl > steepest) {
                            steepest = sl;
                            vflow_to[idx] = nidx;
                        }
                    }
                }
            }
        }
        std::vector<float> vflow(total, 1.0f);
        std::vector<size_t> vorder(total);
        std::iota(vorder.begin(), vorder.end(), 0u);
        std::sort(vorder.begin(), vorder.end(),
                  [&](size_t a, size_t b) { return eroded[a] > eroded[b]; });
        for (size_t idx : vorder) {
            if (vflow_to[idx] != SIZE_MAX) {
                vflow[vflow_to[idx]] += vflow[idx];
            }
        }

        // Smooth ONLY true depositional zones: high flow AND low slope.
        // Geologically: sediment deposits in low-gradient floodplains and basins,
        // NOT in steep mountain valleys where the stream has high transport capacity.
        // Previous version smoothed all high-flow cells, destroying mountain valley walls.
        constexpr float VALLEY_SMOOTH_THRESHOLD = 200.0f;  // higher threshold
        constexpr float VALLEY_SLOPE_MAX = 0.025f;  // only smooth where slope < this
        std::copy(eroded.begin(), eroded.end(), temp_buf.begin());
        for (uint32_t vy = 1; vy < h - 1; ++vy) {
            for (uint32_t vx = 1; vx < w - 1; ++vx) {
                size_t vidx = static_cast<size_t>(vy) * w + vx;
                if (vflow[vidx] < VALLEY_SMOOTH_THRESHOLD || eroded[vidx] < env.water_level)
                    continue;
                // Compute local slope
                float max_slope = 0.0f;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dy == 0) continue;
                        float diff = std::abs(eroded[vidx] -
                            eroded[static_cast<size_t>(vy + dy) * w + (vx + dx)]);
                        max_slope = std::max(max_slope, diff);
                    }
                }
                // Only smooth low-gradient depositional zones
                if (max_slope > VALLEY_SLOPE_MAX) continue;

                float smooth_t =
                    std::clamp((vflow[vidx] - VALLEY_SMOOTH_THRESHOLD) / 400.0f, 0.0f, 0.5f);
                float sum = 0.0f;
                int cnt = 0;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        sum += eroded[static_cast<size_t>(vy + dy) * w + (vx + dx)];
                        ++cnt;
                    }
                }
                temp_buf[vidx] = eroded[vidx] * (1.0f - smooth_t) + (sum / cnt) * smooth_t;
            }
        }
        std::swap(eroded, temp_buf);
    }

    // ── 5c. River valley carving ─────────────────────────────────────────
    // Incise subtle valleys along high-flow paths so rivers look integrated
    // with terrain rather than overlaid. Depth proportional to log(flow).
    {
        // Compute final flow accumulation for carving
        std::vector<size_t> carve_flow_to(total, SIZE_MAX);
        std::vector<float> carve_flow(total, 1.0f);
        for (uint32_t y = 1; y < h - 1; ++y) {
            for (uint32_t x = 1; x < w - 1; ++x) {
                size_t idx = static_cast<size_t>(y) * w + x;
                if (eroded[idx] < env.water_level)
                    continue;
                float center = eroded[idx];
                float steepest = 0.0f;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (!dx && !dy)
                            continue;
                        size_t nidx = static_cast<size_t>(y + dy) * w + (x + dx);
                        float sl = center - eroded[nidx];
                        if (sl > steepest) {
                            steepest = sl;
                            carve_flow_to[idx] = nidx;
                        }
                    }
                }
            }
        }
        std::vector<size_t> carve_order(total);
        std::iota(carve_order.begin(), carve_order.end(), 0u);
        std::sort(carve_order.begin(), carve_order.end(),
                  [&](size_t a, size_t b) { return eroded[a] > eroded[b]; });
        for (size_t idx : carve_order) {
            if (carve_flow_to[idx] != SIZE_MAX)
                carve_flow[carve_flow_to[idx]] += carve_flow[idx];
        }

        // Carve valleys: depth = K * log(1 + flow), capped.
        // Major rivers need visually distinct valleys at this resolution.
        // Also widen valleys for large rivers by lowering neighbors.
        constexpr float K_VALLEY_CARVE = 0.008f;
        constexpr float MIN_CARVE_FLOW = 20.0f;
        constexpr float WIDEN_FLOW = 150.0f;  // flow threshold for valley widening
        std::copy(eroded.begin(), eroded.end(), temp_buf.begin());
        for (size_t idx = 0; idx < total; ++idx) {
            if (eroded[idx] < env.water_level)
                continue;
            if (carve_flow[idx] < MIN_CARVE_FLOW)
                continue;
            float carve_depth = K_VALLEY_CARVE * std::log(1.0f + carve_flow[idx] - MIN_CARVE_FLOW);
            // Deeper valleys in high-uplift areas (mountains) for canyons
            float elev_boost = std::clamp((eroded[idx] - 0.55f) * 3.0f, 0.0f, 1.0f);
            carve_depth *= (1.0f + elev_boost * 1.2f);
            carve_depth = std::min(carve_depth, 0.07f);
            temp_buf[idx] -= carve_depth;

            // Valley widening: major rivers create floodplains by lowering
            // adjacent tiles. Width proportional to log(flow).
            if (carve_flow[idx] > WIDEN_FLOW) {
                float widen_depth = carve_depth * 0.4f;  // neighbors lowered less
                auto xi = static_cast<uint32_t>(idx % w);
                auto yi = static_cast<uint32_t>(idx / w);
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dy == 0) continue;
                        auto nx = static_cast<uint32_t>(static_cast<int>(xi) + dx);
                        auto ny = static_cast<uint32_t>(static_cast<int>(yi) + dy);
                        if (nx >= w || ny >= h) continue;
                        size_t nidx = static_cast<size_t>(ny) * w + nx;
                        if (eroded[nidx] < env.water_level) continue;
                        // Only widen if neighbor is higher (don't fill valleys)
                        if (eroded[nidx] > eroded[idx]) {
                            float nbr_lower = std::min(widen_depth, eroded[nidx] - eroded[idx]);
                            temp_buf[nidx] = std::min(temp_buf[nidx], eroded[nidx] - nbr_lower);
                        }
                    }
                }
            }
        }
        std::swap(eroded, temp_buf);
    }

    // ── 5d. Valley drowning — rias, estuaries, fjords ───────────────────
    // Selectively lower valley floors near the coast below water level to create
    // recognizable drowned-valley inlets (rias), estuaries, and fjord-like features.
    // Uses flow accumulation to identify valleys worth drowning.
    {
        // Recompute flow accumulation (we need it for drowning decisions)
        std::vector<size_t> drown_flow_to(total, SIZE_MAX);
        std::vector<float> drown_flow(total, 1.0f);
        for (uint32_t y2 = 1; y2 < h - 1; ++y2) {
            for (uint32_t x2 = 1; x2 < w - 1; ++x2) {
                size_t idx = static_cast<size_t>(y2) * w + x2;
                if (eroded[idx] < env.water_level)
                    continue;
                float center = eroded[idx];
                float steepest = 0.0f;
                for (int dy2 = -1; dy2 <= 1; ++dy2) {
                    for (int dx2 = -1; dx2 <= 1; ++dx2) {
                        if (!dx2 && !dy2)
                            continue;
                        size_t nidx = static_cast<size_t>(y2 + dy2) * w + (x2 + dx2);
                        float sl = center - eroded[nidx];
                        if (sl > steepest) {
                            steepest = sl;
                            drown_flow_to[idx] = nidx;
                        }
                    }
                }
            }
        }
        // Accumulate flow in elevation-descending order
        std::vector<size_t> drown_order(total);
        std::iota(drown_order.begin(), drown_order.end(), 0u);
        std::sort(drown_order.begin(), drown_order.end(),
                  [&](size_t a, size_t b) { return eroded[a] > eroded[b]; });
        for (size_t idx : drown_order) {
            if (drown_flow_to[idx] != SIZE_MAX)
                drown_flow[drown_flow_to[idx]] += drown_flow[idx];
        }

        // BFS distance from ocean (not lakes) for drowning scope
        std::vector<float> drown_dist(total, 0.0f);
        {
            std::deque<size_t> dq;
            std::vector<bool> dv(total, false);
            for (uint32_t y2 = 0; y2 < h; ++y2) {
                for (uint32_t x2 = 0; x2 < w; ++x2) {
                    size_t idx = static_cast<size_t>(y2) * w + x2;
                    if (eroded[idx] < env.water_level) {
                        drown_dist[idx] = 0.0f;
                        dv[idx] = true;
                        dq.push_back(idx);
                    }
                }
            }
            while (!dq.empty()) {
                size_t idx = dq.front();
                dq.pop_front();
                auto cx2 = static_cast<uint32_t>(idx % w);
                auto cy2 = static_cast<uint32_t>(idx / w);
                for (int dy2 = -1; dy2 <= 1; ++dy2) {
                    for (int dx2 = -1; dx2 <= 1; ++dx2) {
                        if (!dx2 && !dy2)
                            continue;
                        int nx = static_cast<int>(cx2) + dx2;
                        int ny = static_cast<int>(cy2) + dy2;
                        if (nx < 0 || nx >= static_cast<int>(w) || ny < 0 ||
                            ny >= static_cast<int>(h))
                            continue;
                        size_t nidx = static_cast<size_t>(ny) * w + static_cast<size_t>(nx);
                        if (dv[nidx])
                            continue;
                        dv[nidx] = true;
                        drown_dist[nidx] = drown_dist[idx] + 1.0f;
                        dq.push_back(nidx);
                    }
                }
            }
        }

        // Regional sea-level anomaly: low-freq noise ±0.025 height units
        // Prevents uniform shoreline and creates partial drowning variation
        FastNoiseLite sealevel_anom;
        sealevel_anom.SetSeed(static_cast<int>(seed + 30000));
        sealevel_anom.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        sealevel_anom.SetFrequency(0.008f);
        sealevel_anom.SetFractalType(FastNoiseLite::FractalType_FBm);
        sealevel_anom.SetFractalOctaves(2);

        // Drowning parameters — lower threshold to include tributaries (dendritic rias)
        constexpr float DROWN_MAX_DIST = 50.0f;     // max tiles from coast to affect
        constexpr float DROWN_FLOW_THRESH = 40.0f;  // include tributaries for branching inlets
        constexpr float DROWN_DEPTH = 0.05f;        // max depth below water level for rias
        constexpr float DROWN_ELEV_MARGIN = 0.10f;  // wider margin to catch more valley floors

        // Fjord parameters: deeper, narrower, in mountainous cold areas
        constexpr float FJORD_RELIEF_THRESH = 0.15f;  // local relief threshold for fjord mode
        constexpr float FJORD_DEPTH = 0.07f;          // deeper carving for fjords
        constexpr float FJORD_LAT_BIAS = 0.3f;        // latitude bias (0=equator, 1=pole)

        // Compute local relief (R=6 neighborhood) for fjord detection
        std::vector<float> local_relief(total, 0.0f);
        for (uint32_t y2 = 0; y2 < h; ++y2) {
            for (uint32_t x2 = 0; x2 < w; ++x2) {
                size_t idx = static_cast<size_t>(y2) * w + x2;
                float lo = eroded[idx], hi = eroded[idx];
                for (int ry = -6; ry <= 6; ++ry) {
                    for (int rx = -6; rx <= 6; ++rx) {
                        int ny = static_cast<int>(y2) + ry;
                        int nx = static_cast<int>(x2) + rx;
                        if (nx < 0 || nx >= static_cast<int>(w) || ny < 0 ||
                            ny >= static_cast<int>(h))
                            continue;
                        float e = eroded[static_cast<size_t>(ny) * w + static_cast<size_t>(nx)];
                        lo = std::min(lo, e);
                        hi = std::max(hi, e);
                    }
                }
                local_relief[idx] = hi - lo;
            }
        }

        for (size_t i = 0; i < total; ++i) {
            if (eroded[i] < env.water_level)
                continue;  // already water
            if (drown_dist[i] > DROWN_MAX_DIST)
                continue;  // too far inland

            float flow = drown_flow[i];
            if (flow < DROWN_FLOW_THRESH)
                continue;  // not a significant valley

            float fx2 = static_cast<float>(i % w);
            float fy2 = static_cast<float>(i / w);
            float local_wl = env.water_level + sealevel_anom.GetNoise(fx2, fy2) * 0.025f;

            float elev_above = eroded[i] - local_wl;
            if (elev_above > DROWN_ELEV_MARGIN)
                continue;  // too high to drown

            // Flow-based drowning strength: more flow = more likely to drown
            float flow_factor = std::clamp((flow - DROWN_FLOW_THRESH) / 500.0f, 0.0f, 1.0f);
            // Distance falloff: stronger near coast
            float dist_factor = 1.0f - drown_dist[i] / DROWN_MAX_DIST;
            dist_factor = dist_factor * dist_factor;  // quadratic falloff

            // Determine if fjord or ria
            float relief = local_relief[i];
            float lat_t = fy2 / static_cast<float>(h);
            float lat_cold = 2.0f * std::abs(lat_t - 0.5f);  // 0 at equator, 1 at poles
            bool is_fjord = relief > FJORD_RELIEF_THRESH && lat_cold > FJORD_LAT_BIAS;

            float max_depth = is_fjord ? FJORD_DEPTH : DROWN_DEPTH;
            float carve = max_depth * flow_factor * dist_factor;

            // Lower the valley floor below water level
            float new_elev = local_wl - carve;
            if (new_elev < eroded[i]) {
                eroded[i] = new_elev;
            }

            // Fjords: also widen the valley by pulling adjacent tiles down slightly
            if (is_fjord && carve > 0.01f) {
                auto cx2 = static_cast<uint32_t>(i % w);
                auto cy2 = static_cast<uint32_t>(i / w);
                for (int dy2 = -1; dy2 <= 1; ++dy2) {
                    for (int dx2 = -1; dx2 <= 1; ++dx2) {
                        if (!dx2 && !dy2)
                            continue;
                        int nx = static_cast<int>(cx2) + dx2;
                        int ny = static_cast<int>(cy2) + dy2;
                        if (nx < 0 || nx >= static_cast<int>(w) || ny < 0 ||
                            ny >= static_cast<int>(h))
                            continue;
                        size_t nidx = static_cast<size_t>(ny) * w + static_cast<size_t>(nx);
                        // Pull adjacent tiles toward the fjord floor (U-shaped cross-section)
                        float pull = carve * 0.4f;
                        float target2 = local_wl - pull;
                        if (target2 < eroded[nidx]) {
                            eroded[nidx] = eroded[nidx] * 0.7f + target2 * 0.3f;
                        }
                    }
                }
            }
        }
    }

    // ── 6. Distance-from-coast inland elevation bias ────────────────────
    // Push mountains inland: tiles far from coast get height boost.
    // This prevents mountains from hugging coastlines.
    {
        // Quick binary land/water classification
        std::vector<bool> is_land(total, false);
        for (size_t i = 0; i < total; ++i) {
            is_land[i] = eroded[i] >= env.water_level;
        }

        // Fast approximate distance from coast using BFS
        std::vector<float> dist_coast(total, 0.0f);
        std::deque<size_t> bfs_queue;
        std::vector<bool> visited(total, false);

        // Seed BFS with land tiles adjacent to water
        for (uint32_t y = 1; y < h - 1; ++y) {
            for (uint32_t x = 1; x < w - 1; ++x) {
                size_t idx = static_cast<size_t>(y) * w + x;
                if (!is_land[idx])
                    continue;
                bool near_water = false;
                for (int dy = -1; dy <= 1 && !near_water; ++dy)
                    for (int dx = -1; dx <= 1 && !near_water; ++dx)
                        if ((dx != 0 || dy != 0) &&
                            !is_land[static_cast<size_t>(y + dy) * w + (x + dx)])
                            near_water = true;
                if (near_water) {
                    dist_coast[idx] = 1.0f;
                    visited[idx] = true;
                    bfs_queue.push_back(idx);
                }
            }
        }

        while (!bfs_queue.empty()) {
            size_t idx = bfs_queue.front();
            bfs_queue.pop_front();
            auto cx = static_cast<uint32_t>(idx % w);
            auto cy = static_cast<uint32_t>(idx / w);
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0)
                        continue;
                    int nx = static_cast<int>(cx) + dx;
                    int ny = static_cast<int>(cy) + dy;
                    if (nx < 0 || nx >= static_cast<int>(w) || ny < 0 || ny >= static_cast<int>(h))
                        continue;
                    size_t nidx = static_cast<size_t>(ny) * w + static_cast<size_t>(nx);
                    if (visited[nidx] || !is_land[nidx])
                        continue;
                    visited[nidx] = true;
                    dist_coast[nidx] = dist_coast[idx] + 1.0f;
                    bfs_queue.push_back(nidx);
                }
            }
        }

        // Apply inland bias: height += saturating function of distance
        constexpr float INLAND_BIAS_STRENGTH = 0.12f;
        constexpr float INLAND_BIAS_HALFLIFE = 15.0f;  // tiles to reach half effect
        for (size_t i = 0; i < total; ++i) {
            if (dist_coast[i] > 0.0f) {
                float bias =
                    INLAND_BIAS_STRENGTH * (1.0f - std::exp(-dist_coast[i] / INLAND_BIAS_HALFLIFE));
                eroded[i] += bias;
            }
        }

        // Coastal plain enforcement: very subtle — only flatten the steepest
        // coastal cells to prevent cliff artifacts, not a visible "halo".
        {
            std::vector<float> coast_snap(eroded);
            for (size_t i = 0; i < total; ++i) {
                if (dist_coast[i] <= 0.0f || dist_coast[i] > 3.0f)
                    continue;
                auto cx = static_cast<uint32_t>(i % w);
                auto cy = static_cast<uint32_t>(i / w);
                // Only soften very steep coastal transitions
                float local_slope = 0.0f;
                if (cx > 0 && cx < w - 1 && cy > 0 && cy < h - 1) {
                    float dx = coast_snap[i + 1] - coast_snap[i - 1];
                    float dy = coast_snap[i + w] - coast_snap[i - w];
                    local_slope = std::sqrt(dx * dx + dy * dy);
                }
                if (local_slope < 0.08f)
                    continue;  // gentle coast already fine
                // Soften extreme coastal slopes only
                float t = std::clamp(1.0f - dist_coast[i] / 3.0f, 0.0f, 1.0f);
                t *= std::clamp((local_slope - 0.08f) / 0.10f, 0.0f, 1.0f);
                float target = env.water_level + 0.03f;
                eroded[i] = eroded[i] * (1.0f - t * 0.25f) + target * (t * 0.25f);
            }
        }
    }

    // ── 7. Exposure-based SDF coastal remodeling ──────────────────────────
    // Instead of uniform fractal noise, compute wave exposure per coastal tile
    // and apply different morphological operations based on coast type:
    // - Exposed + steep → rocky headlands, more crenulation
    // - Sheltered + gentle → smooth sandy coasts, less detail
    // - Mountainous → cliff-backed, narrow embayments
    // - Lowland → broad, gently curving
    {
        // 7a. Compute signed distance field (positive = land, negative = ocean)
        // Uses approximate BFS distance (fast, sufficient for morphology decisions)
        std::vector<float> coast_sdf(total, 0.0f);
        {
            std::deque<size_t> sdf_q;
            std::vector<bool> sdf_v(total, false);
            // Seed with coastal boundary tiles (land adjacent to water or vice versa)
            for (uint32_t y2 = 1; y2 < h - 1; ++y2) {
                for (uint32_t x2 = 1; x2 < w - 1; ++x2) {
                    size_t idx = static_cast<size_t>(y2) * w + x2;
                    bool me_land = eroded[idx] >= env.water_level;
                    bool at_boundary = false;
                    for (int dy2 = -1; dy2 <= 1 && !at_boundary; ++dy2)
                        for (int dx2 = -1; dx2 <= 1 && !at_boundary; ++dx2) {
                            if (!dx2 && !dy2)
                                continue;
                            size_t nidx = static_cast<size_t>(y2 + dy2) * w + (x2 + dx2);
                            if ((eroded[nidx] >= env.water_level) != me_land)
                                at_boundary = true;
                        }
                    if (at_boundary) {
                        coast_sdf[idx] = me_land ? 1.0f : -1.0f;
                        sdf_v[idx] = true;
                        sdf_q.push_back(idx);
                    }
                }
            }
            while (!sdf_q.empty()) {
                size_t idx = sdf_q.front();
                sdf_q.pop_front();
                float my_dist = coast_sdf[idx];
                float sign = my_dist > 0 ? 1.0f : -1.0f;
                auto cx2 = static_cast<uint32_t>(idx % w);
                auto cy2 = static_cast<uint32_t>(idx / w);
                for (int dy2 = -1; dy2 <= 1; ++dy2) {
                    for (int dx2 = -1; dx2 <= 1; ++dx2) {
                        if (!dx2 && !dy2)
                            continue;
                        int nx = static_cast<int>(cx2) + dx2;
                        int ny = static_cast<int>(cy2) + dy2;
                        if (nx < 0 || nx >= static_cast<int>(w) || ny < 0 ||
                            ny >= static_cast<int>(h))
                            continue;
                        size_t nidx = static_cast<size_t>(ny) * w + static_cast<size_t>(nx);
                        if (sdf_v[nidx])
                            continue;
                        bool n_land = eroded[nidx] >= env.water_level;
                        if ((sign > 0) != n_land)
                            continue;  // same side only
                        sdf_v[nidx] = true;
                        coast_sdf[nidx] = sign * (std::abs(my_dist) + 1.0f);
                        sdf_q.push_back(nidx);
                    }
                }
            }
        }

        // 7b. Compute wave exposure (directional fetch)
        // For each near-coast tile, sample 16 directions and measure how far
        // you can go over open water. High exposure = facing open ocean.
        constexpr int N_FETCH_DIRS = 16;
        constexpr float FETCH_MAX = 60.0f;  // max fetch distance in tiles
        std::vector<float> exposure(total, 0.0f);
        {
            // Precompute direction vectors
            float dir_x[N_FETCH_DIRS], dir_y[N_FETCH_DIRS];
            for (int d = 0; d < N_FETCH_DIRS; ++d) {
                float angle = 2.0f * 3.14159265f * static_cast<float>(d) / N_FETCH_DIRS;
                dir_x[d] = std::cos(angle);
                dir_y[d] = std::sin(angle);
            }
            for (uint32_t y2 = 0; y2 < h; ++y2) {
                for (uint32_t x2 = 0; x2 < w; ++x2) {
                    size_t idx = static_cast<size_t>(y2) * w + x2;
                    float abs_sdf = std::abs(coast_sdf[idx]);
                    if (abs_sdf > 15.0f)
                        continue;  // only compute near coast
                    float total_fetch = 0.0f;
                    for (int d = 0; d < N_FETCH_DIRS; ++d) {
                        float fetch = 0.0f;
                        for (float t = 1.0f; t <= FETCH_MAX; t += 1.0f) {
                            int sx2 = static_cast<int>(x2) + static_cast<int>(t * dir_x[d]);
                            int sy2 = static_cast<int>(y2) + static_cast<int>(t * dir_y[d]);
                            if (sx2 < 0 || sx2 >= static_cast<int>(w) || sy2 < 0 ||
                                sy2 >= static_cast<int>(h)) {
                                fetch = FETCH_MAX;  // off-map = open ocean
                                break;
                            }
                            size_t sidx = static_cast<size_t>(sy2) * w + static_cast<size_t>(sx2);
                            if (eroded[sidx] >= env.water_level)
                                break;  // hit land
                            fetch = t;
                        }
                        total_fetch += fetch;
                    }
                    exposure[idx] = total_fetch / (N_FETCH_DIRS * FETCH_MAX);  // normalize 0-1
                }
            }
        }

        // Blur exposure field to avoid hard transitions (Gaussian-like, 3 passes)
        {
            std::vector<float> expo_buf(exposure);
            for (int blur_iter = 0; blur_iter < 3; ++blur_iter) {
                for (uint32_t y3 = 1; y3 < h - 1; ++y3) {
                    for (uint32_t x3 = 1; x3 < w - 1; ++x3) {
                        size_t idx = static_cast<size_t>(y3) * w + x3;
                        if (exposure[idx] == 0.0f && expo_buf[idx] == 0.0f)
                            continue;
                        float sum2 = exposure[idx] * 4.0f + exposure[idx - 1] + exposure[idx + 1] +
                                     exposure[idx - w] + exposure[idx + w];
                        expo_buf[idx] = sum2 / 8.0f;
                    }
                }
                std::swap(exposure, expo_buf);
            }
        }

        // 7c. Compute coastal slope for hardness proxy
        std::vector<float> coast_slope(total, 0.0f);
        for (uint32_t y2 = 1; y2 < h - 1; ++y2) {
            for (uint32_t x2 = 1; x2 < w - 1; ++x2) {
                size_t idx = static_cast<size_t>(y2) * w + x2;
                float dx2 = eroded[idx + 1] - eroded[idx - 1];
                float dy2 = eroded[idx + w] - eroded[idx - w];
                coast_slope[idx] = std::sqrt(dx2 * dx2 + dy2 * dy2);
            }
        }

        // 7d. Exposure-driven anisotropic displacement
        // Different noise scales and amplitudes based on coast type.
        // Exposed rocky coasts: strong multi-scale crenulation (headlands, coves)
        // Sheltered gentle coasts: mild smoothing, gentler curves
        FastNoiseLite cren_broad_x, cren_broad_y;  // meso-scale (gulfs, capes)
        cren_broad_x.SetSeed(static_cast<int>(seed + 31000));
        cren_broad_x.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        cren_broad_x.SetFractalType(FastNoiseLite::FractalType_FBm);
        cren_broad_x.SetFractalOctaves(3);
        cren_broad_x.SetFrequency(0.012f);
        cren_broad_y.SetSeed(static_cast<int>(seed + 31001));
        cren_broad_y.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        cren_broad_y.SetFractalType(FastNoiseLite::FractalType_FBm);
        cren_broad_y.SetFractalOctaves(3);
        cren_broad_y.SetFrequency(0.012f);

        FastNoiseLite cren_mid_x, cren_mid_y;  // mid-scale (bays, headlands)
        cren_mid_x.SetSeed(static_cast<int>(seed + 31010));
        cren_mid_x.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        cren_mid_x.SetFractalType(FastNoiseLite::FractalType_FBm);
        cren_mid_x.SetFractalOctaves(2);
        cren_mid_x.SetFrequency(0.035f);
        cren_mid_y.SetSeed(static_cast<int>(seed + 31011));
        cren_mid_y.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        cren_mid_y.SetFractalType(FastNoiseLite::FractalType_FBm);
        cren_mid_y.SetFractalOctaves(2);
        cren_mid_y.SetFrequency(0.035f);

        FastNoiseLite cren_fine_x, cren_fine_y;  // fine-scale (inlets, coves)
        cren_fine_x.SetSeed(static_cast<int>(seed + 31020));
        cren_fine_x.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        cren_fine_x.SetFrequency(0.08f);
        cren_fine_y.SetSeed(static_cast<int>(seed + 31021));
        cren_fine_y.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        cren_fine_y.SetFrequency(0.08f);

        // Height perturbation noise (adds concavities in sheltered areas)
        FastNoiseLite coast_perturb;
        coast_perturb.SetSeed(static_cast<int>(seed + 31100));
        coast_perturb.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
        coast_perturb.SetFractalType(FastNoiseLite::FractalType_FBm);
        coast_perturb.SetFractalOctaves(3);
        coast_perturb.SetFrequency(0.03f);

        std::vector<float> warped(eroded);
        for (uint32_t y2 = 2; y2 < h - 2; ++y2) {
            for (uint32_t x2 = 2; x2 < w - 2; ++x2) {
                size_t idx = static_cast<size_t>(y2) * w + x2;
                float fx = static_cast<float>(x2);
                float fy = static_cast<float>(y2);

                float abs_sdf = std::abs(coast_sdf[idx]);
                if (abs_sdf > 20.0f)
                    continue;  // only affect near-coast

                float expo = exposure[idx];
                float slope2 = coast_slope[idx];
                float proximity = std::clamp(1.0f - abs_sdf / 20.0f, 0.0f, 1.0f);

                // Coast type classification
                // Exposed rocky: high exposure + steep slope
                float rocky =
                    std::clamp(expo * 2.0f, 0.0f, 1.0f) * std::clamp(slope2 * 8.0f, 0.2f, 1.0f);
                // Sheltered sandy: low exposure + gentle slope
                float sandy = std::clamp(1.0f - expo * 2.5f, 0.0f, 1.0f) *
                              std::clamp(1.0f - slope2 * 6.0f, 0.2f, 1.0f);

                // Displacement amplitude: exposure modulates SPECTRUM not just amplitude
                // Exposed: all frequencies active (broad + mid + fine)
                // Sheltered: mostly low-frequency gentle arcs, high-freq suppressed 85-95%
                float exp_broad = 1.0f - sandy * 0.30f;  // broad: barely reduced for sheltered
                float exp_mid = 1.0f - sandy * 0.70f;    // mid: heavily reduced for sheltered
                float exp_fine = 1.0f - sandy * 0.90f;   // fine: almost eliminated for sheltered
                // Reduced amplitudes to prevent coastal ridge artifact:
                // domain warp near steep coasts would sample high inland elevations,
                // creating artificial ridges at the shoreline.
                float amp_broad = 6.0f * (0.4f + rocky * 0.6f) * exp_broad;
                float amp_mid = 3.0f * (0.2f + rocky * 0.8f) * exp_mid;
                float amp_fine = 1.2f * (0.1f + rocky * 0.9f) * exp_fine;

                float dx_b = cren_broad_x.GetNoise(fx, fy) * amp_broad;
                float dy_b = cren_broad_y.GetNoise(fx, fy) * amp_broad;
                float dx_m = cren_mid_x.GetNoise(fx, fy) * amp_mid;
                float dy_m = cren_mid_y.GetNoise(fx, fy) * amp_mid;
                float dx_f = cren_fine_x.GetNoise(fx, fy) * amp_fine;
                float dy_f = cren_fine_y.GetNoise(fx, fy) * amp_fine;

                float dx_total = (dx_b + dx_m + dx_f) * proximity;
                float dy_total = (dy_b + dy_m + dy_f) * proximity;

                // Bilinear sample from warped position
                float sx2 = std::clamp(fx + dx_total, 1.0f, static_cast<float>(w) - 2.0f);
                float sy2 = std::clamp(fy + dy_total, 1.0f, static_cast<float>(h) - 2.0f);
                auto ix2 = static_cast<uint32_t>(sx2);
                auto iy2 = static_cast<uint32_t>(sy2);
                float fxr = sx2 - static_cast<float>(ix2);
                float fyr = sy2 - static_cast<float>(iy2);
                size_t i00 = static_cast<size_t>(iy2) * w + ix2;
                size_t i10 = i00 + 1;
                size_t i01 = i00 + w;
                size_t i11 = i01 + 1;
                float sampled = eroded[i00] * (1 - fxr) * (1 - fyr) +
                                eroded[i10] * fxr * (1 - fyr) + eroded[i01] * (1 - fxr) * fyr +
                                eroded[i11] * fxr * fyr;

                // Blend: stronger warp near coastline, weaker far away
                // STRICTLY SUBTRACTIVE on land: domain warp must never raise
                // coastal terrain above its original value (prevents ridge artifact
                // from sampling high inland elevations).
                float blend = proximity * 0.85f;
                float blended = eroded[idx] * (1.0f - blend) + sampled * blend;
                if (coast_sdf[idx] > 0.0f) {
                    // Land tile: only allow lowering
                    warped[idx] = std::min(eroded[idx], blended);
                } else {
                    warped[idx] = blended;
                }

                // Sheltered cove concavities: in low-exposure areas, add negative
                // height perturbation to create embayments
                if (sandy > 0.4f && coast_sdf[idx] > 0.0f && coast_sdf[idx] < 5.0f) {
                    float cove = coast_perturb.GetNoise(fx, fy);
                    if (cove < -0.2f) {
                        // Carve small cove
                        float carve2 = (-cove - 0.2f) * 0.10f * sandy * proximity;
                        warped[idx] -= carve2;
                    }
                }
            }
        }
        std::swap(eroded, warped);

        // 7e. Coastal profile enforcement: gradual elevation ramp at shoreline
        // Prevents the "rim/moat" artifact by ensuring a smooth transition from
        // land to sea level based on coast type.
        // Sandy coasts: wide gentle ramp (4-10 tiles)
        // Rocky coasts: narrow steep ramp (1-3 tiles)
        {
            // Recompute SDF after warping (coastline position changed)
            std::vector<float> prof_sdf(total, 0.0f);
            {
                std::deque<size_t> pq;
                std::vector<bool> pv(total, false);
                for (uint32_t y3 = 1; y3 < h - 1; ++y3) {
                    for (uint32_t x3 = 1; x3 < w - 1; ++x3) {
                        size_t idx = static_cast<size_t>(y3) * w + x3;
                        bool me_land = eroded[idx] >= env.water_level;
                        bool at_boundary = false;
                        for (int dy3 = -1; dy3 <= 1 && !at_boundary; ++dy3)
                            for (int dx3 = -1; dx3 <= 1 && !at_boundary; ++dx3) {
                                if (!dx3 && !dy3)
                                    continue;
                                size_t nidx = static_cast<size_t>(y3 + dy3) * w + (x3 + dx3);
                                if ((eroded[nidx] >= env.water_level) != me_land)
                                    at_boundary = true;
                            }
                        if (at_boundary) {
                            prof_sdf[idx] = me_land ? 1.0f : -1.0f;
                            pv[idx] = true;
                            pq.push_back(idx);
                        }
                    }
                }
                while (!pq.empty()) {
                    size_t idx = pq.front();
                    pq.pop_front();
                    float my_dist = prof_sdf[idx];
                    float sign = my_dist > 0 ? 1.0f : -1.0f;
                    auto cx3 = static_cast<uint32_t>(idx % w);
                    auto cy3 = static_cast<uint32_t>(idx / w);
                    for (int dy3 = -1; dy3 <= 1; ++dy3) {
                        for (int dx3 = -1; dx3 <= 1; ++dx3) {
                            if (!dx3 && !dy3)
                                continue;
                            int nx = static_cast<int>(cx3) + dx3;
                            int ny = static_cast<int>(cy3) + dy3;
                            if (nx < 0 || nx >= static_cast<int>(w) || ny < 0 ||
                                ny >= static_cast<int>(h))
                                continue;
                            size_t nidx = static_cast<size_t>(ny) * w + static_cast<size_t>(nx);
                            if (pv[nidx])
                                continue;
                            bool n_land = eroded[nidx] >= env.water_level;
                            if ((sign > 0) != n_land)
                                continue;
                            pv[nidx] = true;
                            prof_sdf[nidx] = sign * (std::abs(my_dist) + 1.0f);
                            pq.push_back(nidx);
                        }
                    }
                }
            }

            // Apply sigmoid ramp on land tiles very close to coast
            for (uint32_t y3 = 0; y3 < h; ++y3) {
                for (uint32_t x3 = 0; x3 < w; ++x3) {
                    size_t idx = static_cast<size_t>(y3) * w + x3;
                    float sdf = prof_sdf[idx];
                    if (sdf <= 0.0f || sdf > 10.0f)
                        continue;  // land within 10 tiles of coast

                    // Determine coast type from local exposure
                    float expo2 = exposure[idx];
                    float slope3 = coast_slope[idx];

                    // Gate: don't ramp down high-elevation or steep tiles (cliff coasts)
                    float elev_above_wl = eroded[idx] - env.water_level;
                    // High terrain near coast = cliff, skip ramp
                    float elev_gate = std::clamp(1.0f - elev_above_wl * 6.0f, 0.0f, 1.0f);
                    // Steep slopes resist ramping (already a cliff)
                    float slope_gate2 = std::clamp(1.0f - slope3 * 10.0f, 0.0f, 1.0f);
                    float ramp_gate = elev_gate * slope_gate2;
                    if (ramp_gate < 0.05f)
                        continue;  // effectively a cliff, don't ramp

                    // Ramp width: rocky=1 tile, sandy=3 tiles (subtle, not a halo)
                    float rocky2 = std::clamp(expo2 * 2.0f + slope3 * 5.0f, 0.0f, 1.0f);
                    float ramp_width = 1.0f + (1.0f - rocky2) * 2.0f;

                    if (sdf > ramp_width)
                        continue;

                    // Sigmoid blend toward water level
                    float t = sdf / ramp_width;
                    t = t * t * (3.0f - 2.0f * t);                 // smoothstep
                    float target_elev = env.water_level + 0.008f;  // just above water
                    float ramp_elev = target_elev + (eroded[idx] - target_elev) * t;
                    // Weaker blend to avoid halo
                    float blend_str = ramp_gate * 0.5f;  // max 50% blend
                    ramp_elev = eroded[idx] * (1.0f - blend_str) + ramp_elev * blend_str;
                    if (ramp_elev < eroded[idx]) {
                        eroded[idx] = ramp_elev;
                    }
                }
            }
        }

        // ── 7b. Depositional coastal features ───────────────────────────────
        // On low-gradient, low-exposure coasts: barrier islands and river-mouth deltas.
        // These are constructed by adding thin ridges offshore and protruding lobes at
        // high-flow river mouths. Uses coast_sdf and exposure from step 7a-c above.
        // Recompute flow accumulation one more time for delta detection
        std::vector<float> delta_flow(total, 1.0f);
        {
            std::vector<size_t> delta_flow_to(total, SIZE_MAX);
            for (uint32_t y2 = 1; y2 < h - 1; ++y2) {
                for (uint32_t x2 = 1; x2 < w - 1; ++x2) {
                    size_t idx = static_cast<size_t>(y2) * w + x2;
                    if (eroded[idx] < env.water_level)
                        continue;
                    float center = eroded[idx];
                    float steepest = 0.0f;
                    for (int dy2 = -1; dy2 <= 1; ++dy2) {
                        for (int dx2 = -1; dx2 <= 1; ++dx2) {
                            if (!dx2 && !dy2)
                                continue;
                            size_t nidx = static_cast<size_t>(y2 + dy2) * w + (x2 + dx2);
                            float sl = center - eroded[nidx];
                            if (sl > steepest) {
                                steepest = sl;
                                delta_flow_to[idx] = nidx;
                            }
                        }
                    }
                }
            }
            std::vector<size_t> ord(total);
            std::iota(ord.begin(), ord.end(), 0u);
            std::sort(ord.begin(), ord.end(),
                      [&](size_t a, size_t b) { return eroded[a] > eroded[b]; });
            for (size_t idx : ord) {
                if (delta_flow_to[idx] != SIZE_MAX)
                    delta_flow[delta_flow_to[idx]] += delta_flow[idx];
            }
        }

        // Find river mouths: land tiles with high flow adjacent to ocean
        constexpr float DELTA_FLOW_THRESH = 150.0f;  // lower to catch more river mouths
        constexpr float DELTA_LOBE_HEIGHT = 0.018f;  // height above water for delta land
        constexpr int DELTA_MAX_RADIUS = 8;          // max delta protrusion in tiles

        // Delta noise for distributary channel variation
        FastNoiseLite delta_noise;
        delta_noise.SetSeed(static_cast<int>(seed + 32000));
        delta_noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        delta_noise.SetFrequency(0.06f);

        for (uint32_t y2 = 2; y2 < h - 2; ++y2) {
            for (uint32_t x2 = 2; x2 < w - 2; ++x2) {
                size_t idx = static_cast<size_t>(y2) * w + x2;
                if (eroded[idx] < env.water_level)
                    continue;  // must be land
                if (delta_flow[idx] < DELTA_FLOW_THRESH)
                    continue;

                // Check if adjacent to ocean
                bool adj_ocean = false;
                float ocean_dx = 0.0f, ocean_dy = 0.0f;
                int ocean_count = 0;
                for (int dy2 = -1; dy2 <= 1; ++dy2) {
                    for (int dx2 = -1; dx2 <= 1; ++dx2) {
                        if (!dx2 && !dy2)
                            continue;
                        size_t nidx = static_cast<size_t>(y2 + dy2) * w + (x2 + dx2);
                        if (eroded[nidx] < env.water_level) {
                            adj_ocean = true;
                            ocean_dx += static_cast<float>(dx2);
                            ocean_dy += static_cast<float>(dy2);
                            ocean_count++;
                        }
                    }
                }
                if (!adj_ocean)
                    continue;

                // Direction toward ocean (averaged)
                if (ocean_count > 0) {
                    ocean_dx /= static_cast<float>(ocean_count);
                    ocean_dy /= static_cast<float>(ocean_count);
                }
                float od_len = std::sqrt(ocean_dx * ocean_dx + ocean_dy * ocean_dy);
                if (od_len < 0.01f)
                    continue;
                ocean_dx /= od_len;
                ocean_dy /= od_len;

                // Build delta lobe: fan of sediment protruding into ocean
                float flow_scale =
                    std::clamp((delta_flow[idx] - DELTA_FLOW_THRESH) / 2000.0f, 0.0f, 1.0f);
                int radius = 2 + static_cast<int>(flow_scale * (DELTA_MAX_RADIUS - 2));

                for (int ry = -radius; ry <= radius; ++ry) {
                    for (int rx = -radius; rx <= radius; ++rx) {
                        int nx = static_cast<int>(x2) + rx;
                        int ny = static_cast<int>(y2) + ry;
                        if (nx < 0 || nx >= static_cast<int>(w) || ny < 0 ||
                            ny >= static_cast<int>(h))
                            continue;
                        size_t nidx = static_cast<size_t>(ny) * w + static_cast<size_t>(nx);
                        if (eroded[nidx] >= env.water_level)
                            continue;  // only fill ocean

                        // Only fill in the oceanward direction (fan shape)
                        float dot =
                            static_cast<float>(rx) * ocean_dx + static_cast<float>(ry) * ocean_dy;
                        if (dot < 0.3f)
                            continue;  // behind the mouth

                        float dist2 = std::sqrt(static_cast<float>(rx * rx + ry * ry));
                        if (dist2 > static_cast<float>(radius))
                            continue;

                        // Radial falloff + noise for distributary channels
                        float radial = 1.0f - dist2 / static_cast<float>(radius);
                        float dn =
                            delta_noise.GetNoise(static_cast<float>(nx), static_cast<float>(ny));

                        // Angular variation: create distributary channel gaps
                        float angle = std::atan2(static_cast<float>(ry), static_cast<float>(rx));
                        float channel_gap = std::sin(angle * 3.0f + dn * 2.0f);
                        if (channel_gap < -0.3f)
                            continue;  // channel gap

                        float lobe_h = DELTA_LOBE_HEIGHT * radial * flow_scale;
                        float target3 = env.water_level + lobe_h;
                        if (target3 > eroded[nidx]) {
                            eroded[nidx] = std::max(eroded[nidx], target3);
                        }
                    }
                }
            }
        }

        // Barrier islands: on low-slope, moderate-exposure coasts with wide shelf
        // Create thin offshore ridges parallel to the coastline
        FastNoiseLite barrier_noise;
        barrier_noise.SetSeed(static_cast<int>(seed + 32500));
        barrier_noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        barrier_noise.SetFractalType(FastNoiseLite::FractalType_FBm);
        barrier_noise.SetFractalOctaves(2);
        barrier_noise.SetFrequency(0.04f);

        FastNoiseLite barrier_gap;
        barrier_gap.SetSeed(static_cast<int>(seed + 32600));
        barrier_gap.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        barrier_gap.SetFrequency(0.08f);

        for (uint32_t y2 = 2; y2 < h - 2; ++y2) {
            for (uint32_t x2 = 2; x2 < w - 2; ++x2) {
                size_t idx = static_cast<size_t>(y2) * w + x2;
                if (eroded[idx] >= env.water_level)
                    continue;  // must be ocean

                float sdf = coast_sdf[idx];
                if (sdf >= 0.0f)
                    continue;  // must be on ocean side
                float dist_from_coast = -sdf;

                // Barrier islands form 3-8 tiles offshore
                if (dist_from_coast < 3.0f || dist_from_coast > 8.0f)
                    continue;

                // Only form on gently-sloping coasts (check slope of nearest land)
                float expo2 = exposure[idx];
                // Moderate exposure only (not fully exposed, not fully sheltered)
                if (expo2 > 0.6f || expo2 < 0.1f)
                    continue;

                // Noise-driven barrier presence
                float fx2 = static_cast<float>(x2);
                float fy2 = static_cast<float>(y2);
                float bn = barrier_noise.GetNoise(fx2, fy2);
                if (bn < 0.1f)
                    continue;  // no barrier here

                // Gap noise: periodic breaks for tidal inlets
                float gn = barrier_gap.GetNoise(fx2, fy2);
                if (gn < -0.2f)
                    continue;  // tidal inlet gap

                // Barrier ridge: raised just above water level
                float barrier_h = 0.012f * std::clamp(bn, 0.0f, 0.6f) / 0.6f;
                // Narrower toward edges of barrier distance band
                float band_center = 5.5f;
                float band_width = 2.5f;
                float band_factor = 1.0f - std::abs(dist_from_coast - band_center) / band_width;
                band_factor = std::clamp(band_factor, 0.0f, 1.0f);
                barrier_h *= band_factor;

                if (barrier_h > 0.002f) {
                    eroded[idx] = std::max(eroded[idx], env.water_level + barrier_h);
                }
            }
        }

        // ── 7c. Skerry field generation on exposed headlands ────────────────
        // Instead of per-tile noise peppering, identify headland candidate locations
        // and spawn clustered skerry fields around them.
        {
            // Compute coastline curvature (headlands = positive)
            std::vector<float> coast_curv(total, 0.0f);
            {
                std::vector<float> curv_h(total);
                for (size_t i = 0; i < total; ++i)
                    curv_h[i] = (eroded[i] - env.water_level) * 10.0f;
                for (uint32_t y2 = 1; y2 < h - 1; ++y2) {
                    for (uint32_t x2 = 1; x2 < w - 1; ++x2) {
                        size_t idx = static_cast<size_t>(y2) * w + x2;
                        if (std::abs(curv_h[idx]) > 5.0f)
                            continue;
                        coast_curv[idx] = curv_h[idx - 1] + curv_h[idx + 1] + curv_h[idx - w] +
                                          curv_h[idx + w] - 4.0f * curv_h[idx];
                    }
                }
            }

            // Find headland cluster centers: coastal ocean tiles with high exposure
            // + positive curvature. Use sparse sampling to avoid over-generation.
            struct SkerryCluster {
                int cx, cy;
                float strength;
            };
            std::vector<SkerryCluster> clusters;
            constexpr int CLUSTER_SPACING = 8;            // min spacing between cluster centers
            for (uint32_t y2 = 4; y2 < h - 4; y2 += 3) {  // sparse scan
                for (uint32_t x2 = 4; x2 < w - 4; x2 += 3) {
                    size_t idx = static_cast<size_t>(y2) * w + x2;
                    if (eroded[idx] >= env.water_level)
                        continue;  // ocean only
                    float sdf3 = coast_sdf[idx];
                    if (sdf3 >= 0.0f)
                        continue;
                    float dist3 = -sdf3;
                    if (dist3 < 2.0f || dist3 > 8.0f)
                        continue;

                    float expo3 = exposure[idx];
                    if (expo3 < 0.4f)
                        continue;

                    float curv3 = coast_curv[idx];
                    if (curv3 < 0.15f)
                        continue;

                    // Check distance from existing clusters
                    bool too_close = false;
                    for (const auto& c : clusters) {
                        float dx3 = static_cast<float>(x2 - c.cx);
                        float dy3 = static_cast<float>(y2 - c.cy);
                        if (dx3 * dx3 + dy3 * dy3 < CLUSTER_SPACING * CLUSTER_SPACING) {
                            too_close = true;
                            break;
                        }
                    }
                    if (too_close)
                        continue;

                    float strength = std::clamp(expo3 * curv3 * 5.0f, 0.0f, 1.0f);
                    clusters.push_back({static_cast<int>(x2), static_cast<int>(y2), strength});
                }
            }

            // For each cluster center, spawn a small field of skerries
            FastNoiseLite skerry_noise;
            skerry_noise.SetSeed(static_cast<int>(seed + 33000));
            skerry_noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
            skerry_noise.SetFrequency(0.07f);

            for (const auto& cluster : clusters) {
                int radius = 3 + static_cast<int>(cluster.strength * 3.0f);  // 3-6 tile radius
                for (int ry = -radius; ry <= radius; ++ry) {
                    for (int rx = -radius; rx <= radius; ++rx) {
                        int nx = cluster.cx + rx;
                        int ny = cluster.cy + ry;
                        if (nx < 1 || nx >= static_cast<int>(w) - 1 || ny < 1 ||
                            ny >= static_cast<int>(h) - 1)
                            continue;
                        size_t nidx = static_cast<size_t>(ny) * w + static_cast<size_t>(nx);
                        if (eroded[nidx] >= env.water_level)
                            continue;  // already land

                        float dist2 = std::sqrt(static_cast<float>(rx * rx + ry * ry));
                        if (dist2 > static_cast<float>(radius))
                            continue;

                        // Noise gate for individual islet placement
                        float sn =
                            skerry_noise.GetNoise(static_cast<float>(nx), static_cast<float>(ny));
                        float threshold = 0.2f + dist2 / static_cast<float>(radius) * 0.3f;
                        if (sn < threshold)
                            continue;

                        // Min spacing between skerries: skip if land neighbor exists
                        bool has_land_neighbor = false;
                        for (int dy3 = -1; dy3 <= 1 && !has_land_neighbor; ++dy3)
                            for (int dx3 = -1; dx3 <= 1 && !has_land_neighbor; ++dx3) {
                                if (!dx3 && !dy3)
                                    continue;
                                size_t nnidx = static_cast<size_t>(ny + dy3) * w +
                                               static_cast<size_t>(nx + dx3);
                                if (eroded[nnidx] >= env.water_level)
                                    has_land_neighbor = true;
                            }
                        // Allow touching existing land (extends headland) or isolated
                        float height =
                            0.010f * (1.0f - dist2 / static_cast<float>(radius)) * cluster.strength;
                        if (height > 0.003f) {
                            eroded[nidx] = std::max(eroded[nidx], env.water_level + height);
                        }
                    }
                }
            }
        }

        // ── 7d. Sub-bay inlet carving ───────────────────────────────────────
        // In sheltered concavities (bays), carve narrow branching inlets aligned
        // to local drainage directions to create ria-like substructure.
        {
            // Recompute flow for inlet direction guidance
            std::vector<int> inlet_dx(total, 0), inlet_dy(total, 0);
            for (uint32_t y2 = 1; y2 < h - 1; ++y2) {
                for (uint32_t x2 = 1; x2 < w - 1; ++x2) {
                    size_t idx = static_cast<size_t>(y2) * w + x2;
                    if (eroded[idx] < env.water_level)
                        continue;
                    float center = eroded[idx];
                    float steepest = 0.0f;
                    int best_dx = 0, best_dy = 0;
                    for (int dy2 = -1; dy2 <= 1; ++dy2) {
                        for (int dx2 = -1; dx2 <= 1; ++dx2) {
                            if (!dx2 && !dy2)
                                continue;
                            size_t nidx = static_cast<size_t>(y2 + dy2) * w + (x2 + dx2);
                            float sl = center - eroded[nidx];
                            if (sl > steepest) {
                                steepest = sl;
                                best_dx = dx2;
                                best_dy = dy2;
                            }
                        }
                    }
                    inlet_dx[idx] = best_dx;
                    inlet_dy[idx] = best_dy;
                }
            }

            // Inlet placement noise
            FastNoiseLite inlet_noise;
            inlet_noise.SetSeed(static_cast<int>(seed + 34000));
            inlet_noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
            inlet_noise.SetFrequency(0.03f);

            // Find sheltered bay coastline tiles and trace inlets inland
            for (uint32_t y2 = 2; y2 < h - 2; ++y2) {
                for (uint32_t x2 = 2; x2 < w - 2; ++x2) {
                    size_t idx = static_cast<size_t>(y2) * w + x2;
                    if (eroded[idx] < env.water_level)
                        continue;  // must be land

                    // Must be coastal (within 2 tiles of water)
                    float sdf4 = coast_sdf[idx];
                    if (sdf4 < 0.5f || sdf4 > 2.5f)
                        continue;

                    float expo4 = exposure[idx];
                    if (expo4 > 0.4f)
                        continue;  // only sheltered zones

                    // Noise gate: sparse inlet placement
                    float in_noise =
                        inlet_noise.GetNoise(static_cast<float>(x2), static_cast<float>(y2));
                    if (in_noise < 0.35f)
                        continue;

                    // Trace inlet: follow upstream for up to 8 tiles (shorter = more natural)
                    int cx3 = static_cast<int>(x2), cy3 = static_cast<int>(y2);
                    for (int step = 0; step < 8; ++step) {
                        size_t cur = static_cast<size_t>(cy3) * w + static_cast<size_t>(cx3);
                        if (eroded[cur] < env.water_level)
                            break;  // reached water

                        // Carve: lower this tile below water level (tapering inlet)
                        float carve_depth = 0.012f * (1.0f - static_cast<float>(step) / 8.0f);
                        eroded[cur] = std::min(eroded[cur], env.water_level - carve_depth);

                        // Follow steepest uphill (opposite of downhill) to trace into land
                        int ddx = inlet_dx[cur], ddy = inlet_dy[cur];
                        if (ddx == 0 && ddy == 0)
                            break;
                        // Go OPPOSITE to flow (upstream = into land)
                        cx3 -= ddx;
                        cy3 -= ddy;
                        if (cx3 < 1 || cx3 >= static_cast<int>(w) - 1 || cy3 < 1 ||
                            cy3 >= static_cast<int>(h) - 1)
                            break;
                    }
                }
            }
        }
    }  // end of merged step 7 block (coast_sdf, exposure in scope)

    // ── 8. (Basin flood-fill moved to step 5, before stream-power erosion) ──

    // ── 8b. Global terrain smoothing ──────────────────────────────────
    // Light 3x3 Gaussian smooth to remove sharp edges from noise intersections.
    // Only on land tiles — preserve coastline position.
    {
        std::copy(eroded.begin(), eroded.end(), temp_buf.begin());
        for (uint32_t y = 1; y < h - 1; ++y) {
            for (uint32_t x = 1; x < w - 1; ++x) {
                size_t idx = static_cast<size_t>(y) * w + x;
                if (eroded[idx] < env.water_level)
                    continue;  // skip water
                // Only smooth interior land (not immediately coastal)
                float coast_dist = eroded[idx] - env.water_level;
                if (coast_dist < 0.03f)
                    continue;  // don't smooth coastline

                // 3x3 weighted average — very light blend to keep ridge structure
                float sum = eroded[idx] * 4.0f;
                sum += eroded[idx - 1] + eroded[idx + 1] + eroded[idx - w] + eroded[idx + w];
                sum += eroded[idx - w - 1] + eroded[idx - w + 1] + eroded[idx + w - 1] +
                       eroded[idx + w + 1];
                float avg = sum / 12.0f;
                // Lighter blend: preserve high-frequency ridge/valley contrast
                // Mountains get LESS smoothing to keep sharp ridgelines
                float elev_keep = std::clamp((eroded[idx] - 0.55f) * 4.0f, 0.0f, 0.5f);
                float blend = 0.25f * (1.0f - elev_keep);  // 25% in lowlands, ~0% in mountains
                temp_buf[idx] = eroded[idx] * (1.0f - blend) + avg * blend;
            }
        }
        std::swap(eroded, temp_buf);
    }

    // ── 9. Remove single-tile water speckle ─────────────────────────────
    // Any land tile that dipped below water_level from noise but isn't part
    // of a connected water body of at least 3 tiles gets raised back above.
    {
        std::vector<bool> is_water(total, false);
        for (size_t i = 0; i < total; ++i)
            is_water[i] = eroded[i] < env.water_level;

        // Connected-component labeling for water tiles
        std::vector<int> water_comp(total, -1);
        std::vector<uint32_t> comp_sizes;
        for (uint32_t y = 0; y < h; ++y) {
            for (uint32_t x = 0; x < w; ++x) {
                size_t idx = static_cast<size_t>(y) * w + x;
                if (!is_water[idx] || water_comp[idx] >= 0)
                    continue;

                int cid = static_cast<int>(comp_sizes.size());
                comp_sizes.push_back(0);
                std::deque<size_t> queue;
                water_comp[idx] = cid;
                queue.push_back(idx);

                while (!queue.empty()) {
                    size_t cur = queue.front();
                    queue.pop_front();
                    comp_sizes[static_cast<size_t>(cid)]++;

                    auto cx = static_cast<uint32_t>(cur % w);
                    auto cy = static_cast<uint32_t>(cur / w);
                    for (int dy = -1; dy <= 1; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            if (dx == 0 && dy == 0)
                                continue;
                            int nx = static_cast<int>(cx) + dx;
                            int ny = static_cast<int>(cy) + dy;
                            if (nx < 0 || nx >= static_cast<int>(w) || ny < 0 ||
                                ny >= static_cast<int>(h))
                                continue;
                            size_t nidx = static_cast<size_t>(ny) * w + static_cast<size_t>(nx);
                            if (is_water[nidx] && water_comp[nidx] < 0) {
                                water_comp[nidx] = cid;
                                queue.push_back(nidx);
                            }
                        }
                    }
                }
            }
        }

        // Remove water bodies smaller than 4 tiles (raise them above water level)
        for (size_t i = 0; i < total; ++i) {
            if (water_comp[i] >= 0 && comp_sizes[static_cast<size_t>(water_comp[i])] < 4) {
                eroded[i] = env.water_level + 0.01f;
            }
        }
    }

    // ── 9b. Remove debris islets ────────────────────────────────────────
    // Tiny land bodies created by displacement noise that aren't part of a
    // meaningful landmass. Sink them below water level.
    {
        std::vector<bool> is_land2(total, false);
        for (size_t i = 0; i < total; ++i)
            is_land2[i] = eroded[i] >= env.water_level;

        std::vector<int> land_comp(total, -1);
        std::vector<uint32_t> land_sizes;
        for (uint32_t y = 0; y < h; ++y) {
            for (uint32_t x = 0; x < w; ++x) {
                size_t idx = static_cast<size_t>(y) * w + x;
                if (!is_land2[idx] || land_comp[idx] >= 0)
                    continue;
                int cid = static_cast<int>(land_sizes.size());
                land_sizes.push_back(0);
                std::deque<size_t> lq;
                land_comp[idx] = cid;
                lq.push_back(idx);
                while (!lq.empty()) {
                    size_t cur = lq.front();
                    lq.pop_front();
                    land_sizes[static_cast<size_t>(cid)]++;
                    auto cx2 = static_cast<uint32_t>(cur % w);
                    auto cy2 = static_cast<uint32_t>(cur / w);
                    for (int dy2 = -1; dy2 <= 1; ++dy2) {
                        for (int dx2 = -1; dx2 <= 1; ++dx2) {
                            if (!dx2 && !dy2)
                                continue;
                            int nx = static_cast<int>(cx2) + dx2;
                            int ny = static_cast<int>(cy2) + dy2;
                            if (nx < 0 || nx >= static_cast<int>(w) || ny < 0 ||
                                ny >= static_cast<int>(h))
                                continue;
                            size_t nidx = static_cast<size_t>(ny) * w + static_cast<size_t>(nx);
                            if (is_land2[nidx] && land_comp[nidx] < 0) {
                                land_comp[nidx] = cid;
                                lq.push_back(nidx);
                            }
                        }
                    }
                }
            }
        }
        // Remove land bodies smaller than 8 tiles (noise debris)
        for (size_t i = 0; i < total; ++i) {
            if (land_comp[i] >= 0 && land_sizes[static_cast<size_t>(land_comp[i])] < 8) {
                eroded[i] = env.water_level - 0.02f;
            }
        }
    }

    // ── 9c. Narrow feature cleanup (morphological) ────────────────────
    // Remove 1-2 tile wide peninsulas/necks that look artificial at this
    // resolution. A land tile is a "neck" if it connects two larger land
    // bodies through a 1-tile pinch point.
    {
        // Detect neck tiles: land tiles where removing them would NOT disconnect
        // large land masses (approximated by counting land neighbors on opposite sides)
        std::vector<bool> is_neck(total, false);
        for (uint32_t y2 = 1; y2 < h - 1; ++y2) {
            for (uint32_t x2 = 1; x2 < w - 1; ++x2) {
                size_t idx = static_cast<size_t>(y2) * w + x2;
                if (eroded[idx] < env.water_level)
                    continue;  // ocean, skip

                // Count land neighbors in cardinal directions
                bool n = eroded[idx - w] >= env.water_level;
                bool s = eroded[idx + w] >= env.water_level;
                bool e2 = eroded[idx + 1] >= env.water_level;
                bool w2 = eroded[idx - 1] >= env.water_level;

                // A neck is a 1-tile bridge: land on two opposite sides, water on the other two
                bool ns_bridge = (n && s && !e2 && !w2);
                bool ew_bridge = (e2 && w2 && !n && !s);
                if (ns_bridge || ew_bridge) {
                    is_neck[idx] = true;
                }

                // Also detect corner bridges (diagonal connections only)
                if (!n && !s && !e2 && !w2) {
                    // Isolated from cardinals but connected diagonally
                    bool ne = eroded[idx - w + 1] >= env.water_level;
                    bool nw = eroded[idx - w - 1] >= env.water_level;
                    bool se = eroded[idx + w + 1] >= env.water_level;
                    bool sw = eroded[idx + w - 1] >= env.water_level;
                    int diag_count = (ne ? 1 : 0) + (nw ? 1 : 0) + (se ? 1 : 0) + (sw ? 1 : 0);
                    if (diag_count >= 2)
                        is_neck[idx] = true;
                }
            }
        }
        // Remove necks by sinking them
        for (size_t i = 0; i < total; ++i) {
            if (is_neck[i]) {
                eroded[i] = env.water_level - 0.005f;
            }
        }

        // Also shorten very thin peninsulas (1 tile wide, extending > 3 tiles)
        // Detect by checking if a land tile has water on 3 cardinal sides
        for (uint32_t y2 = 1; y2 < h - 1; ++y2) {
            for (uint32_t x2 = 1; x2 < w - 1; ++x2) {
                size_t idx = static_cast<size_t>(y2) * w + x2;
                if (eroded[idx] < env.water_level)
                    continue;

                bool n = eroded[idx - w] >= env.water_level;
                bool s = eroded[idx + w] >= env.water_level;
                bool e2 = eroded[idx + 1] >= env.water_level;
                bool w2 = eroded[idx - 1] >= env.water_level;
                int land_count = (n ? 1 : 0) + (s ? 1 : 0) + (e2 ? 1 : 0) + (w2 ? 1 : 0);

                // Peninsula tip: only 1 cardinal land neighbor
                if (land_count <= 1) {
                    eroded[idx] = env.water_level - 0.005f;
                }
            }
        }
    }

    // ── 10. SDF curvature flow — anti-alias coastlines ─────────────────
    // Instead of simple Laplacian smoothing, compute a signed distance field
    // near the coastline and apply curvature flow to smooth grid stairstepping.
    // This removes right-angle corners from the coastline contour while
    // preserving intentional bays and headlands.
    {
        // Build continuous SDF from height field (signed distance to water_level)
        // Positive = land, negative = ocean. Use height difference as initial SDF.
        constexpr int CURV_ITERS = 4;
        constexpr float CURV_DT = 0.18f;
        constexpr float CURV_BAND = 3.0f;  // only affect within 3 tiles of coast

        // Compute approximate signed distance using height gradient
        std::vector<float> curv_sdf(total);
        for (size_t i = 0; i < total; ++i) {
            curv_sdf[i] = (eroded[i] - env.water_level) * 8.0f;  // scale for curvature
            // Clamp to narrow band
            curv_sdf[i] = std::clamp(curv_sdf[i], -CURV_BAND, CURV_BAND);
        }

        // Curvature flow iterations: move the zero-crossing by mean curvature
        std::vector<float> curv_buf(curv_sdf);
        for (int iter = 0; iter < CURV_ITERS; ++iter) {
            for (uint32_t y = 1; y < h - 1; ++y) {
                for (uint32_t x = 1; x < w - 1; ++x) {
                    size_t idx = static_cast<size_t>(y) * w + x;
                    float c = curv_sdf[idx];
                    if (std::abs(c) > CURV_BAND * 0.8f)
                        continue;  // only near coast

                    // Laplacian of SDF = mean curvature
                    float laplacian = curv_sdf[idx - 1] + curv_sdf[idx + 1] + curv_sdf[idx - w] +
                                      curv_sdf[idx + w] - 4.0f * c;
                    // Move SDF by curvature (smooths jagged contours)
                    curv_buf[idx] = c + CURV_DT * laplacian;
                }
            }
            std::swap(curv_sdf, curv_buf);
        }

        // Apply SDF changes back to elevation: where the SDF sign changed,
        // push elevation across water_level. For cells near the zero-crossing,
        // blend elevation toward the new implied coastline position.
        for (uint32_t y = 1; y < h - 1; ++y) {
            for (uint32_t x = 1; x < w - 1; ++x) {
                size_t idx = static_cast<size_t>(y) * w + x;
                float old_sdf = (eroded[idx] - env.water_level) * 8.0f;
                float new_sdf = curv_sdf[idx];

                // Only modify tiles very near the coastline
                if (std::abs(old_sdf) > 2.5f)
                    continue;

                // If sign changed: flip the tile
                if ((old_sdf > 0) != (new_sdf > 0)) {
                    if (new_sdf > 0) {
                        // Was ocean, now land
                        eroded[idx] = env.water_level + 0.005f;
                    } else {
                        // Was land, now ocean
                        eroded[idx] = env.water_level - 0.005f;
                    }
                } else if (std::abs(old_sdf) < 1.5f) {
                    // Near coast: gentle blend toward smoothed position
                    float target = env.water_level + new_sdf / 8.0f;
                    eroded[idx] = eroded[idx] * 0.6f + target * 0.4f;
                }
            }
        }
    }

    // ── 11. Ocean floor: tectonics-aware bathymetry ─────────────────────────
    // EDT distance from coast + plate boundary data for trenches/ridges.
    {
        // Compute EDT for ocean tiles (distance from nearest land)
        std::vector<float> ocean_dist_coast(total, 0.0f);
        {
            constexpr float INF = 1e10f;
            std::vector<float> dist_sq(total, INF);
            for (uint32_t y = 0; y < h; ++y) {
                for (uint32_t x = 0; x < w; ++x) {
                    size_t idx = static_cast<size_t>(y) * w + x;
                    if (eroded[idx] >= env.water_level) {
                        dist_sq[idx] = 0.0f;
                    }
                }
            }
            std::vector<float> row_buf(std::max(w, h));
            std::vector<int> v_buf(std::max(w, h));
            std::vector<float> z_buf(std::max(w, h) + 1);
            for (uint32_t y = 0; y < h; ++y) {
                for (uint32_t x = 0; x < w; ++x)
                    row_buf[x] = dist_sq[static_cast<size_t>(y) * w + x];
                int k = 0;
                v_buf[0] = 0;
                z_buf[0] = -INF;
                z_buf[1] = INF;
                for (uint32_t q = 1; q < w; ++q) {
                    float s;
                    while (true) {
                        float vk = static_cast<float>(v_buf[k]);
                        float fq = static_cast<float>(q);
                        s = ((row_buf[q] + fq * fq) -
                             (row_buf[static_cast<size_t>(v_buf[k])] + vk * vk)) /
                            (2.0f * fq - 2.0f * vk);
                        if (s > z_buf[k])
                            break;
                        --k;
                    }
                    ++k;
                    v_buf[k] = static_cast<int>(q);
                    z_buf[k] = s;
                    z_buf[k + 1] = INF;
                }
                k = 0;
                for (uint32_t q = 0; q < w; ++q) {
                    while (z_buf[k + 1] < static_cast<float>(q))
                        ++k;
                    float dq = static_cast<float>(q) - static_cast<float>(v_buf[k]);
                    dist_sq[static_cast<size_t>(y) * w + q] =
                        dq * dq + row_buf[static_cast<size_t>(v_buf[k])];
                }
            }
            for (uint32_t x = 0; x < w; ++x) {
                for (uint32_t y = 0; y < h; ++y)
                    row_buf[y] = dist_sq[static_cast<size_t>(y) * w + x];
                int k = 0;
                v_buf[0] = 0;
                z_buf[0] = -INF;
                z_buf[1] = INF;
                for (uint32_t q = 1; q < h; ++q) {
                    float s;
                    while (true) {
                        float vk = static_cast<float>(v_buf[k]);
                        float fq = static_cast<float>(q);
                        s = ((row_buf[q] + fq * fq) -
                             (row_buf[static_cast<size_t>(v_buf[k])] + vk * vk)) /
                            (2.0f * fq - 2.0f * vk);
                        if (s > z_buf[k])
                            break;
                        --k;
                    }
                    ++k;
                    v_buf[k] = static_cast<int>(q);
                    z_buf[k] = s;
                    z_buf[k + 1] = INF;
                }
                k = 0;
                for (uint32_t q = 0; q < h; ++q) {
                    while (z_buf[k + 1] < static_cast<float>(q))
                        ++k;
                    float dq = static_cast<float>(q) - static_cast<float>(v_buf[k]);
                    dist_sq[static_cast<size_t>(q) * w + x] =
                        dq * dq + row_buf[static_cast<size_t>(v_buf[k])];
                }
            }
            for (size_t i = 0; i < total; ++i) {
                if (eroded[i] < env.water_level)
                    ocean_dist_coast[i] = std::sqrt(dist_sq[i]);
            }
        }

        // Compute per-ocean-tile coastal proximity shelf boost
        // Uniform coastal proximity is used as the shelf boost proxy since
        // dynamic rivers are computed by the weather system, not terrain gen.
        std::vector<float> river_shelf_boost(total, 0.0f);
        {
            // For each ocean tile adjacent to land, mark it as a coastal tile
            // and propagate outward with decay to create the shelf gradient
            std::vector<float> coastal_flow(total, 0.0f);
            for (uint32_t y = 1; y < h - 1; ++y) {
                for (uint32_t x = 1; x < w - 1; ++x) {
                    size_t idx = static_cast<size_t>(y) * w + x;
                    if (eroded[idx] >= env.water_level)
                        continue;  // ocean tile
                    // Check if adjacent to land
                    for (int dy = -1; dy <= 1; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            if (!dx && !dy)
                                continue;
                            size_t nidx = static_cast<size_t>(y + dy) * w + (x + dx);
                            if (eroded[nidx] >= env.water_level) {
                                coastal_flow[idx] = std::max(coastal_flow[idx], 1.0f);
                            }
                        }
                    }
                }
            }
            // BFS propagation of shelf boost from coast outward
            std::deque<size_t> shelf_q;
            for (size_t i = 0; i < total; ++i) {
                if (coastal_flow[i] > 0.0f)
                    shelf_q.push_back(i);
            }
            while (!shelf_q.empty()) {
                size_t cur = shelf_q.front();
                shelf_q.pop_front();
                auto cx = static_cast<uint32_t>(cur % w);
                auto cy = static_cast<uint32_t>(cur / w);
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (!dx && !dy)
                            continue;
                        int nx = static_cast<int>(cx) + dx;
                        int ny = static_cast<int>(cy) + dy;
                        if (nx < 0 || nx >= static_cast<int>(w) || ny < 0 ||
                            ny >= static_cast<int>(h))
                            continue;
                        size_t nidx = static_cast<size_t>(ny) * w + static_cast<size_t>(nx);
                        if (eroded[nidx] >= env.water_level)
                            continue;
                        float prop = coastal_flow[cur] * 0.85f;
                        if (prop > coastal_flow[nidx]) {
                            coastal_flow[nidx] = prop;
                            shelf_q.push_back(nidx);
                        }
                    }
                }
            }
            for (size_t i = 0; i < total; ++i)
                river_shelf_boost[i] = std::clamp(coastal_flow[i] * 3.0f, 0.0f, 8.0f);
        }

        // Multi-scale ocean floor noise
        FastNoiseLite ocean_broad;
        ocean_broad.SetSeed(static_cast<int>(seed + 30000));
        ocean_broad.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        ocean_broad.SetFractalType(FastNoiseLite::FractalType_FBm);
        ocean_broad.SetFractalOctaves(3);
        ocean_broad.SetFrequency(0.008f);

        FastNoiseLite ocean_detail;
        ocean_detail.SetSeed(static_cast<int>(seed + 30100));
        ocean_detail.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        ocean_detail.SetFractalType(FastNoiseLite::FractalType_FBm);
        ocean_detail.SetFractalOctaves(4);
        ocean_detail.SetFrequency(0.025f);

        FastNoiseLite ocean_ridge;
        ocean_ridge.SetSeed(static_cast<int>(seed + 30200));
        ocean_ridge.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        ocean_ridge.SetFractalType(FastNoiseLite::FractalType_Ridged);
        ocean_ridge.SetFractalOctaves(2);
        ocean_ridge.SetFrequency(0.012f);

        FastNoiseLite shelf_noise;
        shelf_noise.SetSeed(static_cast<int>(seed + 30300));
        shelf_noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        shelf_noise.SetFractalType(FastNoiseLite::FractalType_FBm);
        shelf_noise.SetFractalOctaves(2);
        shelf_noise.SetFrequency(0.012f);

        float max_ocean_dist = 1.0f;
        for (size_t i = 0; i < total; ++i)
            max_ocean_dist = std::max(max_ocean_dist, ocean_dist_coast[i]);

        for (uint32_t y = 0; y < h; ++y) {
            for (uint32_t x = 0; x < w; ++x) {
                size_t idx = static_cast<size_t>(y) * w + x;
                if (eroded[idx] >= env.water_level)
                    continue;
                float fx = static_cast<float>(x);
                float fy = static_cast<float>(y);
                float dist = ocean_dist_coast[idx];

                // Tectonically-aware shelf width:
                // - Convergent margins (active): narrow shelf (2-6 tiles)
                // - Passive margins (far from convergence): wide shelf (8-20 tiles)
                // - River sediment: widens shelf near river mouths
                // - Noise variation along coast
                float sn = shelf_noise.GetNoise(fx, fy);
                float conv = convergence_field[idx];
                float bd = boundary_dist[idx];
                uint32_t pid = plate_id[idx];
                uint32_t sid = second_plate_id[idx];
                bool pa_cont = plates[pid].is_continental;
                bool pb_cont = plates[sid].is_continental;

                // Active vs passive margin: convergent boundaries get narrow shelf
                float margin_type = std::clamp(1.0f - std::abs(conv) * 3.0f, 0.0f, 1.0f);
                // Base shelf: 3 tiles (active) to 12 tiles (passive)
                float base_shelf = 3.0f + margin_type * 9.0f;
                float local_shelf_width = base_shelf + sn * 5.0f + river_shelf_boost[idx];
                local_shelf_width = std::clamp(local_shelf_width, 1.5f, 22.0f);
                float local_slope_width = local_shelf_width * 1.5f;

                float trench_depth = 0.0f;
                if (conv > 0.1f && bd < 25.0f) {
                    // Convergent boundary in ocean → subduction trench
                    if (pa_cont != pb_cont || (!pa_cont && !pb_cont)) {
                        float trench_falloff = std::exp(-bd * bd / (12.0f * 12.0f));
                        float trench_strength = conv * trench_falloff;
                        // Along-strike variation so trenches aren't uniform
                        float strike_x = boundary_tx[idx] * fx + boundary_ty[idx] * fy;
                        float trench_var =
                            0.6f + 0.4f * crust_noise.GetNoise(strike_x * 0.008f, bd * 0.02f);
                        trench_depth = 0.12f * trench_strength * trench_var;
                    }
                }

                float target_depth;
                // Shelf starts well below water level to avoid "glow ring" artifact
                float shelf_start = env.water_level - 0.035f;  // 3.5% below water level
                float shelf_depth_range = 0.04f + margin_type * 0.04f;
                if (dist < local_shelf_width) {
                    float t = dist / local_shelf_width;
                    t = t * t * (3.0f - 2.0f * t);
                    target_depth = shelf_start - t * shelf_depth_range;
                } else if (dist < local_shelf_width + local_slope_width) {
                    float t = (dist - local_shelf_width) / local_slope_width;
                    t = t * t;
                    float shelf_base = shelf_start - shelf_depth_range;
                    target_depth = shelf_base - t * 0.25f;
                } else {
                    float excess = dist - local_shelf_width - local_slope_width;
                    float t =
                        std::clamp(excess / std::max(max_ocean_dist * 0.5f, 1.0f), 0.0f, 1.0f);
                    target_depth = env.water_level - 0.30f - t * 0.08f;
                }

                // Apply tectonic trench
                target_depth -= trench_depth;

                // Multi-scale noise for natural variation
                float n_broad = ocean_broad.GetNoise(fx, fy) * 0.035f;
                float n_detail = ocean_detail.GetNoise(fx, fy) * 0.012f;
                float deep_frac = std::clamp((dist - local_shelf_width - local_slope_width) /
                                                 std::max(max_ocean_dist * 0.3f, 1.0f),
                                             0.0f, 1.0f);
                float ridge = ocean_ridge.GetNoise(fx, fy) * 0.04f * deep_frac;

                eroded[idx] = target_depth + n_broad + n_detail + ridge;
                eroded[idx] = std::clamp(eroded[idx], 0.0f, env.water_level - 0.005f);
            }
        }
    }

    // Write back final heights
    for (size_t i = 0; i < total; ++i) {
        height_field[i] = std::clamp(eroded[i], 0.0f, 1.0f);
    }
}

// ── Ocean flood-fill ────────────────────────────────────────────────────────

static void compute_ocean(const std::vector<float>& heights, uint32_t w, uint32_t h,
                          float water_level, std::vector<bool>& is_ocean) {
    size_t size = static_cast<size_t>(w) * h;
    is_ocean.assign(size, false);

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
        if (ocean) {
            for (size_t idx : components[c]) {
                is_ocean[idx] = true;
            }
        }
        // Non-ocean water bodies are just low-elevation land (basins for weather to fill)
    }
}

// ── Slope + Aspect + ElevBand from unified height ───────────────────────────

static void compute_slope_aspect_bands(const std::vector<float>& heights, uint32_t w, uint32_t h,
                                       float water_level, std::vector<float>& slope,
                                       std::vector<float>& aspect, std::vector<ElevBand>& bands) {
    // First pass: compute slope, aspect, and local relief
    constexpr int RELIEF_RADIUS = 4;
    std::vector<float> local_relief(static_cast<size_t>(w) * h, 0.0f);

    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            size_t idx = static_cast<size_t>(y) * w + x;
            float ht = heights[idx];

            // Gradient
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
            aspect[idx] = std::atan2(dy_val, dx);

            // Local relief: max - min in neighborhood
            float local_min = ht, local_max = ht;
            int y0 = std::max(0, static_cast<int>(y) - RELIEF_RADIUS);
            int y1 = std::min(static_cast<int>(h) - 1, static_cast<int>(y) + RELIEF_RADIUS);
            int x0 = std::max(0, static_cast<int>(x) - RELIEF_RADIUS);
            int x1 = std::min(static_cast<int>(w) - 1, static_cast<int>(x) + RELIEF_RADIUS);
            for (int ny = y0; ny <= y1; ++ny) {
                for (int nx = x0; nx <= x1; ++nx) {
                    float nht = heights[static_cast<size_t>(ny) * w + static_cast<size_t>(nx)];
                    local_min = std::min(local_min, nht);
                    local_max = std::max(local_max, nht);
                }
            }
            local_relief[idx] = local_max - local_min;
        }
    }

    // Second pass: classify bands from elevation + slope + local relief
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            size_t idx = static_cast<size_t>(y) * w + x;
            float ht = heights[idx];
            float sl = slope[idx];
            float relief = local_relief[idx];

            if (ht < water_level) {
                bands[idx] = ElevBand::Water;
            } else if (relief > 0.22f && sl > 0.18f) {
                // Mountains: high local relief + steep slopes
                bands[idx] = ElevBand::Mountains;
            } else if (relief > 0.12f || (sl > 0.12f && ht > water_level + 0.12f)) {
                // Hills: moderate relief or moderate slope on elevated terrain
                bands[idx] = ElevBand::Hills;
            } else {
                bands[idx] = ElevBand::Lowland;
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
        float s =
            ((f[q] + static_cast<float>(q) * static_cast<float>(q)) -
             (f[v[static_cast<size_t>(k)]] + static_cast<float>(v[static_cast<size_t>(k)]) *
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
                    if (nx < 0 || nx >= static_cast<int>(w) || ny < 0 || ny >= static_cast<int>(h))
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

// ── Geology (bedrock + soil) ────────────────────────────────────────────────

static void compute_geology(
    std::vector<RockType>& rock_field, std::vector<float>& hardness_field,
    std::vector<float>& permeability_field, std::vector<SoilTexture>& soil_tex_field,
    std::vector<float>& soil_depth_field, std::vector<float>& porosity_field,
    std::vector<float>& ksat_field, std::vector<float>& field_cap_field,
    std::vector<float>& wilt_field, std::vector<float>& erodibility_field,
    std::vector<float>& rock_blend_field, std::vector<float>& soil_blend_field,
    std::vector<float>& tectonic_activity_field, const std::vector<float>& heights,
    const std::vector<float>& slopes, const std::vector<ElevBand>& bands,
    const std::vector<bool>& ocean_flags, const std::vector<float>& dist_ocean, uint32_t w,
    uint32_t h, uint32_t seed) {
    size_t size = static_cast<size_t>(w) * h;
    rock_field.resize(size);
    hardness_field.resize(size);
    permeability_field.resize(size);
    soil_tex_field.resize(size);
    soil_depth_field.resize(size);
    porosity_field.resize(size);
    ksat_field.resize(size);
    field_cap_field.resize(size);
    wilt_field.resize(size);
    erodibility_field.resize(size);
    rock_blend_field.resize(size);
    soil_blend_field.resize(size);
    tectonic_activity_field.resize(size);

    // Spatial variation noise for rock type assignment — broad provinces
    FastNoiseLite rock_noise;
    rock_noise.SetSeed(static_cast<int>(seed + 20000));
    rock_noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    rock_noise.SetFrequency(0.004f);  // ~250 tile wavelength for province-scale lithology
    rock_noise.SetFractalType(FastNoiseLite::FractalType_FBm);
    rock_noise.SetFractalOctaves(2);

    // Secondary noise for property variation
    FastNoiseLite var_noise;
    var_noise.SetSeed(static_cast<int>(seed + 20500));
    var_noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    var_noise.SetFrequency(0.008f);  // medium-scale variation
    var_noise.SetFractalType(FastNoiseLite::FractalType_FBm);
    var_noise.SetFractalOctaves(2);

    // Fine lithology noise — adds local variation within provinces
    FastNoiseLite fine_rock_noise;
    fine_rock_noise.SetSeed(static_cast<int>(seed + 20100));
    fine_rock_noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    fine_rock_noise.SetFrequency(0.015f);  // ~65 tile wavelength

    // Bedrock properties lookup: hardness, permeability
    // Granite:     0.85, 0.10
    // Basalt:      0.75, 0.20
    // Limestone:   0.45, 0.70
    // Sandstone:   0.35, 0.80
    // Shale:       0.25, 0.05
    // Metamorphic: 0.90, 0.08
    auto rock_hardness = [](RockType r) -> float {
        switch (r) {
            case RockType::Granite:
                return 0.85f;
            case RockType::Basalt:
                return 0.75f;
            case RockType::Limestone:
                return 0.45f;
            case RockType::Sandstone:
                return 0.35f;
            case RockType::Shale:
                return 0.25f;
            case RockType::Metamorphic:
                return 0.90f;
            default:
                return 0.50f;
        }
    };
    auto rock_permeability = [](RockType r) -> float {
        switch (r) {
            case RockType::Granite:
                return 0.10f;
            case RockType::Basalt:
                return 0.20f;
            case RockType::Limestone:
                return 0.70f;
            case RockType::Sandstone:
                return 0.80f;
            case RockType::Shale:
                return 0.05f;
            case RockType::Metamorphic:
                return 0.08f;
            default:
                return 0.30f;
        }
    };

    // Soil texture hydraulic properties lookup:
    // Sand:  porosity=0.38, ksat=1e-4, field_cap=0.12, wilt=0.05
    // Loam:  porosity=0.45, ksat=5e-6, field_cap=0.30, wilt=0.12
    // Silt:  porosity=0.48, ksat=2e-6, field_cap=0.35, wilt=0.15
    // Clay:  porosity=0.52, ksat=5e-8, field_cap=0.42, wilt=0.25
    // Peat:  porosity=0.75, ksat=1e-5, field_cap=0.55, wilt=0.20
    struct SoilProps {
        float porosity;
        float ksat;
        float field_cap;
        float wilt;
    };
    auto soil_props = [](SoilTexture s) -> SoilProps {
        switch (s) {
            case SoilTexture::Sand:
                return {0.38f, 1e-4f, 0.12f, 0.05f};
            case SoilTexture::Loam:
                return {0.45f, 5e-6f, 0.30f, 0.12f};
            case SoilTexture::Silt:
                return {0.48f, 2e-6f, 0.35f, 0.15f};
            case SoilTexture::Clay:
                return {0.52f, 5e-8f, 0.42f, 0.25f};
            case SoilTexture::Peat:
                return {0.75f, 1e-5f, 0.55f, 0.20f};
            default:
                return {0.45f, 5e-6f, 0.30f, 0.12f};
        }
    };

    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            size_t idx = static_cast<size_t>(y) * w + x;

            // Ocean tiles: defaults are fine, skip complex logic
            if (ocean_flags[idx]) {
                rock_field[idx] = RockType::Basalt;
                hardness_field[idx] = 0.75f;
                permeability_field[idx] = 0.20f;
                soil_tex_field[idx] = SoilTexture::Clay;
                soil_depth_field[idx] = 0.0f;
                porosity_field[idx] = 0.52f;
                ksat_field[idx] = 5e-8f;
                field_cap_field[idx] = 0.42f;
                wilt_field[idx] = 0.25f;
                erodibility_field[idx] = 0.0f;
                continue;
            }

            float fx = static_cast<float>(x);
            float fy = static_cast<float>(y);

            // Province-scale rock noise [0, 1] — very broad lithological provinces
            float rn = rock_noise.GetNoise(fx, fy) * 0.5f + 0.5f;
            // Fine-scale noise for local variation within provinces
            float fn = fine_rock_noise.GetNoise(fx, fy) * 0.5f + 0.5f;
            // Combined: broad province + 20% local variation
            float rn_combined = std::clamp(rn * 0.80f + fn * 0.20f, 0.0f, 1.0f);
            float vn = var_noise.GetNoise(fx, fy);  // [-1, 1]

            ElevBand band = bands[idx];
            float d_ocean = dist_ocean[idx];
            float slope = slopes[idx];

            // Tectonic activity: high near mountains, moderate at hills, low in lowlands
            float activity = 0.0f;
            if (band == ElevBand::Mountains)
                activity = 0.7f + slope * 0.6f;
            else if (band == ElevBand::Hills)
                activity = 0.3f + slope * 0.4f;
            else
                activity = slope * 0.3f;
            tectonic_activity_field[idx] = std::clamp(activity, 0.0f, 1.0f);

            // ── Rock type assignment ────────────────────────────────────
            // rn_combined is the province-scale lithology noise, stored as rock_blend
            // for the renderer to use for smooth color transitions
            rock_blend_field[idx] = rn_combined;

            RockType rock;
            if (band == ElevBand::Mountains) {
                // Mountains: primarily Metamorphic and Granite
                if (rn_combined < 0.45f)
                    rock = RockType::Metamorphic;
                else if (rn_combined < 0.80f)
                    rock = RockType::Granite;
                else
                    rock = RockType::Basalt;  // volcanic intrusions
            } else if (band == ElevBand::Hills) {
                // Hills: mix of Granite, Sandstone, Limestone
                if (rn_combined < 0.30f)
                    rock = RockType::Granite;
                else if (rn_combined < 0.60f)
                    rock = RockType::Sandstone;
                else if (rn_combined < 0.85f)
                    rock = RockType::Limestone;
                else
                    rock = RockType::Metamorphic;
            } else {
                // Lowland / Water band: softer sedimentary rocks
                if (d_ocean < 15.0f) {
                    // Near-coast: more Shale and Sandstone (sedimentary deposits)
                    if (rn_combined < 0.40f)
                        rock = RockType::Shale;
                    else if (rn_combined < 0.70f)
                        rock = RockType::Sandstone;
                    else if (rn_combined < 0.90f)
                        rock = RockType::Limestone;
                    else
                        rock = RockType::Granite;  // rare shield exposure
                } else {
                    // Interior lowland: sedimentary basins
                    if (rn_combined < 0.30f)
                        rock = RockType::Sandstone;
                    else if (rn_combined < 0.55f)
                        rock = RockType::Limestone;
                    else if (rn_combined < 0.80f)
                        rock = RockType::Shale;
                    else
                        rock = RockType::Granite;  // craton core
                }
            }
            rock_field[idx] = rock;

            // ── Bedrock properties with ±10% noise variation ────────────
            float base_hard = rock_hardness(rock);
            float base_perm = rock_permeability(rock);
            float var10 = vn * 0.10f;  // ±10%
            hardness_field[idx] = std::clamp(base_hard + base_hard * var10, 0.0f, 1.0f);
            permeability_field[idx] = std::clamp(base_perm + base_perm * var10, 0.0f, 1.0f);

            // ── Soil texture from rock type + elevation + slope ─────────
            SoilTexture soil;
            if (rock == RockType::Sandstone || (band == ElevBand::Mountains && slope > 0.3f)) {
                soil = SoilTexture::Sand;  // sandy on sandstone or steep eroded mountain
            } else if (rock == RockType::Shale || (band == ElevBand::Lowland && slope < 0.05f)) {
                soil = SoilTexture::Clay;  // clay on shale or flat lowlands
            } else if (rock == RockType::Limestone || (slope < 0.08f && heights[idx] < 0.45f)) {
                soil = SoilTexture::Silt;  // silt on limestone or low-slope low-elevation
            } else if (slope < 0.03f && heights[idx] < 0.38f && d_ocean > 20.0f) {
                soil = SoilTexture::Peat;  // very flat interior basins
            } else {
                soil = SoilTexture::Loam;  // moderate conditions default
            }
            soil_tex_field[idx] = soil;
            // Store continuous blend value for smooth rendering transitions
            // Mix of rock noise + height + slope to give spatial continuity
            soil_blend_field[idx] =
                std::clamp(rn_combined * 0.4f + heights[idx] * 0.3f + (1.0f - slope) * 0.3f, 0.0f,
                           1.0f);

            // ── Soil depth (meters) ─────────────────────────────────────
            float depth;
            if (band == ElevBand::Mountains) {
                depth = 0.1f + rn * 0.4f;  // 0.1–0.5m, thin
            } else if (band == ElevBand::Hills) {
                depth = 0.3f + rn * 1.2f;  // 0.3–1.5m
            } else {
                depth = 1.5f + rn * 1.5f;  // 1.5–3.0m, deep lowland
            }
            // Steeper slope reduces soil depth (erosion strips it)
            depth *= std::clamp(1.0f - slope * 2.0f, 0.2f, 1.0f);
            soil_depth_field[idx] = std::clamp(depth, 0.05f, 3.0f);

            // ── Derived hydraulic properties with ±5% noise ─────────────
            SoilProps sp = soil_props(soil);
            float var5 = vn * 0.05f;  // ±5%
            porosity_field[idx] = std::clamp(sp.porosity + sp.porosity * var5, 0.25f, 0.80f);
            ksat_field[idx] = sp.ksat * (1.0f + var5);  // var5 in [-0.05, 0.05], always positive
            field_cap_field[idx] = std::clamp(sp.field_cap + sp.field_cap * var5, 0.05f, 0.65f);
            wilt_field[idx] = std::clamp(sp.wilt + sp.wilt * var5, 0.02f, 0.35f);

            // ── Erodibility: inverse hardness, modulated by soil depth ──
            float erod = (1.0f - hardness_field[idx]);
            // Deeper soil = more erodible surface material
            float depth_factor = std::clamp(soil_depth_field[idx] / 2.0f, 0.3f, 1.0f);
            erodibility_field[idx] = std::clamp(erod * depth_factor, 0.0f, 1.0f);
        }
    }
}

// ── Terrain generation ──────────────────────────────────────────────────────

Terrain generate_terrain(uint32_t width, uint32_t height, const EnvParams& env, uint32_t seed,
                         GenerationTimings* timings) {
    auto total_start = Clock::now();
    size_t size = static_cast<size_t>(width) * height;

    std::vector<float> height_field(size);
    std::vector<float> ridge_field(size);
    std::vector<bool> ocean_flags;
    std::vector<float> slope_field(size);
    std::vector<float> aspect_field(size);
    std::vector<ElevBand> band_field(size);
    std::vector<float> dist_ocean_field;
    std::vector<float> roughness_field(size);

    // Geology fields
    std::vector<RockType> rock_field;
    std::vector<float> hardness_field;
    std::vector<float> permeability_field;
    std::vector<SoilTexture> soil_tex_field;
    std::vector<float> soil_depth_field;
    std::vector<float> porosity_field;
    std::vector<float> ksat_field;
    std::vector<float> field_cap_field;
    std::vector<float> wilt_field;
    std::vector<float> erodibility_field;
    std::vector<float> rock_blend_field;
    std::vector<float> soil_blend_field;
    std::vector<float> tectonic_activity_field;

    auto t0 = Clock::now();

    if (env.plate_count > 0) {
        // Tectonic mode: plates → boundaries → terrain
        generate_tectonic_terrain(height_field, ridge_field, width, height, seed, env);
        if (timings) {
            timings->height_ms = elapsed_ms(t0);
            timings->ridge_ms = 0.0;
            timings->unify_ms = 0.0;
        }
    } else {
        // Legacy noise mode
        // 1. Height (continental noise with domain warping)
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
    }

    // 4. Ocean classification (no lakes — basins left for weather system to fill)
    t0 = Clock::now();
    compute_ocean(height_field, width, height, env.water_level, ocean_flags);
    if (timings)
        timings->ocean_ms = elapsed_ms(t0);

    // 5. Slope + Aspect + ElevBand (from unified height)
    t0 = Clock::now();
    compute_slope_aspect_bands(height_field, width, height, env.water_level, slope_field,
                               aspect_field, band_field);
    if (timings)
        timings->slope_band_ms = elapsed_ms(t0);

    // 6. EDT for dist_ocean only (no dist_water)
    t0 = Clock::now();
    compute_edt(ocean_flags, width, height, dist_ocean_field);
    if (timings)
        timings->dist_fields_ms = elapsed_ms(t0);

    // 7. Geology (bedrock + soil from tectonic context)
    t0 = Clock::now();
    compute_geology(rock_field, hardness_field, permeability_field, soil_tex_field,
                    soil_depth_field, porosity_field, ksat_field, field_cap_field, wilt_field,
                    erodibility_field, rock_blend_field, soil_blend_field,
                    tectonic_activity_field, height_field, slope_field, band_field, ocean_flags,
                    dist_ocean_field, width, height, seed);
    if (timings)
        timings->geology_ms = elapsed_ms(t0);

    // 8. Roughness (3x3 stddev of unified height)
    t0 = Clock::now();
    compute_roughness(roughness_field, height_field, width, height);
    if (timings)
        timings->roughness_ms = elapsed_ms(t0);

    // Assemble tiles
    std::vector<TerrainTile> tiles(size);
    for (size_t i = 0; i < size; ++i) {
        tiles[i] = TerrainTile{
            .elev01 = height_field[i],
            .band = band_field[i],
            .is_ocean = ocean_flags[i],
            .slope01 = slope_field[i],
            .aspect = aspect_field[i],
            .dist_ocean = dist_ocean_field[i],
            .roughness = roughness_field[i],
            .elevation = ridge_field[i],
            .rock = rock_field[i],
            .bedrock_hardness = hardness_field[i],
            .bedrock_permeability = permeability_field[i],
            .rock_blend = rock_blend_field[i],
            .tectonic_activity = tectonic_activity_field[i],
            .soil = soil_tex_field[i],
            .soil_blend = soil_blend_field[i],
            .soil_depth = soil_depth_field[i],
            .porosity = porosity_field[i],
            .ksat = ksat_field[i],
            .field_capacity = field_cap_field[i],
            .wilting_point = wilt_field[i],
            .erodibility = erodibility_field[i],
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
