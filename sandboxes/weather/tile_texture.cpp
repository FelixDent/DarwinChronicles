#include "tile_texture.h"

#include <FastNoiseLite.h>

#include <algorithm>
#include <cmath>

namespace sandbox {

namespace {

// ── Color helpers ──────────────────────────────────────────────────────────

struct Col3 {
    float r, g, b;

    Col3 operator*(float s) const { return {r * s, g * s, b * s}; }
    Col3 operator+(Col3 o) const { return {r + o.r, g + o.g, b + o.b}; }
};

Col3 lerp(Col3 a, Col3 b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

Col3 clamp_col(Col3 c) {
    return {std::clamp(c.r, 0.0f, 255.0f), std::clamp(c.g, 0.0f, 255.0f),
            std::clamp(c.b, 0.0f, 255.0f)};
}

// ── Heightfield sampling ───────────────────────────────────────────────────

float sample_elev(const Terrain& terrain, float fx, float fy) {
    int w = static_cast<int>(terrain.width);
    int h = static_cast<int>(terrain.height);
    fx = std::clamp(fx, 0.0f, static_cast<float>(w - 1));
    fy = std::clamp(fy, 0.0f, static_cast<float>(h - 1));

    int x0 = static_cast<int>(fx);
    int y0 = static_cast<int>(fy);
    int x1 = std::min(x0 + 1, w - 1);
    int y1 = std::min(y0 + 1, h - 1);
    float sx = fx - static_cast<float>(x0);
    float sy = fy - static_cast<float>(y0);

    float e00 = terrain.tile_at(static_cast<uint32_t>(x0), static_cast<uint32_t>(y0)).elev01;
    float e10 = terrain.tile_at(static_cast<uint32_t>(x1), static_cast<uint32_t>(y0)).elev01;
    float e01 = terrain.tile_at(static_cast<uint32_t>(x0), static_cast<uint32_t>(y1)).elev01;
    float e11 = terrain.tile_at(static_cast<uint32_t>(x1), static_cast<uint32_t>(y1)).elev01;

    return (e00 * (1 - sx) + e10 * sx) * (1 - sy) + (e01 * (1 - sx) + e11 * sx) * sy;
}

float sample_field(const Terrain& terrain, float fx, float fy, float TerrainTile::*field) {
    int w = static_cast<int>(terrain.width);
    int h = static_cast<int>(terrain.height);
    fx = std::clamp(fx, 0.0f, static_cast<float>(w - 1));
    fy = std::clamp(fy, 0.0f, static_cast<float>(h - 1));

    int x0 = static_cast<int>(fx);
    int y0 = static_cast<int>(fy);
    int x1 = std::min(x0 + 1, w - 1);
    int y1 = std::min(y0 + 1, h - 1);
    float sx = fx - static_cast<float>(x0);
    float sy = fy - static_cast<float>(y0);

    float v00 = terrain.tile_at(static_cast<uint32_t>(x0), static_cast<uint32_t>(y0)).*field;
    float v10 = terrain.tile_at(static_cast<uint32_t>(x1), static_cast<uint32_t>(y0)).*field;
    float v01 = terrain.tile_at(static_cast<uint32_t>(x0), static_cast<uint32_t>(y1)).*field;
    float v11 = terrain.tile_at(static_cast<uint32_t>(x1), static_cast<uint32_t>(y1)).*field;

    return (v00 * (1 - sx) + v10 * sx) * (1 - sy) + (v01 * (1 - sx) + v11 * sx) * sy;
}

void compute_gradient(const Terrain& terrain, float fx, float fy, float& slope, float& aspect) {
    constexpr float eps = 0.25f;
    float dzdx = sample_elev(terrain, fx + eps, fy) - sample_elev(terrain, fx - eps, fy);
    float dzdy = sample_elev(terrain, fx, fy + eps) - sample_elev(terrain, fx, fy - eps);
    dzdx /= (2.0f * eps);
    dzdy /= (2.0f * eps);

    slope = std::sqrt(dzdx * dzdx + dzdy * dzdy);
    aspect = std::atan2(-dzdy, -dzdx);
}

float compute_curvature(const Terrain& terrain, float fx, float fy) {
    constexpr float eps = 0.4f;
    float c = sample_elev(terrain, fx, fy);
    float n = sample_elev(terrain, fx, fy - eps);
    float s = sample_elev(terrain, fx, fy + eps);
    float e = sample_elev(terrain, fx + eps, fy);
    float w = sample_elev(terrain, fx - eps, fy);
    return ((n + s + e + w) * 0.25f - c) / (eps * eps);
}

// ── Continuous geology color ramps ─────────────────────────────────────────
// Instead of discrete rock_base_color(enum), these produce smooth color
// gradients across the rock_blend value for each elevation context.
// This eliminates hard lithological boundaries.

// Mountain province: medium-bright base colors so hillshade creates clear 3D relief.
// The hillshade (light/shadow) IS the primary elevation signal — rock color is secondary tinting.
Col3 mountain_rock_color(float blend) {
    Col3 metamorphic = {145, 148, 162};  // cool blue-grey
    Col3 granite = {185, 172, 158};      // warm tan-grey
    Col3 basalt = {118, 112, 115};       // medium dark volcanic
    Col3 slate = {155, 160, 168};        // cool light grey
    if (blend < 0.35f)
        return lerp(metamorphic, granite, blend / 0.35f);
    else if (blend < 0.70f)
        return lerp(granite, basalt, (blend - 0.35f) / 0.35f);
    else
        return lerp(basalt, slate, (blend - 0.70f) / 0.30f);
}

// Hill province: Sandstone warm (0.0) → Limestone (0.35) → Granite (0.65) → Shale blue (1.0)
Col3 hill_rock_color(float blend) {
    Col3 sandstone = {185, 152, 95};   // warm amber
    Col3 limestone = {205, 198, 172};  // cream (slightly muted)
    Col3 granite = {145, 132, 125};    // grey-pink
    Col3 shale = {92, 98, 112};        // blue-grey
    if (blend < 0.35f)
        return lerp(sandstone, limestone, blend / 0.35f);
    else if (blend < 0.65f)
        return lerp(limestone, granite, (blend - 0.35f) / 0.30f);
    else
        return lerp(granite, shale, (blend - 0.65f) / 0.35f);
}

// Lowland (coastal): Sandy tan (0.0) → Pale limestone (0.4) → Clay ochre (0.75) → Shale (1.0)
Col3 coastal_lowland_rock_color(float blend) {
    Col3 sand = {215, 198, 155};       // warm sandy
    Col3 limestone = {225, 218, 195};  // pale cream
    Col3 clay_ochre = {185, 145, 95};  // warm ochre
    Col3 shale = {115, 108, 98};       // dark grey
    if (blend < 0.40f)
        return lerp(sand, limestone, blend / 0.40f);
    else if (blend < 0.75f)
        return lerp(limestone, clay_ochre, (blend - 0.40f) / 0.35f);
    else
        return lerp(clay_ochre, shale, (blend - 0.75f) / 0.25f);
}

// Lowland (interior): Warm sandstone (0.0) → Red-brown (0.35) → Limestone (0.65) → Grey-green
// (1.0)
Col3 interior_lowland_rock_color(float blend) {
    Col3 sandstone = {210, 178, 120};   // amber sandstone
    Col3 red_brown = {172, 118, 75};    // laterite / red earth
    Col3 limestone = {210, 205, 182};   // warm cream
    Col3 grey_green = {138, 142, 128};  // weathered grey-green
    if (blend < 0.35f)
        return lerp(sandstone, red_brown, blend / 0.35f);
    else if (blend < 0.65f)
        return lerp(red_brown, limestone, (blend - 0.35f) / 0.30f);
    else
        return lerp(limestone, grey_green, (blend - 0.65f) / 0.35f);
}

// Continuous soil color ramp from soil_blend
Col3 continuous_soil_color(float blend, float dist_ocean) {
    Col3 sand = {218, 205, 165};
    Col3 loam = {148, 118, 72};
    Col3 silt = {165, 155, 132};
    Col3 clay = {168, 115, 72};
    Col3 peat = {62, 48, 32};

    // Coastal soils: sand → silt → clay
    // Interior soils: loam → clay → peat
    float coastal = std::clamp(1.0f - dist_ocean * 0.06f, 0.0f, 1.0f);
    Col3 coastal_col;
    if (blend < 0.4f)
        coastal_col = lerp(sand, silt, blend / 0.4f);
    else
        coastal_col = lerp(silt, clay, (blend - 0.4f) / 0.6f);

    Col3 interior_col;
    if (blend < 0.3f)
        interior_col = lerp(loam, silt, blend / 0.3f);
    else if (blend < 0.7f)
        interior_col = lerp(silt, clay, (blend - 0.3f) / 0.4f);
    else
        interior_col = lerp(clay, peat, (blend - 0.7f) / 0.3f);

    return lerp(interior_col, coastal_col, coastal);
}

// ── Noise generators ───────────────────────────────────────────────────────

struct NoiseSet {
    FastNoiseLite terrain_var;   // broad terrain color variation
    FastNoiseLite detail;        // fine-grain detail
    FastNoiseLite medium;        // medium-scale texture
    FastNoiseLite rock_tex;      // rock texture / grain direction
    FastNoiseLite rock_strata;   // sedimentary layering
    FastNoiseLite snow_n;        // snow patchiness
    FastNoiseLite ocean_n;       // ocean surface
    FastNoiseLite shelf_n;       // shelf variation
    FastNoiseLite dust_n;        // aeolian dust / weathering stain
    FastNoiseLite scree_n;       // talus/scree patches
    FastNoiseLite coast_n;       // high-freq coastline perturbation
    FastNoiseLite strata_dir;    // directional strata orientation
    FastNoiseLite warp_x;        // domain warp for ridge branching
    FastNoiseLite warp_y;        // domain warp for ridge branching
    FastNoiseLite ridge_main;    // primary ridged noise (mountain ranges)
    FastNoiseLite ridge_sec;     // secondary ridged noise (spurs)
    FastNoiseLite ridge_fine;    // fine ridged noise (peaks)

    explicit NoiseSet(uint32_t seed) {
        auto setup = [](FastNoiseLite& n, int s, float freq, int octaves,
                        FastNoiseLite::NoiseType type = FastNoiseLite::NoiseType_OpenSimplex2) {
            n.SetNoiseType(type);
            n.SetSeed(s);
            n.SetFrequency(freq);
            if (octaves > 1) {
                n.SetFractalType(FastNoiseLite::FractalType_FBm);
                n.SetFractalOctaves(octaves);
            }
        };

        auto s = static_cast<int>(seed);
        setup(terrain_var, s + 50000, 0.003f, 3);
        setup(detail, s + 50001, 0.06f, 2);
        setup(medium, s + 50002, 0.015f, 3);
        setup(rock_tex, s + 50004, 0.08f, 2);
        setup(rock_strata, s + 50009, 0.025f, 2);
        setup(snow_n, s + 50005, 0.012f, 3);
        setup(ocean_n, s + 50006, 0.03f, 2);
        setup(shelf_n, s + 50007, 0.018f, 3);
        setup(dust_n, s + 50008, 0.008f, 3);
        setup(scree_n, s + 50010, 0.04f, 2);
        setup(coast_n, s + 50011, 0.045f, 3);    // mid-freq for organic coastlines (no speckle)
        setup(strata_dir, s + 50012, 0.005f, 2);  // very broad orientation field
        // Domain warp fields — FBm for smooth, organic warping
        setup(warp_x, s + 50020, 0.0015f, 3);  // very broad warp (~40 tile wavelength)
        setup(warp_y, s + 50021, 0.0015f, 3);
        // Ridged noise — much lower frequency for large connected ridgelines
        setup(ridge_main, s + 50030, 0.010f, 3);  // ~100 tile wavelength (range-scale)
        setup(ridge_sec, s + 50031, 0.022f, 2);   // ~45 tile wavelength (major spurs)
        setup(ridge_fine, s + 50032, 0.055f, 2);  // ~18 tile wavelength (minor spurs)
    }
};

// ── Hillshade ──────────────────────────────────────────────────────────────

float hillshade(float slope, float aspect, float exaggeration = 8.0f) {
    constexpr float sun_az = 5.497787f;  // 315° NW
    constexpr float sun_alt = 0.6981f;   // 40°
    float slope_rad = std::atan(slope * exaggeration);
    float shade = std::cos(slope_rad) * std::sin(sun_alt) +
                  std::sin(slope_rad) * std::cos(sun_alt) * std::cos(sun_az - aspect);
    return std::clamp(shade * 0.75f + 0.25f, 0.08f, 1.35f);
}

// Smoothstep for sharper transitions
float smoothstep(float edge0, float edge1, float x) {
    float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// ── Ocean colors ───────────────────────────────────────────────────────────

Col3 ocean_color(float depth01, float shelf_noise) {
    float d = std::clamp(depth01, 0.0f, 1.0f);
    float dg = std::pow(d, 0.50f);

    // Smooth depth-based gradient, no slope darkening (avoids tile-grid artifacts)
    float shelf = std::clamp(1.0f - d * 5.0f, 0.0f, 1.0f);

    Col3 deep = {8.0f + dg * 4.0f, 20.0f + dg * 12.0f, 72.0f + (1 - dg) * 28.0f};
    Col3 shallow = {28.0f + shelf_noise * 10.0f, 65.0f + shelf_noise * 8.0f,
                    105.0f + shelf_noise * 5.0f};

    return lerp(deep, shallow, shelf);
}

constexpr Col3 BEACH_SAND = {218, 208, 178};
constexpr Col3 WET_SAND = {178, 168, 138};

// ── Rock texture modifiers per material context ───────────────────────────
// Instead of switching on discrete RockType, apply texture based on
// continuous rock_blend position which implies material character.

Col3 apply_material_texture(Col3 base, float rock_blend, float tectonic_activity,
                            const NoiseSet& ns, float wx, float wy, float slope, float elev,
                            float aspect) {
    float grain = ns.rock_tex.GetNoise(wx, wy);
    float strata = ns.rock_strata.GetNoise(wx, wy);

    // High tectonic activity → more texture contrast (foliated/fractured rock)
    // Keep subtle so province colors dominate at zoom-out
    float tex_intensity = 0.04f + tectonic_activity * 0.08f;
    base = base * (1.0f - tex_intensity + grain * tex_intensity * 2.0f);

    // Directional sedimentary strata in calm zones
    // Creates visible layered banding aligned to a broad orientation field
    if (tectonic_activity < 0.35f && slope < 0.25f && elev < 0.55f) {
        // Get broad-scale strata orientation
        float dir_angle = ns.strata_dir.GetNoise(wx * 0.02f, wy * 0.02f) * 3.14159f;
        float dir_x = std::cos(dir_angle);
        float dir_y = std::sin(dir_angle);
        // Directional layering: project pixel position onto strata direction
        float layer_val =
            std::sin((wx * dir_x + wy * dir_y) * 0.4f + strata * 3.0f) * 0.5f + 0.5f;
        float layer_strength = (0.35f - tectonic_activity) * 0.20f * (1.0f - slope * 4.0f);
        base = base * (1.0f - layer_strength * 0.5f + layer_val * layer_strength);
    }

    // Steep slopes: subtle lighter fracture faces
    if (slope > 0.18f) {
        float fracture = std::clamp((slope - 0.18f) * 3.0f, 0.0f, 0.12f);
        float cross = std::sin(wx * std::cos(aspect + 1.57f) * 0.3f +
                               wy * std::sin(aspect + 1.57f) * 0.3f);
        base = base * (1.0f + fracture * (0.6f + cross * 0.4f));
    }

    // Weathering patina on exposed hard rock at high elevation
    if (elev > 0.48f && tectonic_activity > 0.25f) {
        float patina = ns.dust_n.GetNoise(wx * 1.5f, wy * 1.5f) * 0.5f + 0.5f;
        float strength = std::clamp((elev - 0.48f) * 2.5f, 0.0f, 0.18f);
        Col3 dark_varnish = {90, 80, 68};
        base = lerp(base, dark_varnish, patina * strength);
    }

    return base;
}

// ═══════════════════════════════════════════════════════════════════════════
// Core pixel evaluator — LOD-aware, works at any scale
// ═══════════════════════════════════════════════════════════════════════════
//
// world_x/world_y: position in tile-coordinate space (0..width, 0..height)
// ppt: pixels-per-tile at current zoom (controls which noise octaves are active)
//   Macro (1-4 ppt): only broad noise, simple hillshade
//   Meso (8-16 ppt): ridge structure, material provinces
//   Micro (32-64 ppt): full detail — strata, fractures, scree, weathering

uint32_t eval_pixel_internal(const Terrain& terrain, float world_x, float world_y,
                             float ppt, NoiseSet& ns, float water_level) {
    // Noise coordinates: scale world coords to match the 16-px-per-tile tuning
    // of all noise frequencies. When ppt != 16, we still sample the same
    // world-space noise field — ppt only gates which octaves we evaluate.
    constexpr float BASE_PPT = 16.0f;
    float wx = world_x * BASE_PPT;  // pixel-space x (as if 16 px/tile)
    float wy = world_y * BASE_PPT;  // pixel-space y
    float tile_fx = world_x;
    float tile_fy = world_y;

    // Per-pixel interpolated continuous fields (sample before elevation
    // because we need roughness/tectonic to compute sub-tile detail)
    float dist_ocean = sample_field(terrain, tile_fx, tile_fy, &TerrainTile::dist_ocean);
    float roughness = sample_field(terrain, tile_fx, tile_fy, &TerrainTile::roughness);
    float soil_depth = sample_field(terrain, tile_fx, tile_fy, &TerrainTile::soil_depth);
    float rock_blend = sample_field(terrain, tile_fx, tile_fy, &TerrainTile::rock_blend);
    float soil_blend = sample_field(terrain, tile_fx, tile_fy, &TerrainTile::soil_blend);
    float tectonic =
        sample_field(terrain, tile_fx, tile_fy, &TerrainTile::tectonic_activity);
    float hardness =
        sample_field(terrain, tile_fx, tile_fy, &TerrainTile::bedrock_hardness);

    // Per-pixel elevation from heightfield
    float elev_base = sample_elev(terrain, tile_fx, tile_fy);

    // Sub-tile ridge detail provides ALL visible relief signal at every
    // elevation level. Amplitude increases GRADUALLY with elevation:
    // coast → gentle rolling texture, hills → moderate, mountains → dramatic ridges.
    // This creates visible "steps" along elevation contours at planetary zoom.

    // Continuous elevation ramp starting just above water level
    // At water_level+0.02: amplitude ~0.02 (barely visible rolling texture)
    // At water_level+0.15: amplitude ~0.10 (visible hills)
    // At water_level+0.30: amplitude ~0.26 (dramatic mountain ridges)
    float elev_above_water = std::max(0.0f, elev_base - (water_level + 0.02f));
    float elev_ramp = std::clamp(elev_above_water * 3.3f, 0.0f, 1.0f);
    // Squared ramp: low elevations get very little, high get a lot
    float elev_factor = elev_ramp * elev_ramp;

    // Spatial density variation: some areas more rugged than others
    float ridge_density = ns.dust_n.GetNoise(wx * 0.5f, wy * 0.5f) * 0.5f + 0.5f;
    float density_factor = 0.5f + ridge_density * 0.5f;

    // Tectonic activity boosts ridges
    float tect_boost = 1.0f + std::clamp(tectonic - 0.3f, 0.0f, 0.4f);

    // Base terrain slope gates detail: flat surfaces (alluvial plains, coastal
    // lowlands) stay smooth, steep terrain (exposed bedrock) gets full ridges.
    // Slope from the base heightfield — not the detailed elevation.
    float base_slope, base_aspect_unused;
    compute_gradient(terrain, tile_fx, tile_fy, base_slope, base_aspect_unused);
    // Ramp: slopes < 0.04 get ~20% of detail, slopes > 0.15 get full detail
    float slope_gate = std::clamp((base_slope - 0.04f) / 0.11f, 0.2f, 1.0f);

    float detail_gate = elev_factor * density_factor * tect_boost * slope_gate;
    detail_gate = std::clamp(detail_gate, 0.0f, 1.0f);

    // Amplitude scales from 0 at coast to 0.40 at mountain peaks.
    // Must be large enough that ridge gradients rival coastal escarpment
    // gradients (~0.1/tile) so hillshade shows comparable relief inland.
    float detail_amplitude = detail_gate * 0.40f;

    // Domain warp for organic curves
    float warp_strength = 70.0f;
    float dwx = wx + ns.warp_x.GetNoise(wx, wy) * warp_strength;
    float dwy = wy + ns.warp_y.GetNoise(wx, wy) * warp_strength;

    // Broad-scale strike direction for mild anisotropy
    float strike_angle = ns.strata_dir.GetNoise(wx * 0.08f, wy * 0.08f) * 3.14159f;
    float st_cos = std::cos(strike_angle), st_sin = std::sin(strike_angle);

    // PRIMARY: mostly isotropic with mild directional bias (70/30)
    // Less directional = less visible banding artifacts
    float r1_iso_raw = ns.ridge_main.GetNoise(dwx, dwy);
    float ridge1_iso = std::max(0.0f, 1.0f - std::abs(r1_iso_raw) * 2.0f);

    float along = dwx * st_cos + dwy * st_sin;
    float cross = -dwx * st_sin + dwy * st_cos;
    float r1_dir_raw = ns.ridge_main.GetNoise(cross, along * 0.45f);
    float ridge1_dir = std::max(0.0f, 1.0f - std::abs(r1_dir_raw) * 2.0f);

    float ridge1 = ridge1_iso * 0.70f + ridge1_dir * 0.30f;
    ridge1 = std::pow(ridge1, 1.2f);

    // SECONDARY: offset-warped spurs (branching)
    float dwx2 = wx + ns.warp_x.GetNoise(wx * 0.9f + 300.0f, wy * 0.9f) * 55.0f;
    float dwy2 = wy + ns.warp_y.GetNoise(wx * 0.9f, wy * 0.9f + 300.0f) * 55.0f;
    float r2_raw = ns.ridge_sec.GetNoise(dwx2, dwy2);
    float ridge2 = std::max(0.0f, 1.0f - std::abs(r2_raw) * 2.0f);

    // FINE: craggy peaks (isotropic) — skip at macro LOD (ppt < 6)
    float ridge3 = 0.0f, ridge4 = 0.0f;
    if (ppt >= 6.0f) {
        float r3_raw = ns.ridge_fine.GetNoise(dwx * 0.7f, dwy * 0.7f);
        ridge3 = std::max(0.0f, 1.0f - std::abs(r3_raw) * 2.0f);

        // EXTRA FINE: tributary-scale fractal detail — skip at meso LOD (ppt < 12)
        if (ppt >= 12.0f) {
            float r4_raw = ns.ridge_fine.GetNoise(dwx * 1.8f + 500.0f, dwy * 1.8f + 500.0f);
            ridge4 = std::max(0.0f, 1.0f - std::abs(r4_raw) * 2.0f);
        }
    }

    // Reweight based on active octaves
    float detail_raw;
    if (ppt < 6.0f)
        detail_raw = ridge1 * 0.60f + ridge2 * 0.40f;  // macro: 2 octaves
    else if (ppt < 12.0f)
        detail_raw = ridge1 * 0.48f + ridge2 * 0.32f + ridge3 * 0.20f;  // meso: 3 octaves
    else
        detail_raw = ridge1 * 0.42f + ridge2 * 0.28f + ridge3 * 0.18f + ridge4 * 0.12f;
    // Valley floor threshold
    float detail_height = std::max(0.0f, detail_raw - 0.12f) / 0.88f;
    float elev = elev_base + detail_height * detail_amplitude;

    // Compute gradient from DETAILED elevation (includes blended ridges)
    // Wider epsilon at low LOD for smoother hillshade (avoids sparkle)
    float px_slope, px_aspect;
    {
        float grad_eps = (ppt < 6.0f) ? 1.5f : (ppt < 12.0f) ? 0.7f : 0.4f;
        auto detailed_elev = [&](float fx, float fy) -> float {
            float e = sample_elev(terrain, fx, fy);
            float r = sample_field(terrain, fx, fy, &TerrainTile::roughness);
            float t = sample_field(terrain, fx, fy, &TerrainTile::tectonic_activity);
            float nwx = fx * BASE_PPT;
            float nwy = fy * BASE_PPT;
            float eaw = std::max(0.0f, e - (water_level + 0.02f));
            float er = std::clamp(eaw * 3.3f, 0.0f, 1.0f);
            float ef = er * er;
            float rdn = ns.dust_n.GetNoise(nwx * 0.5f, nwy * 0.5f) * 0.5f + 0.5f;
            float dfn = 0.5f + rdn * 0.5f;
            float tbn = 1.0f + std::clamp(t - 0.3f, 0.0f, 0.4f);
            float sgn = std::clamp((base_slope - 0.04f) / 0.11f, 0.2f, 1.0f);
            float dgn = std::clamp(ef * dfn * tbn * sgn, 0.0f, 1.0f);
            float amp = dgn * 0.40f;
            float ws = 70.0f;
            float ndwx = nwx + ns.warp_x.GetNoise(nwx, nwy) * ws;
            float ndwy = nwy + ns.warp_y.GetNoise(nwx, nwy) * ws;
            float sa = ns.strata_dir.GetNoise(nwx * 0.08f, nwy * 0.08f) * 3.14159f;
            float nr1i = ns.ridge_main.GetNoise(ndwx, ndwy);
            float nrd1i = std::max(0.0f, 1.0f - std::abs(nr1i) * 2.0f);
            float sc = std::cos(sa), ss_v = std::sin(sa);
            float al = ndwx * sc + ndwy * ss_v;
            float cr = -ndwx * ss_v + ndwy * sc;
            float nr1d = ns.ridge_main.GetNoise(cr, al * 0.45f);
            float nrd1d = std::max(0.0f, 1.0f - std::abs(nr1d) * 2.0f);
            float nrd1 = std::pow(nrd1i * 0.70f + nrd1d * 0.30f, 1.2f);
            float nwx2 = nwx + ns.warp_x.GetNoise(nwx*0.9f+300.0f, nwy*0.9f) * 55.0f;
            float nwy2 = nwy + ns.warp_y.GetNoise(nwx*0.9f, nwy*0.9f+300.0f) * 55.0f;
            float nr2 = ns.ridge_sec.GetNoise(nwx2, nwy2);
            float nrd2 = std::max(0.0f, 1.0f - std::abs(nr2) * 2.0f);
            // LOD-gated fine octaves in gradient lambda
            float nrd3 = 0.0f, nrd4 = 0.0f;
            if (ppt >= 6.0f) {
                float nr3 = ns.ridge_fine.GetNoise(ndwx * 0.7f, ndwy * 0.7f);
                nrd3 = std::max(0.0f, 1.0f - std::abs(nr3) * 2.0f);
                if (ppt >= 12.0f) {
                    float nr4 = ns.ridge_fine.GetNoise(ndwx*1.8f+500.0f, ndwy*1.8f+500.0f);
                    nrd4 = std::max(0.0f, 1.0f - std::abs(nr4) * 2.0f);
                }
            }
            float dh_raw;
            if (ppt < 6.0f)
                dh_raw = nrd1 * 0.60f + nrd2 * 0.40f;
            else if (ppt < 12.0f)
                dh_raw = nrd1 * 0.48f + nrd2 * 0.32f + nrd3 * 0.20f;
            else
                dh_raw = nrd1 * 0.42f + nrd2 * 0.28f + nrd3 * 0.18f + nrd4 * 0.12f;
            float dh = std::max(0.0f, dh_raw - 0.12f) / 0.88f;
            return e + dh * amp;
        };
        float dzdx = detailed_elev(tile_fx + grad_eps, tile_fy) -
                     detailed_elev(tile_fx - grad_eps, tile_fy);
        float dzdy = detailed_elev(tile_fx, tile_fy + grad_eps) -
                     detailed_elev(tile_fx, tile_fy - grad_eps);
        dzdx /= (2.0f * grad_eps);
        dzdy /= (2.0f * grad_eps);
        px_slope = std::sqrt(dzdx * dzdx + dzdy * dzdy);
        px_aspect = std::atan2(-dzdy, -dzdx);
    }

    float curvature = compute_curvature(terrain, tile_fx, tile_fy);

    // ── Per-pixel ocean/land with noise-perturbed coastline ─────
    // Multi-scale noise perturbs water level near the coastline,
    // breaking the tile-grid staircase. Broad noise (smooth curves) +
    // medium noise (detail) creates organic shoreline without pixel speckle.
    float raw_shore = water_level - elev;  // positive = underwater
    float near_coast = std::clamp(1.0f - std::abs(raw_shore) * 10.0f, 0.0f, 1.0f);
    // Broad-scale perturbation (smooth coastline curves, ~25 tile wavelength)
    float coast_broad = ns.terrain_var.GetNoise(wx * 1.5f, wy * 1.5f) * 0.025f;
    // Medium-scale perturbation (bays/headlands, ~8 tile wavelength)
    float coast_med = ns.medium.GetNoise(wx * 0.6f, wy * 0.6f) * 0.018f;
    float coast_perturb = (coast_broad + coast_med) * near_coast;
    float effective_wl = water_level + coast_perturb;
    float shore_depth = effective_wl - elev;

    // Variable shelf width: active margins → narrow, passive → wider
    // Tightened to reduce the "halo ring" artifact
    float shelf_scale = 12.0f + (1.0f - tectonic) * 12.0f - px_slope * 20.0f;
    shelf_scale = std::clamp(shelf_scale, 6.0f, 28.0f);
    float coastal_blend = std::clamp(shore_depth * shelf_scale, -1.0f, 1.0f);

    Col3 color;

    if (coastal_blend > 0.6f) {
        // ── Ocean ────────────────────────────────────────────────
        float depth = std::max(0.0f, shore_depth);
        float depth01 = std::clamp(depth / 0.40f, 0.0f, 1.0f);
        float shelf_noise = ns.shelf_n.GetNoise(wx, wy) * 0.5f + 0.5f;
        Col3 base = ocean_color(depth01, shelf_noise);

        // Very subtle ripple only — no hillshade on ocean (creates tile-grid artifacts)
        float ripple = ns.ocean_n.GetNoise(wx, wy);
        color = clamp_col(base * (1.0f + ripple * 0.03f));

    } else if (coastal_blend > -0.15f) {
        // ── Coastal transition (very narrow to avoid halo) ────────
        float t = std::clamp((coastal_blend + 0.15f) / 0.75f, 0.0f, 1.0f);

        float beach_noise = ns.detail.GetNoise(wx * 1.5f, wy * 1.5f) * 0.5f + 0.5f;
        bool steep_coast = px_slope > 0.12f;
        Col3 shore_col;
        if (steep_coast) {
            Col3 cliff_rock = mountain_rock_color(rock_blend) * 0.70f;
            shore_col = lerp(cliff_rock, WET_SAND, beach_noise * 0.15f);
        } else {
            shore_col = lerp(WET_SAND, BEACH_SAND, beach_noise * 0.4f);
            shore_col = shore_col * 0.82f;  // darker to reduce halo
        }

        float shelf_noise = ns.shelf_n.GetNoise(wx, wy) * 0.5f + 0.5f;
        Col3 shallow_col = {30.0f + shelf_noise * 12.0f, 68.0f + shelf_noise * 10.0f,
                            105.0f + shelf_noise * 8.0f};

        color = lerp(shore_col, shallow_col, t);
        float shade = hillshade(px_slope * 0.8f, px_aspect);
        color = clamp_col(color * shade);

    } else {
        // ── Land (bare geological surface) ───────────────────────

        // --- 1. Continuous rock color from blend field ---
        // Determine elevation context for rock color ramp
        // Use continuous elevation instead of discrete ElevBand
        float mtn_factor = std::clamp((elev - 0.55f) * 5.0f, 0.0f, 1.0f);
        float hill_factor =
            std::clamp((elev - 0.42f) * 5.0f, 0.0f, 1.0f) * (1.0f - mtn_factor);
        float lowland_factor = 1.0f - mtn_factor - hill_factor;
        // Slope also pushes toward mountain character
        mtn_factor = std::clamp(mtn_factor + px_slope * 2.0f, 0.0f, 1.0f);
        hill_factor = std::clamp(hill_factor, 0.0f, 1.0f - mtn_factor);
        lowland_factor = std::max(0.0f, 1.0f - mtn_factor - hill_factor);

        Col3 mtn_col = mountain_rock_color(rock_blend);
        Col3 hill_col = hill_rock_color(rock_blend);
        float coastal = std::clamp(1.0f - dist_ocean * 0.06f, 0.0f, 1.0f);
        Col3 low_col = lerp(interior_lowland_rock_color(rock_blend),
                            coastal_lowland_rock_color(rock_blend), coastal);

        Col3 rock_col =
            mtn_col * mtn_factor + hill_col * hill_factor + low_col * lowland_factor;

        // --- 1b. Geological province coloring (structure-tied + noise) ---
        // Province hue is driven by structural terrain context + noise variation
        // so geological color aligns with elevation domains, not just random noise
        float geo_noise = ns.medium.GetNoise(wx * 0.7f, wy * 0.7f);  // ~20 tile
        float geo_broad = ns.terrain_var.GetNoise(wx * 1.0f, wy * 1.0f);  // ~30 tile

        // Structure-tied warmth: lowlands warm/sandy, mountains cool/grey,
        // coastal areas lighter, interior darker
        float struct_warmth = lowland_factor * 0.7f + hill_factor * 0.4f;
        float coastal_light = std::clamp(1.0f - dist_ocean * 0.08f, 0.0f, 0.5f);
        struct_warmth += coastal_light * 0.3f;
        // Noise adds local variation within structural context
        float warmth = std::clamp(struct_warmth + geo_noise * 0.3f, 0.0f, 1.0f);
        float redness = std::clamp(0.3f + geo_broad * 0.4f + lowland_factor * 0.2f
                                   - coastal_light * 0.15f, 0.0f, 1.0f);

        // Province color shifts — large enough to read at zoom-out
        // Reduced on mountains so rock ramp colors dominate at high elevations
        float shift_str = 1.0f - mtn_factor * 0.6f;
        Col3 province_shift = {
            (warmth - 0.4f) * 45.0f + (redness - 0.4f) * 25.0f,
            (warmth - 0.4f) * 15.0f - (redness - 0.4f) * 8.0f,
            std::max(-(warmth - 0.4f) * 20.0f - (redness - 0.4f) * 10.0f, -12.0f)
        };
        rock_col = rock_col + province_shift * shift_str;

        // Brightness: moderate range, don't over-darken mountains
        float bright_mod = 0.92f + lowland_factor * 0.06f + coastal_light * 0.04f +
                           (geo_broad * 0.5f + 0.5f) * 0.06f;
        rock_col = rock_col * bright_mod;

        // --- 1b2. Mountain hue variation from curvature ---
        // Ridges warmer/lighter, valleys cooler/darker — adds structure within mountains
        if (mtn_factor > 0.2f) {
            float curv_hue = std::clamp(curvature * 3.0f, -0.15f, 0.15f);
            rock_col = rock_col + Col3{curv_hue * 60.0f, curv_hue * 25.0f, -curv_hue * 15.0f}
                                  * mtn_factor;
        }

        // --- 1c. Tectonic activity color shift ---
        // Active zones: cooler/darker; stable cratons: warmer/sandier
        float tect_shift = (tectonic - 0.3f) * 2.0f;
        if (tect_shift > 0) {
            float active_str = std::clamp(tect_shift, 0.0f, 0.35f);
            rock_col = rock_col * (1.0f - active_str * 0.25f);  // darken active
            rock_col = rock_col + Col3{-5, -3, 4} * active_str;  // cool active
        } else {
            float stable_str = std::clamp(-tect_shift, 0.0f, 0.25f);
            rock_col = rock_col + Col3{12, 6, -6} * stable_str;  // warm stable
        }

        // --- 1d. Continental interior warmth ---
        // Deep interior develops red/ochre laterite tones (realistic weathering)
        float interior_factor =
            std::clamp((dist_ocean - 6.0f) * 0.05f, 0.0f, 0.30f) * lowland_factor;
        rock_col = rock_col + Col3{22, -6, -18} * interior_factor;

        // --- 2. Continuous soil color ---
        Col3 soil_col = continuous_soil_color(soil_blend, dist_ocean);

        // --- 3. Soil cover fraction ---
        // Soil only covers where deep enough and flat enough
        float soil_cover = std::clamp(soil_depth / 2.5f, 0.0f, 1.0f);
        soil_cover *= std::clamp(1.0f - px_slope * 4.0f, 0.0f, 1.0f);
        soil_cover *= std::clamp(1.3f - elev * 2.0f, 0.0f, 1.0f);
        // Keep soil as subtle tint, not full replacement
        float var_noise = ns.terrain_var.GetNoise(wx, wy) * 0.08f;
        float eff_soil = std::clamp(soil_cover + var_noise, 0.0f, 0.55f);

        // --- 4. Blend rock + soil ---
        Col3 ground = lerp(rock_col, soil_col, eff_soil);

        // --- 5. Beach on low-slope coast ---
        if (dist_ocean < 3.0f && px_slope < 0.10f && elev < water_level + 0.06f) {
            float bn = ns.detail.GetNoise(wx * 1.5f, wy * 1.5f) * 0.5f + 0.5f;
            float bt = std::clamp(1.0f - dist_ocean * 0.35f, 0.0f, 1.0f) *
                       std::clamp(1.0f - px_slope * 12.0f, 0.0f, 1.0f);
            Col3 beach = lerp(WET_SAND, BEACH_SAND, bn);
            ground = lerp(ground, beach, bt * 0.8f);
        }

        // --- 6. Scree on very steep slopes (subtle) ---
        if (px_slope > 0.18f) {
            float scree_str = std::clamp((px_slope - 0.18f) * 3.0f, 0.0f, 0.3f);
            Col3 scree_col = rock_col * 0.75f + Col3{15, 12, 8};
            ground = lerp(ground, scree_col, scree_str);
        }

        // --- 7. Texture detail layer (LOD-gated) ---
        // At macro LOD, skip fine texture entirely.
        // At meso, add medium + coarse. At micro, add fine grain too.
        if (ppt >= 6.0f) {
            float med_tex = ns.medium.GetNoise(wx * 1.2f, wy * 1.2f);
            float coarse_tex = ns.terrain_var.GetNoise(wx * 2.0f, wy * 2.0f);
            float lowland_tex = (1.0f - mtn_factor) *
                (med_tex * 0.06f + coarse_tex * 0.04f);
            if (ppt >= 12.0f) {
                // Micro: full fine-grain texture
                float fine = ns.detail.GetNoise(wx, wy);
                float detail_str = 0.015f + tectonic * 0.025f;
                lowland_tex += (1.0f - mtn_factor) * fine * 0.02f;
                ground = ground * (1.0f - detail_str + fine * detail_str * 2.0f + lowland_tex);
            } else {
                ground = ground * (1.0f + lowland_tex);
            }
        }

        color = ground;

        // --- 8. Single unified hillshade ---
        // px_slope/px_aspect are computed from detailed_elev which
        // combines base terrain + ridge noise. This means:
        //   Coast: base terrain gradient dominates → strong shade ✓
        //   Mountains: ridge noise gradient dominates → strong shade ✓
        //   Lowlands: both gentle → gentle shade ✓
        // No two-scale split needed — one gradient captures everything.
        float shade = hillshade(px_slope, px_aspect, 12.0f);

        // Ambient occlusion from curvature
        float ao = std::clamp(curvature * 4.0f, -0.18f, 0.08f);
        shade += ao * mtn_factor;

        color = color * std::max(shade, 0.05f);

        // No snow — this is bare geological surface before weather.

        color = clamp_col(color);
    }

    auto r = static_cast<uint8_t>(color.r);
    auto g = static_cast<uint8_t>(color.g);
    auto b = static_cast<uint8_t>(color.b);
    return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) |
           (static_cast<uint32_t>(b) << 16) | (255u << 24);
}

NoiseSet& get_noise_set(uint32_t seed) {
    thread_local NoiseSet* cached_ns = nullptr;
    thread_local uint32_t cached_seed = 0;
    if (!cached_ns || cached_seed != seed) {
        delete cached_ns;
        cached_ns = new NoiseSet(seed);
        cached_seed = seed;
    }
    return *cached_ns;
}

}  // namespace (anonymous, inside sandbox)

// ═══════════════════════════════════════════════════════════════════════════
// Public API: LOD-aware pixel evaluator
// ═══════════════════════════════════════════════════════════════════════════

uint32_t eval_terrain_pixel(const Terrain& terrain, float world_x, float world_y,
                            float pixels_per_tile, uint32_t seed, float water_level) {
    NoiseSet& ns = get_noise_set(seed);
    return eval_pixel_internal(terrain, world_x, world_y, pixels_per_tile, ns, water_level);
}

// ═══════════════════════════════════════════════════════════════════════════
// Public API: render a rectangular region at arbitrary scale
// ═══════════════════════════════════════════════════════════════════════════

void render_terrain_region(const Terrain& terrain, float world_x0, float world_y0,
                           float world_per_pixel, int out_w, int out_h, uint32_t seed,
                           uint32_t* out_pixels, int out_stride, float water_level) {
    NoiseSet& ns = get_noise_set(seed);
    float ppt = 1.0f / world_per_pixel;  // pixels per tile

    for (int py = 0; py < out_h; ++py) {
        float wy = world_y0 + static_cast<float>(py) * world_per_pixel;
        for (int px = 0; px < out_w; ++px) {
            float wx = world_x0 + static_cast<float>(px) * world_per_pixel;
            out_pixels[py * out_stride + px] =
                eval_pixel_internal(terrain, wx, wy, ppt, ns, water_level);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Legacy API: 16×16 tile texture (wraps eval_pixel_internal at ppt=16)
// ═══════════════════════════════════════════════════════════════════════════

TilePixels generate_tile_texture(const Terrain& terrain, int tile_x, int tile_y, uint32_t seed,
                                 float water_level) {
    NoiseSet& ns = get_noise_set(seed);
    TilePixels out{};
    constexpr int SZ = TilePixels::SIZE;
    constexpr float INV_SZ = 1.0f / static_cast<float>(SZ);
    constexpr float PPT = 16.0f;

    int tw = static_cast<int>(terrain.width);
    int th = static_cast<int>(terrain.height);
    if (tile_x < 0 || tile_y < 0 || tile_x >= tw || tile_y >= th) return out;

    for (int py = 0; py < SZ; ++py) {
        for (int px = 0; px < SZ; ++px) {
            float world_x = static_cast<float>(tile_x) + static_cast<float>(px) * INV_SZ;
            float world_y = static_cast<float>(tile_y) + static_cast<float>(py) * INV_SZ;
            out.pixels[py * SZ + px] =
                eval_pixel_internal(terrain, world_x, world_y, PPT, ns, water_level);
        }
    }
    return out;
}

void render_tile_patch(const Terrain& terrain, int patch_x, int patch_y, int patch_w, int patch_h,
                       uint32_t seed, uint32_t* out_pixels, int out_stride, float water_level) {
    constexpr int SZ = TilePixels::SIZE;
    constexpr float INV_SZ = 1.0f / static_cast<float>(SZ);
    constexpr float PPT = 16.0f;
    NoiseSet& ns = get_noise_set(seed);

    for (int ty = 0; ty < patch_h; ++ty) {
        for (int tx = 0; tx < patch_w; ++tx) {
            int wx = patch_x + tx;
            int wy = patch_y + ty;
            if (wx < 0 || wy < 0 || wx >= static_cast<int>(terrain.width) ||
                wy >= static_cast<int>(terrain.height))
                continue;

            for (int py = 0; py < SZ; ++py) {
                int out_y = ty * SZ + py;
                for (int px = 0; px < SZ; ++px) {
                    int out_x = tx * SZ + px;
                    float world_x = static_cast<float>(wx) + static_cast<float>(px) * INV_SZ;
                    float world_y = static_cast<float>(wy) + static_cast<float>(py) * INV_SZ;
                    out_pixels[out_y * out_stride + out_x] =
                        eval_pixel_internal(terrain, world_x, world_y, PPT, ns, water_level);
                }
            }
        }
    }
}

}  // namespace sandbox
