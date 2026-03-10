#include "tile_texture.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstring>
#include <thread>

namespace sandbox {

namespace {

// ═══════════════════════════════════════════════════════════════════════════
// Hash utilities
// ═══════════════════════════════════════════════════════════════════════════

struct Col3 {
    float r, g, b;
    Col3 operator*(float s) const { return {r * s, g * s, b * s}; }
    Col3 operator+(Col3 o) const { return {r + o.r, g + o.g, b + o.b}; }
};

Col3 lerp_col(Col3 a, Col3 b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

uint32_t pack_rgba(Col3 c) {
    auto clb = [](float v) { return static_cast<uint8_t>(std::clamp(v, 0.0f, 255.0f)); };
    return static_cast<uint32_t>(clb(c.r)) | (static_cast<uint32_t>(clb(c.g)) << 8) |
           (static_cast<uint32_t>(clb(c.b)) << 16) | (255u << 24);
}

uint32_t pack_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) |
           (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(a) << 24);
}

inline uint32_t hash2d(int x, int y, uint32_t seed) {
    uint32_t h =
        static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u + seed;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

inline float hash01(int x, int y, uint32_t seed) {
    return static_cast<float>(hash2d(x, y, seed) & 0xFFFFu) / 65535.0f;
}

// ═══════════════════════════════════════════════════════════════════════════
// Pattern variation context
// ═══════════════════════════════════════════════════════════════════════════

struct PatVar {
    float color_shift;  // [-1,1] low-freq color variation
    float detail_mod;   // [-1,1] medium-freq detail modulation
};

Col3 vary_color(Col3 c, float shift, float amount) {
    float s = shift * amount;
    return {c.r + s, c.g + s * 0.9f, c.b + s * 0.7f};
}

// ═══════════════════════════════════════════════════════════════════════════
// Base texture pattern functions — SNES-style with hash-driven variation
// ═══════════════════════════════════════════════════════════════════════════

Col3 pattern_water(int px, int py, bool deep) {
    Col3 base = deep ? Col3{28, 52, 108} : Col3{52, 95, 152};
    Col3 dark = deep ? Col3{18, 38, 88} : Col3{40, 78, 132};
    Col3 light = deep ? Col3{42, 68, 128} : Col3{68, 118, 172};

    float h = hash01(px, py, 19000u);
    float h2 = hash01(px / 3, py / 3, 19100u);
    float blend = h * 0.4f + h2 * 0.3f - 0.35f;
    Col3 color = lerp_col(base, (blend > 0) ? light : dark, std::abs(blend) * 0.6f);

    // Wave hints
    float wx = static_cast<float>(px);
    float wy = static_cast<float>(py);
    float wave = std::sin(wy * 0.08f + std::sin(wx * 0.03f) * 2.5f);
    if (wave > 0.6f) {
        float t = (wave - 0.6f) / 0.4f;
        color = lerp_col(color, light, t * 0.25f);
    }

    float grain = (hash01(px, py, 19200u) - 0.5f) * 3.0f;
    color = {color.r + grain * 0.3f, color.g + grain * 0.4f, color.b + grain * 0.6f};
    return color;
}

Col3 pattern_sand(int px, int py, PatVar pv) {
    Col3 base = vary_color({214, 190, 138}, pv.color_shift, 5.0f);
    Col3 light = vary_color({224, 202, 152}, pv.color_shift, 5.0f);
    Col3 speck = vary_color({196, 170, 120}, pv.color_shift, 5.0f);

    float y = static_cast<float>(py);
    float x = static_cast<float>(px);
    // Wind ripple pattern at tile scale
    float ripple = std::sin(y * 0.45f + std::sin(x * 0.15f + pv.color_shift) * 1.8f);
    if (ripple > 0.5f)
        return lerp_col(base, light, (ripple - 0.5f) * 1.5f);
    if (ripple < -0.5f)
        return lerp_col(base, speck, (-ripple - 0.5f) * 1.2f);
    float h = hash01(px, py, 7000u);
    if (h < 0.08f + pv.detail_mod * 0.03f)
        return speck;
    if (h > 0.92f)
        return light;
    return base;
}

// ── Soil patterns ──────────────────────────────────────────────────────

Col3 pattern_soil_loam(int px, int py, PatVar pv) {
    Col3 base = vary_color({124, 98, 72}, pv.color_shift, 5.0f);
    Col3 light = vary_color({148, 120, 90}, pv.color_shift, 5.0f);
    Col3 dark = vary_color({106, 82, 60}, pv.color_shift, 5.0f);

    float h = hash01(px, py, 8000u);
    float h2 = hash01(px / 2, py / 2, 8200u);
    if (h < 0.10f + pv.detail_mod * 0.04f)
        return dark;
    if (h > 0.90f)
        return light;
    float wave = std::sin(static_cast<float>(px) * (0.35f + pv.detail_mod * 0.08f) +
                          std::sin(static_cast<float>(py) * 0.20f + pv.color_shift * 2.0f) * 2.5f);
    if (wave > 0.4f)
        return lerp_col(base, light, (wave - 0.4f) * 0.8f);
    if (wave < -0.4f)
        return lerp_col(base, dark, (-wave - 0.4f) * 0.6f);
    if (h2 > 0.7f)
        return lerp_col(base, light, 0.3f);
    if (h2 < 0.3f)
        return lerp_col(base, dark, 0.2f);
    return base;
}

Col3 pattern_soil_dry(int px, int py, PatVar pv) {
    Col3 base = vary_color({176, 156, 124}, pv.color_shift, 5.0f);
    Col3 light = vary_color({190, 170, 138}, pv.color_shift, 5.0f);
    Col3 crack = vary_color({160, 140, 110}, pv.color_shift, 5.0f);

    float x = static_cast<float>(px);
    float y = static_cast<float>(py);
    float freq = 0.40f + pv.detail_mod * 0.08f;
    float c1 = std::sin(x * freq + std::sin(y * 0.15f + pv.color_shift) * 3.0f);
    if (std::abs(c1) < 0.15f + pv.detail_mod * 0.04f)
        return crack;
    if (hash01(px, py, 8500u) < 0.08f + pv.detail_mod * 0.03f)
        return crack;
    if (hash01(px, py, 8600u) > 0.92f)
        return light;
    float h2 = hash01(px / 2, py / 2, 8700u);
    if (h2 > 0.75f)
        return lerp_col(base, light, 0.4f);
    return base;
}

Col3 pattern_soil_clay(int px, int py, PatVar pv) {
    Col3 base = vary_color({150, 92, 70}, pv.color_shift, 5.0f);
    Col3 light = vary_color({172, 110, 86}, pv.color_shift, 5.0f);
    Col3 dark = vary_color({128, 78, 60}, pv.color_shift, 5.0f);

    float y = static_cast<float>(py);
    float x = static_cast<float>(px);
    int wave = static_cast<int>(std::sin(x * (0.25f + pv.detail_mod * 0.05f)) * 2.0f);
    float band_period = 5.0f + pv.detail_mod * 1.5f;
    float band_f = std::fmod(y + static_cast<float>(wave), band_period);
    if (band_f < 0)
        band_f += band_period;
    if (band_f < 1.2f)
        return dark;
    if (band_f > band_period * 0.5f && band_f < band_period * 0.5f + 1.2f)
        return light;
    if (hash01(px, py, 9000u) < 0.06f + pv.detail_mod * 0.03f)
        return dark;
    return base;
}

Col3 pattern_gravel(int px, int py, PatVar pv) {
    Col3 base = vary_color({138, 128, 116}, pv.color_shift, 5.0f);
    Col3 light = vary_color({176, 166, 154}, pv.color_shift, 5.0f);
    Col3 dark = vary_color({92, 88, 84}, pv.color_shift, 5.0f);

    float h = hash01(px, py, 10000u);
    if (h < 0.18f + pv.detail_mod * 0.06f)
        return dark;
    if (h > 0.82f - pv.detail_mod * 0.04f)
        return light;
    float h2 = hash01(px / 2, py / 2, 10500u);
    if (h2 < 0.25f)
        return lerp_col(base, dark, 0.4f);
    if (h2 > 0.75f)
        return lerp_col(base, light, 0.3f);
    return base;
}

// ── Geology-driven rock patterns ──────────────────────────────────────

// Granite: pink-gray speckled (feldspar + quartz + mica), coarse-grained
// Uses multi-scale hash clusters (2-3px blobs) for mineral grain structure
Col3 pattern_granite(int px, int py, PatVar pv) {
    Col3 base = vary_color({156, 150, 145}, pv.color_shift, 4.0f);      // gray matrix
    Col3 feldspar = vary_color({188, 175, 165}, pv.color_shift, 4.0f);  // pink-warm feldspar
    Col3 quartz = vary_color({180, 178, 175}, pv.color_shift, 3.0f);    // light quartz
    Col3 mica = vary_color({105, 100, 98}, pv.color_shift, 4.0f);       // dark mica

    // 3px cluster hash for coarse crystal structure
    float h3 = hash01(px / 3, py / 3, 14000u);
    // 2px cluster hash for medium grains
    float h2 = hash01(px / 2, py / 2, 14100u);
    // 1px fine hash for grain boundary detail
    float h1 = hash01(px, py, 14200u);

    // Large feldspar phenocrysts (3px clusters, ~30% coverage)
    if (h3 > 0.70f) {
        float blend = 0.5f + h1 * 0.4f;
        return lerp_col(base, feldspar, blend);
    }
    // Quartz veins (3px clusters, ~20% coverage)
    if (h3 < 0.20f) {
        float blend = 0.4f + h1 * 0.3f;
        return lerp_col(base, quartz, blend);
    }
    // Mica flakes at medium scale (2px, ~12% coverage)
    if (h2 < 0.12f + pv.detail_mod * 0.04f)
        return lerp_col(base, mica, 0.6f + h1 * 0.3f);
    // Subtle grain boundary detail (not white noise — modulated by cluster)
    float grain = (h1 - 0.5f) * 4.0f * (0.3f + 0.7f * std::abs(h2 - 0.5f));
    return {base.r + grain * 0.3f, base.g + grain * 0.25f, base.b + grain * 0.2f};
}

// Basalt: dark blue-gray, fine-grained, dense mass with sparse bright flecks
// Columnar joint hints only on steep faces (passed via detail_mod)
Col3 pattern_basalt(int px, int py, PatVar pv) {
    Col3 base = vary_color({68, 72, 80}, pv.color_shift, 3.0f);    // dark blue-gray mass
    Col3 light = vary_color({92, 96, 104}, pv.color_shift, 3.0f);  // weathered lighter
    Col3 vesicle = vary_color({48, 50, 56}, pv.color_shift, 2.0f); // dark vesicle holes
    Col3 fleck = vary_color({115, 118, 122}, pv.color_shift, 2.0f); // bright mineral flecks

    // Fine-grained: subtle correlated texture at 2px scale
    float h2 = hash01(px / 2, py / 2, 15000u);
    float h1 = hash01(px, py, 15100u);
    float grain = (h2 - 0.5f) * 3.5f;
    Col3 col = {base.r + grain, base.g + grain * 0.9f, base.b + grain * 1.1f};

    // Columnar joint hint — subtle vertical line darkening
    float col_joint = std::sin(static_cast<float>(px) * (0.35f + pv.detail_mod * 0.06f));
    if (std::abs(col_joint) < 0.10f)
        col = lerp_col(col, vesicle, 0.2f);

    // Rare vesicles (gas bubbles) — dark pits
    if (h1 > 0.96f)
        return vesicle;
    // Sparse bright mineral flecks (olivine/plagioclase)
    if (h1 < 0.03f)
        return lerp_col(col, fleck, 0.5f);
    // Coarse-scale weathering patches
    float h3 = hash01(px / 4, py / 4, 15200u);
    if (h3 > 0.82f)
        col = lerp_col(col, light, 0.15f);
    return col;
}

// Limestone: cream to light gray, smooth with subtle horizontal bedding
Col3 pattern_limestone(int px, int py, PatVar pv) {
    Col3 base = vary_color({202, 192, 170}, pv.color_shift, 4.0f);   // warm cream
    Col3 light = vary_color({212, 204, 185}, pv.color_shift, 4.0f);  // lighter band
    Col3 dark = vary_color({170, 160, 140}, pv.color_shift, 4.0f);   // darker band
    Col3 fossil = vary_color({160, 152, 138}, pv.color_shift, 3.0f); // fossil specks

    float y = static_cast<float>(py);
    float x = static_cast<float>(px);
    // Subtle horizontal bedding planes
    float bed_period = 6.0f + pv.detail_mod * 2.0f;
    int wave = static_cast<int>(std::sin(x * 0.12f + pv.color_shift) * 1.5f);
    float bed_f = std::fmod(y + static_cast<float>(wave), bed_period);
    if (bed_f < 0)
        bed_f += bed_period;
    if (bed_f < 0.8f)
        return dark;  // bedding plane line

    // Smooth texture — low speck density
    float h = hash01(px, py, 16000u);
    // Fossil specks (slightly darker round spots)
    float h2 = hash01(px / 2, py / 2, 16100u);
    if (h2 > 0.88f && h > 0.5f)
        return fossil;
    if (h > 0.93f)
        return light;
    // Very gentle variation
    float grain = (h - 0.5f) * 4.0f;
    return {base.r + grain * 0.2f, base.g + grain * 0.2f, base.b + grain * 0.15f};
}

// Sandstone: warm tan/orange with prominent directional strata (cross-bedding)
// Wider bands (3-5px) with visible layer thickness variation
Col3 pattern_sandstone(int px, int py, PatVar pv) {
    Col3 base = vary_color({194, 156, 112}, pv.color_shift, 5.0f);  // warm tan
    Col3 light = vary_color({218, 185, 140}, pv.color_shift, 5.0f); // lighter layer
    Col3 dark = vary_color({162, 126, 86}, pv.color_shift, 5.0f);   // darker iron-rich layer
    Col3 iron = vary_color({175, 125, 78}, pv.color_shift, 4.0f);   // iron oxide band

    float x = static_cast<float>(px);
    float y = static_cast<float>(py);
    // Primary cross-bedding: diagonal strata at ~35° (wider period for 3-5px bands)
    float angle = 0.65f + pv.detail_mod * 0.12f;
    float bed_coord = (x * angle + y) * 0.18f +
                      std::sin(y * 0.08f + pv.color_shift * 2.0f) * 2.5f;
    float cross = std::sin(bed_coord);

    // Layer selection: wider bands with cleaner transitions
    if (cross > 0.40f) {
        float t = (cross - 0.40f) / 0.60f;
        return lerp_col(base, light, t * 0.8f);
    }
    if (cross < -0.40f) {
        float t = (-cross - 0.40f) / 0.60f;
        return lerp_col(base, dark, t * 0.7f);
    }
    // Iron oxide band at secondary angle (wider, rarer)
    float cross2 = std::sin((x * 0.25f + y * 0.85f) * 0.15f + pv.color_shift * 3.0f);
    if (std::abs(cross2) < 0.12f + pv.detail_mod * 0.03f)
        return iron;
    // Subtle sand grain texture (very gentle, not white noise)
    float h2 = hash01(px / 2, py / 2, 17000u);
    float grain = (h2 - 0.5f) * 3.0f;
    return {base.r + grain * 0.4f, base.g + grain * 0.3f, base.b + grain * 0.2f};
}

// Shale: dark gray-brown, dense horizontal fissile laminations
Col3 pattern_shale(int px, int py, PatVar pv) {
    Col3 base = vary_color({96, 92, 86}, pv.color_shift, 4.0f);    // gray-brown
    Col3 light = vary_color({118, 112, 104}, pv.color_shift, 4.0f); // lighter lamina
    Col3 dark = vary_color({72, 68, 64}, pv.color_shift, 4.0f);     // dark lamina

    float y = static_cast<float>(py);
    float x = static_cast<float>(px);
    // Dense fissile laminations — narrow horizontal bands (~2px spacing)
    float lam_period = 2.5f + pv.detail_mod * 0.8f;
    int wave = static_cast<int>(std::sin(x * (0.15f + pv.detail_mod * 0.04f)) * 1.0f);
    float lam_f = std::fmod(y + static_cast<float>(wave), lam_period);
    if (lam_f < 0)
        lam_f += lam_period;
    if (lam_f < 0.7f)
        return dark;
    if (lam_f > lam_period * 0.5f && lam_f < lam_period * 0.5f + 0.5f)
        return light;

    // Crumbly speckle — higher density than other rocks
    float h = hash01(px, py, 18000u);
    if (h < 0.12f + pv.detail_mod * 0.04f)
        return dark;
    float grain = (h - 0.5f) * 6.0f;
    return {base.r + grain * 0.3f, base.g + grain * 0.25f, base.b + grain * 0.2f};
}

// Metamorphic: foliated gneissic banding (alternating light/dark, wavy)
Col3 pattern_metamorphic(int px, int py, PatVar pv) {
    Col3 base = vary_color({138, 132, 124}, pv.color_shift, 4.0f);  // medium gray
    Col3 light = vary_color({170, 162, 150}, pv.color_shift, 4.0f); // light felsic band
    Col3 dark = vary_color({112, 108, 104}, pv.color_shift, 4.0f);  // dark mafic band

    float x = static_cast<float>(px);
    float y = static_cast<float>(py);
    // Wavy foliation bands — diagonal, with sinusoidal waviness
    float fold_angle = 0.15f + pv.detail_mod * 0.06f;
    float band_coord = (x * fold_angle + y) * 0.25f +
                        std::sin(x * 0.12f + pv.color_shift * 3.0f) * 1.5f;
    float band = std::sin(band_coord);

    // Strong light/dark contrast (gneissic banding)
    if (band > 0.3f)
        return lerp_col(base, light, std::min((band - 0.3f) * 1.2f, 1.0f));
    if (band < -0.3f)
        return lerp_col(base, dark, std::min((-band - 0.3f) * 1.2f, 1.0f));

    // Fine-scale mineral speckling within bands
    float h = hash01(px, py, 19500u);
    if (h < 0.06f)
        return dark;
    if (h > 0.94f)
        return light;
    return base;
}

Col3 get_pattern(Mat mat, int gpx, int gpy, PatVar pv) {
    switch (mat) {
        case Mat::Water:
            return Col3{56, 100, 155};
        case Mat::Sand:
            return pattern_sand(gpx, gpy, pv);
        case Mat::SoilLoam:
            return pattern_soil_loam(gpx, gpy, pv);
        case Mat::SoilDry:
            return pattern_soil_dry(gpx, gpy, pv);
        case Mat::SoilClay:
            return pattern_soil_clay(gpx, gpy, pv);
        case Mat::Gravel:
            return pattern_gravel(gpx, gpy, pv);
        case Mat::Granite:
            return pattern_granite(gpx, gpy, pv);
        case Mat::Basalt:
            return pattern_basalt(gpx, gpy, pv);
        case Mat::Limestone:
            return pattern_limestone(gpx, gpy, pv);
        case Mat::Sandstone:
            return pattern_sandstone(gpx, gpy, pv);
        case Mat::Shale:
            return pattern_shale(gpx, gpy, pv);
        case Mat::Metamorphic:
            return pattern_metamorphic(gpx, gpy, pv);
        default:
            return {128, 128, 128};
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Template atlas — generated once at startup
// ═══════════════════════════════════════════════════════════════════════════

// Global atlas instance
TemplateAtlas g_atlas;

// Bezier curve evaluation (quadratic)
struct Vec2 {
    float x, y;
};

Vec2 bezier_quad(Vec2 p0, Vec2 p1, Vec2 p2, float t) {
    float u = 1.0f - t;
    return {u * u * p0.x + 2.0f * u * t * p1.x + t * t * p2.x,
            u * u * p0.y + 2.0f * u * t * p1.y + t * t * p2.y};
}

// Combined distance + side from point to quadratic bezier curve
// Returns {distance, side_sign} in a single pass (saves ~2x vs calling separately)
struct BezierResult {
    float dist;
    float side;
};

BezierResult bezier_dist_side(Vec2 p, Vec2 p0, Vec2 p1, Vec2 p2) {
    float min_d2 = 1e9f;
    float best_t = 0.0f;
    constexpr int STEPS = 16;  // 16 steps sufficient for 16px tiles
    constexpr float INV_STEPS = 1.0f / static_cast<float>(STEPS);
    for (int i = 0; i <= STEPS; ++i) {
        float t = static_cast<float>(i) * INV_STEPS;
        Vec2 c = bezier_quad(p0, p1, p2, t);
        float dx = p.x - c.x, dy = p.y - c.y;
        float d2 = dx * dx + dy * dy;
        if (d2 < min_d2) {
            min_d2 = d2;
            best_t = t;
        }
    }
    float dist = std::sqrt(min_d2);

    // Tangent at best_t
    float u = 1.0f - best_t;
    Vec2 tangent = {2.0f * (u * (p1.x - p0.x) + best_t * (p2.x - p1.x)),
                    2.0f * (u * (p1.y - p0.y) + best_t * (p2.y - p1.y))};
    Vec2 c = bezier_quad(p0, p1, p2, best_t);
    // Cross product: tangent × (p - c)
    float side = tangent.x * (p.y - c.y) - tangent.y * (p.x - c.x);

    return {dist, side};
}

// Convenience wrappers for callers that need only one value
float bezier_dist(Vec2 p, Vec2 p0, Vec2 p1, Vec2 p2) {
    return bezier_dist_side(p, p0, p1, p2).dist;
}

float bezier_side(Vec2 p, Vec2 p0, Vec2 p1, Vec2 p2) {
    return bezier_dist_side(p, p0, p1, p2).side;
}

// Generate a transition mask template with a bezier boundary curve
void generate_transition_mask(TileTemplate& tmpl, CanonicalPair pair, TransitionGrade grade,
                              int variant, uint32_t seed) {
    constexpr int SZ = 16;

    // Edge midpoints for canonical pairs — must be at 7.5 (grid center)
    // so that 90° rotation maps midpoints to midpoints exactly.
    // N = (7.5, 0), E = (15, 7.5), S = (7.5, 15), W = (0, 7.5)
    Vec2 p0, p2;
    if (pair == CanonicalPair::NS) {
        p0 = {7.5f, 0.0f};   // N midpoint
        p2 = {7.5f, 15.0f};  // S midpoint
    } else {
        p0 = {7.5f, 0.0f};   // N midpoint
        p2 = {15.0f, 7.5f};  // E midpoint
    }

    // Control point determines the curve shape.
    // CONSTRAINT: control point must ensure perpendicular entry/exit at edge
    // midpoints so adjacent tiles' curves join smoothly (no kinks).
    // For quadratic bezier, tangent at P0 ∝ (P1-P0), tangent at P2 ∝ (P2-P1).
    // Perpendicular to N edge → P1.x = P0.x = 7.5
    // Perpendicular to E edge → P1.y = P2.y = 7.5
    // Perpendicular to S edge → P1.x = P2.x = 7.5
    Vec2 p1;
    if (pair == CanonicalPair::NS) {
        // Straight-through: P1.x must equal 7.5 for perpendicular entry/exit.
        // Vary only P1.y for subtle speed variation.
        float y_offsets[] = {0.0f, -2.0f, 2.0f};
        p1 = {7.5f, 7.5f + y_offsets[variant]};
    } else {
        // Corner arc N→E: P1.x must be 7.5 (perpendicular to N edge) AND
        // P1.y must be 7.5 (perpendicular to E edge). Only solution: tile center.
        (void)variant;
        p1 = {7.5f, 7.5f};
    }

    // Grade determines boundary band width
    float band_w;
    switch (grade) {
        case TransitionGrade::Shallow:
            band_w = 3.5f;
            break;
        case TransitionGrade::Medium:
            band_w = 2.0f;
            break;
        case TransitionGrade::Steep:
            band_w = 1.0f;
            break;
    }

    for (int y = 0; y < SZ; ++y) {
        for (int x = 0; x < SZ; ++x) {
            Vec2 p = {static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f};
            float dist = bezier_dist(p, p0, p1, p2);
            float side = bezier_side(p, p0, p1, p2);

            MaskRegion region;
            if (dist <= band_w) {
                region = MaskRegion::Boundary;
            } else if (side > 0) {
                region = MaskRegion::RegionA;
            } else {
                region = MaskRegion::RegionB;
            }
            tmpl.mask[y * SZ + x] = region;

            // Pre-fill boundary pixels with grade-specific shore/contour treatment
            if (region == MaskRegion::Boundary) {
                float t = dist / band_w;  // 0 at curve, 1 at edge of band
                Col3 col;
                if (grade == TransitionGrade::Shallow) {
                    // Sandy beach gradient
                    Col3 wet = {148, 140, 118};
                    Col3 dry = {195, 185, 162};
                    col = lerp_col(wet, dry, t);
                    float grain =
                        (hash01(x + static_cast<int>(seed & 0xFFu), y, 18000u) - 0.5f) * 4.0f;
                    col = {col.r + grain, col.g + grain * 0.9f, col.b + grain * 0.7f};
                } else if (grade == TransitionGrade::Medium) {
                    // Rocky edge with algae hints
                    Col3 wet_rock = {105, 98, 82};
                    Col3 rock = {142, 132, 115};
                    col = lerp_col(wet_rock, rock, t);
                    if (hash01(x + static_cast<int>(seed & 0xFFu), y, 18100u) > 0.85f) {
                        col = lerp_col(col, Col3{85, 100, 72}, 0.3f);
                    }
                } else {
                    // Sharp cliff line
                    Col3 shadow = {55, 48, 35};
                    Col3 ledge = {140, 130, 112};
                    col = lerp_col(shadow, ledge, t);
                }
                tmpl.pixels[y * SZ + x] = pack_rgba(col);
            } else {
                // Pre-fill with placeholder (will be composited at render time)
                tmpl.pixels[y * SZ + x] = 0;
            }
        }
    }
}

// Build the rotation lookup table
void build_rotation_lut(RotationEntry lut[4][4]) {
    // Edge mapping under clockwise rotation:
    // 0 steps: N=N, E=E, S=S, W=W
    // 1 step:  N→E, E→S, S→W, W→N
    // 2 steps: N→S, E→W, S→N, W→E
    // 3 steps: N→W, E→N, S→E, W→S

    // Horizontal flip of edges: N stays N, S stays S, E↔W
    // Flip transforms canonical NE(N→E) into N→W.
    // Flip transforms canonical NS(N→S) into N→S (no change).

    auto rotate_edge = [](TileEdge e, int steps) -> TileEdge {
        return static_cast<TileEdge>((static_cast<int>(e) + steps) % 4);
    };
    auto flip_edge = [](TileEdge e) -> TileEdge {
        if (e == TileEdge::E) return TileEdge::W;
        if (e == TileEdge::W) return TileEdge::E;
        return e;
    };

    // Initialize all as invalid
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            lut[i][j] = {CanonicalPair::NS, 0, false};

    // Canonical NS: entry=N, exit=S
    // Canonical NE: entry=N, exit=E
    for (int ci = 0; ci < static_cast<int>(CanonicalPair::COUNT); ++ci) {
        CanonicalPair cp = static_cast<CanonicalPair>(ci);
        TileEdge base_entry = TileEdge::N;
        TileEdge base_exit = (cp == CanonicalPair::NS) ? TileEdge::S : TileEdge::E;

        // Direct: rotate canonical pair by r steps
        for (int r = 0; r < 4; ++r) {
            TileEdge re = rotate_edge(base_entry, r);
            TileEdge rx = rotate_edge(base_exit, r);
            lut[static_cast<int>(re)][static_cast<int>(rx)] = {cp, r, false};
        }

        // Flipped: flip the canonical pair, then rotate by r steps.
        // Skip NS — flip(N)=N, flip(S)=S, so flipped NS is identical to direct.
        if (cp == CanonicalPair::NS) continue;
        TileEdge flipped_entry = flip_edge(base_entry);
        TileEdge flipped_exit = flip_edge(base_exit);
        for (int r = 0; r < 4; ++r) {
            TileEdge re = rotate_edge(flipped_entry, r);
            TileEdge rx = rotate_edge(flipped_exit, r);
            lut[static_cast<int>(re)][static_cast<int>(rx)] = {cp, r, true};
        }
    }
}

// Generate PureTerrain template for a given material and variant
void generate_pure_terrain(TileTemplate& tmpl, Mat mat, int variant, uint32_t seed) {
    constexpr int SZ = 16;
    PatVar pv;
    // Use variant to shift pattern slightly
    pv.color_shift = (static_cast<float>(variant) - 1.0f) * 0.3f +
                     (hash01(variant, static_cast<int>(mat), seed + 50000u) - 0.5f) * 0.4f;
    pv.detail_mod = (hash01(variant + 10, static_cast<int>(mat), seed + 50100u) - 0.5f) * 0.5f;

    // Pixel coordinates are global-like but offset by variant for variation
    int offset_x = variant * 37 + static_cast<int>(seed & 0xFF);
    int offset_y = variant * 61 + static_cast<int>((seed >> 8) & 0xFF);

    for (int y = 0; y < SZ; ++y) {
        for (int x = 0; x < SZ; ++x) {
            int gpx = x + offset_x;
            int gpy = y + offset_y;

            Col3 col;
            if (mat == Mat::Water) {
                col = pattern_water(gpx, gpy, false);
            } else {
                col = get_pattern(mat, gpx, gpy, pv);
            }

            // Mild shading variation
            float shade = (hash01(gpx / 3, gpy / 3, seed + 67000u) - 0.5f) * 8.0f;
            col = {col.r + shade * 0.5f, col.g + shade * 0.45f, col.b + shade * 0.35f};

            tmpl.pixels[y * SZ + x] = pack_rgba(col);
            tmpl.mask[y * SZ + x] = MaskRegion::RegionB;  // all solid
        }
    }
}

}  // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
// Public: Material classification
// ═══════════════════════════════════════════════════════════════════════════

Mat classify_tile_mat(const TerrainTile& tile, float water_level) {
    if (tile.is_ocean || tile.elev01 < water_level)
        return Mat::Water;

    // Beach sand: near coast, flat, low elevation
    if (tile.dist_ocean < 2.0f && tile.slope01 < 0.05f && tile.elev01 < water_level + 0.06f)
        return Mat::Sand;

    // Exposed bedrock: steep slopes or thin soil at elevation → use geology
    if (tile.slope01 > 0.15f || (tile.soil_depth < 0.3f && tile.elev01 > 0.55f)) {
        switch (tile.rock) {
            case RockType::Granite:
                return Mat::Granite;
            case RockType::Basalt:
                return Mat::Basalt;
            case RockType::Limestone:
                return Mat::Limestone;
            case RockType::Sandstone:
                return Mat::Sandstone;
            case RockType::Shale:
                return Mat::Shale;
            case RockType::Metamorphic:
                return Mat::Metamorphic;
            default:
                return Mat::Granite;
        }
    }

    // Talus/scree: moderate slope, thin soil
    if (tile.slope01 > 0.08f && tile.soil_depth < 0.6f)
        return Mat::Gravel;

    // Soil classification based on SoilTexture enum
    switch (tile.soil) {
        case SoilTexture::Sand:
            return Mat::Sand;
        case SoilTexture::Clay:
            return Mat::SoilClay;
        case SoilTexture::Silt:
            // Silt-rich soils map to loam (visually similar)
            return Mat::SoilLoam;
        case SoilTexture::Peat:
            // Peat is dark organic soil — closest to clay visually
            return Mat::SoilClay;
        case SoilTexture::Loam:
        default:
            break;
    }

    // Arid interior with thin soil → dry dirt
    if (tile.soil_depth < 0.5f && tile.dist_ocean > 8.0f)
        return Mat::SoilDry;

    // Near-coast sandy soil
    if (tile.dist_ocean < 4.0f && tile.soil_blend < 0.3f)
        return Mat::Sand;

    return Mat::SoilLoam;
}

// ═══════════════════════════════════════════════════════════════════════════
// Tile classification (public API)
// ═══════════════════════════════════════════════════════════════════════════

TileClassification classify_tile(const Terrain& terrain, int tx, int ty, float water_level) {
    int w = static_cast<int>(terrain.width);
    int h = static_cast<int>(terrain.height);
    TileClassification cls;

    auto safe_tile = [&](int x, int y) -> const TerrainTile& {
        x = std::clamp(x, 0, w - 1);
        y = std::clamp(y, 0, h - 1);
        return terrain.tile_at(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
    };

    auto coast_val = [&](int x, int y) -> float {
        const auto& t = safe_tile(x, y);
        if (t.is_ocean)
            return std::min(t.elev01, water_level - 0.001f);
        return std::max(t.elev01, water_level + 0.001f);
    };

    float cnw = coast_val(tx, ty);
    float cne = coast_val(tx + 1, ty);
    float csw = coast_val(tx, ty + 1);
    float cse = coast_val(tx + 1, ty + 1);

    auto crosses = [&](float a, float b) -> bool { return (a < water_level) != (b < water_level); };

    bool cross_n = crosses(cnw, cne);
    bool cross_e = crosses(cne, cse);
    bool cross_s = crosses(csw, cse);
    bool cross_w = crosses(cnw, csw);
    int num_crossings = cross_n + cross_e + cross_s + cross_w;

    const auto& center_tile = safe_tile(tx, ty);
    auto grade_from_slope = [](float slope) -> TransitionGrade {
        if (slope >= 0.45f)
            return TransitionGrade::Steep;
        if (slope >= 0.25f)
            return TransitionGrade::Medium;
        return TransitionGrade::Shallow;
    };

    // Coast grade uses higher thresholds — coast tiles often have artificial slope
    // from the water level crossing that doesn't reflect actual terrain steepness.
    auto coast_grade_from_slope = [](float slope) -> TransitionGrade {
        if (slope >= 0.30f)
            return TransitionGrade::Steep;
        if (slope >= 0.15f)
            return TransitionGrade::Medium;
        return TransitionGrade::Shallow;
    };

    // Coast detection (priority over elevation)
    if (num_crossings == 2) {
        cls.family = TileFamily::Coast;
        cls.grade = coast_grade_from_slope(center_tile.slope01);

        TileEdge e1 = TileEdge::N, e2 = TileEdge::S;
        int found = 0;
        if (cross_n && found < 2) {
            if (found == 0)
                e1 = TileEdge::N;
            else
                e2 = TileEdge::N;
            found++;
        }
        if (cross_e && found < 2) {
            if (found == 0)
                e1 = TileEdge::E;
            else
                e2 = TileEdge::E;
            found++;
        }
        if (cross_s && found < 2) {
            if (found == 0)
                e1 = TileEdge::S;
            else
                e2 = TileEdge::S;
            found++;
        }
        if (cross_w && found < 2) {
            if (found == 0)
                e1 = TileEdge::W;
            else
                e2 = TileEdge::W;
            found++;
        }
        cls.edges = {e1, e2};

        float center_val = (cnw + cne + csw + cse) * 0.25f;
        cls.flipped_polarity = (center_val < water_level);
        return cls;
    }

    // Saddle points (4 crossings) or odd cases
    if (num_crossings != 0) {
        cls.family = TileFamily::PureTerrain;
        return cls;
    }

    // Elevation transition check — slope-gated contour lines.
    // Draw boundary lines only where slope is high enough to warrant them.
    // Gentle slopes rely on hillshade + hypsometric tinting alone (no contour maze).
    float enw = safe_tile(tx, ty).elev01;
    float ene = safe_tile(tx + 1, ty).elev01;
    float esw = safe_tile(tx, ty + 1).elev01;
    float ese = safe_tile(tx + 1, ty + 1).elev01;

    if (!center_tile.is_ocean) {
        constexpr float CONTOUR_SPACING = 0.08f;
        // Slope gate: don't draw contour lines on very flat terrain (plateaus).
        // Low enough that even shallow slopes show elevation steps.
        constexpr float SLOPE_GATE = 0.06f;

        float e_min = std::min({enw, ene, esw, ese});
        float e_max = std::max({enw, ene, esw, ese});

        float best_threshold = -1.0f;
        if (center_tile.slope01 >= SLOPE_GATE) {
            for (float thr = water_level + CONTOUR_SPACING; thr < 1.0f;
                 thr += CONTOUR_SPACING) {
                if (e_min < thr && e_max >= thr) {
                    best_threshold = thr;
                    break;
                }
            }
        }

        if (best_threshold > 0.0f) {
            cls.family = TileFamily::Elevation;
            cls.grade = grade_from_slope(center_tile.slope01);

            auto ecross = [&](float a, float b) -> bool {
                return (a < best_threshold) != (b < best_threshold);
            };
            bool en = ecross(enw, ene);
            bool ee = ecross(ene, ese);
            bool es = ecross(esw, ese);
            bool ew = ecross(enw, esw);

            TileEdge e1 = TileEdge::N, e2 = TileEdge::S;
            int found = 0;
            if (en && found < 2) {
                if (found == 0)
                    e1 = TileEdge::N;
                else
                    e2 = TileEdge::N;
                found++;
            }
            if (ee && found < 2) {
                if (found == 0)
                    e1 = TileEdge::E;
                else
                    e2 = TileEdge::E;
                found++;
            }
            if (es && found < 2) {
                if (found == 0)
                    e1 = TileEdge::S;
                else
                    e2 = TileEdge::S;
                found++;
            }
            if (ew && found < 2) {
                if (found == 0)
                    e1 = TileEdge::W;
                else
                    e2 = TileEdge::W;
                found++;
            }

            if (found == 2) {
                cls.edges = {e1, e2};
                float center_elev = (enw + ene + esw + ese) * 0.25f;
                cls.flipped_polarity = (center_elev < best_threshold);
            } else {
                cls.family = TileFamily::PureTerrain;
            }
            return cls;
        }
    }

    cls.family = TileFamily::PureTerrain;
    return cls;
}

// ═══════════════════════════════════════════════════════════════════════════
// Atlas generation
// ═══════════════════════════════════════════════════════════════════════════

void generate_template_atlas(TemplateAtlas& atlas, uint32_t seed) {
    atlas.seed = seed;

    // Generate PureTerrain templates for each material
    for (int m = 0; m < static_cast<int>(Mat::MAT_COUNT); ++m) {
        for (int v = 0; v < TemplateAtlas::MAT_VARIANTS; ++v) {
            generate_pure_terrain(atlas.pure_terrain[m][v], static_cast<Mat>(m), v, seed);
        }
    }

    // Generate transition mask templates for each canonical pair, grade, variant
    for (int cp = 0; cp < static_cast<int>(CanonicalPair::COUNT); ++cp) {
        for (int g = 0; g < TemplateAtlas::GRADE_COUNT; ++g) {
            for (int v = 0; v < TemplateAtlas::SHAPE_VARIANTS; ++v) {
                generate_transition_mask(atlas.transition_masks[cp][g][v],
                                         static_cast<CanonicalPair>(cp),
                                         static_cast<TransitionGrade>(g), v, seed);
            }
        }
    }

    // Build rotation LUT
    build_rotation_lut(atlas.rotation_lut);

    atlas.valid = true;
}

const TemplateAtlas& get_template_atlas() {
    return g_atlas;
}

// ═══════════════════════════════════════════════════════════════════════════
// Template-based tile rendering
// ═══════════════════════════════════════════════════════════════════════════

namespace {

// ── Hillshade + hypsometric tinting ──────────────────────────────────────

// NW directional light (315° azimuth, ~45° altitude)
float compute_hillshade(float slope01, float aspect) {
    constexpr float LIGHT_AZ = 5.497787f;    // 315° in radians
    constexpr float COS_ALT = 0.707107f;     // cos(45°)
    constexpr float SIN_ALT = 0.707107f;     // sin(45°)
    constexpr float EXAG = 8.0f;             // aggressive slope exaggeration for clear relief
    constexpr float AMBIENT = 0.50f;         // ambient floor — shadows never go near-black

    float slope_rad = std::atan(slope01 * EXAG);
    float cos_slope = std::cos(slope_rad);
    float sin_slope = std::sin(slope_rad);
    float raw = SIN_ALT * cos_slope + COS_ALT * sin_slope * std::cos(LIGHT_AZ - aspect);
    // Gamma lift: reduces harsh metallic contrast in midtones
    float shade = std::pow(std::clamp(raw, 0.0f, 1.0f), 1.35f);
    // Ambient floor so shadows are never too dark
    shade = AMBIENT + (1.0f - AMBIENT) * shade;
    return std::clamp(shade, 0.0f, 1.0f);
}

// Hypsometric tinting: warm lowlands → neutral midlands → cool highlands.
// Quantized into 8 discrete bands for clear topographic readability at all zoom levels.
// Blends toward a tint color rather than darkening — elevation reads as climate zone,
// not shadow. Per GPT recommendation: "height = different palette, not darker."
Col3 apply_elevation_tint(Col3 base, float elev01, float water_level) {
    float height_above = std::max(0.0f, elev01 - water_level);
    float norm_h = std::clamp(height_above / (1.0f - water_level + 0.001f), 0.0f, 1.0f);

    // 8 discrete elevation bands
    constexpr int ELEV_BANDS = 8;
    float band_f = norm_h * static_cast<float>(ELEV_BANDS);
    int band = std::min(static_cast<int>(band_f), ELEV_BANDS - 1);

    // Tint colors: warm beige (lowland) → neutral gray (mid) → cool blue-gray (highland)
    // These are "paper map" tints that read as elevation without killing material identity.
    struct BandTint {
        Col3 color;
        float weight;  // blend strength toward tint
    };
    static constexpr float R = 1.0f;  // just a namespace trick for readability
    (void)R;
    const BandTint bands[ELEV_BANDS] = {
        {{220, 195, 150}, 0.45f},  // B0: coastal lowland — warm sandy
        {{210, 190, 155}, 0.42f},  // B1: low plains — warm
        {{195, 185, 165}, 0.40f},  // B2: upper plains — muted warm
        {{180, 180, 180}, 0.38f},  // B3: foothills — neutral gray
        {{170, 175, 188}, 0.40f},  // B4: lower highlands — cool shift
        {{160, 170, 195}, 0.42f},  // B5: highlands — cool
        {{148, 164, 200}, 0.45f},  // B6: mountains — blue-gray
        {{135, 155, 205}, 0.50f},  // B7: peaks — strong cool blue-gray
    };

    const auto& bt = bands[band];
    return lerp_col(base, bt.color, bt.weight);
}

// Apply hillshade to a color — SNES-style quantized shading
Col3 apply_shade(Col3 col, float shade, float ao = 1.0f) {
    // Quantize shade into 7 discrete steps — enough granularity to avoid
    // posterization artifacts while keeping the stepped pixel-art feel.
    constexpr int SHADE_STEPS = 7;
    shade = std::floor(shade * SHADE_STEPS) / static_cast<float>(SHADE_STEPS - 1);

    // Strength 0.20 — supportive relief, not dominant (hypsometric tint owns elevation)
    constexpr float SHADE_STR = 0.20f;
    float k = 1.0f - SHADE_STR + SHADE_STR * shade;

    // Apply valley ambient occlusion (gentler than before)
    k *= ao;

    // Warm/cool color shift based on quantized shade level
    if (shade > 0.66f) {
        // Sun face: warm highlight
        float warm = (shade - 0.66f) * 0.25f;
        return {col.r * k + warm * 8.0f, col.g * k + warm * 4.0f, col.b * k};
    }
    if (shade < 0.34f) {
        // Shadow face: cool shift
        float cool = (0.34f - shade) * 0.25f;
        return {col.r * k, col.g * k, col.b * k + cool * 6.0f};
    }
    return col * k;
}

// Interpolated terrain properties for continuous rendering across tile boundaries
struct TileProps {
    float elev01;
    float slope01;
    float shade;       // pre-computed hillshade (avoids aspect interpolation issues)
    float micro_slope; // slope01 * 0.5 + roughness * 0.3
    float ao;          // valley ambient occlusion from Laplacian curvature
};

// Get TileProps for a tile (or safe defaults for out-of-bounds)
TileProps get_tile_props(const Terrain& terrain, int tx, int ty) {
    int w = static_cast<int>(terrain.width);
    int h = static_cast<int>(terrain.height);
    tx = std::clamp(tx, 0, w - 1);
    ty = std::clamp(ty, 0, h - 1);
    const auto& t = terrain.tile_at(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty));

    auto safe_elev = [&](int x, int y) {
        x = std::clamp(x, 0, w - 1);
        y = std::clamp(y, 0, h - 1);
        return terrain.tile_at(static_cast<uint32_t>(x), static_cast<uint32_t>(y)).elev01;
    };

    // AO from immediate neighbors (gentle)
    float eN = safe_elev(tx, ty - 1);
    float eS = safe_elev(tx, ty + 1);
    float eE = safe_elev(tx + 1, ty);
    float eW = safe_elev(tx - 1, ty);
    float laplacian = (eN + eS + eE + eW) * 0.25f - t.elev01;
    float ao = std::clamp(1.0f - laplacian * 1.5f, 0.93f, 1.0f);

    return {t.elev01, t.slope01, compute_hillshade(t.slope01, t.aspect),
            t.slope01 * 0.5f + t.roughness * 0.3f, ao};
}

// Bilinear interpolation of TileProps.
// u,v are pixel position within tile: 0 at left/top edge, 1 at right/bottom edge.
// Interpolation is between this tile's center and the 3 nearest neighbor centers.
TileProps interp_tile_props(const TileProps& center, const TileProps& horiz,
                            const TileProps& vert, const TileProps& diag, float wu, float wv) {
    auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    return {lerp(lerp(center.elev01, horiz.elev01, wu), lerp(vert.elev01, diag.elev01, wu), wv),
            lerp(lerp(center.slope01, horiz.slope01, wu), lerp(vert.slope01, diag.slope01, wu), wv),
            lerp(lerp(center.shade, horiz.shade, wu), lerp(vert.shade, diag.shade, wu), wv),
            lerp(lerp(center.micro_slope, horiz.micro_slope, wu),
                 lerp(vert.micro_slope, diag.micro_slope, wu), wv),
            lerp(lerp(center.ao, horiz.ao, wu), lerp(vert.ao, diag.ao, wu), wv)};
}

// Render a land pixel with full material + hillshade + hypsometric tinting
Col3 render_land_pixel(int gpx, int gpy, Mat mat, PatVar pv, const TileProps& tp,
                       float water_level, uint32_t seed) {
    Col3 col = get_pattern(mat, gpx, gpy, pv);

    // Elevation darkening — higher tiles subtly darker (no color shift)
    col = apply_elevation_tint(col, tp.elev01, water_level);

    // Hillshade (pre-computed and interpolated from tile centers)
    float shade = tp.shade;

    // Slope-gated micro-shade: only adds variation on steep terrain.
    // Flat areas stay clean; steep slopes get subtle surface detail.
    float slope_gate = std::clamp((tp.slope01 - 0.06f) / 0.15f, 0.0f, 1.0f);
    float micro_shade =
        (hash01(gpx / 2, gpy / 2, seed + 68100u) - 0.5f) * 0.08f * slope_gate;
    shade = std::clamp(shade + micro_shade, 0.0f, 1.0f);

    col = apply_shade(col, shade, tp.ao);

    col.r = std::clamp(col.r, 0.0f, 255.0f);
    col.g = std::clamp(col.g, 0.0f, 255.0f);
    col.b = std::clamp(col.b, 0.0f, 255.0f);
    return col;
}

// Render a water pixel with depth-based coloring
Col3 render_water_pixel(int gpx, int gpy, float depth) {
    bool deep = depth > 0.10f;
    Col3 wc = pattern_water(gpx, gpy, deep);

    Col3 nearshore = {64, 132, 170};
    Col3 shelf_col = {44, 98, 152};
    Col3 deep_col = {18, 42, 92};

    // Depth noise to add texture to the shelf gradient
    float noise = (hash01(gpx / 5, gpy / 5, 19300u) - 0.5f) * 0.025f;
    float d = std::max(0.0f, depth + noise);

    // Single continuous linear ramp from nearshore to deep — no intermediate
    // shelf break that would create visible tile-aligned isobands.
    // Gamma curve (sqrt) gives more visual space to shallow depths.
    float t = std::clamp(d / 0.25f, 0.0f, 1.0f);
    t = std::sqrt(t);  // more gradual near shore, faster toward deep
    Col3 base_col = lerp_col(nearshore, deep_col, t);
    float wave_alpha = 0.25f + 0.55f * t;
    return lerp_col(base_col, wc, wave_alpha);
}

// ── Coast boundary rendering ────────────────────────────────────────────

// Grade-specific coast transition coloring
// dist_to_boundary: 0 = at boundary, positive = deeper into zone
// side: -1 = water side, +1 = land side
Col3 coast_transition_color(float dist_to_boundary, float side_sign, TransitionGrade grade,
                            Mat land_mat, int gpx, int gpy, PatVar pv, float depth,
                            const TileProps& tp, float water_level, uint32_t seed) {
    float d = dist_to_boundary;  // distance from boundary line

    // Water side: ALWAYS use depth-based color (no terrain-dependent tinting).
    // Only add foam/lightening near the boundary line.
    if (side_sign < 0) {
        Col3 wc = render_water_pixel(gpx, gpy, depth);
        // Lighten near the boundary (shallowing toward shore)
        float shore_fade;
        switch (grade) {
            case TransitionGrade::Shallow: shore_fade = 4.0f; break;
            case TransitionGrade::Medium:  shore_fade = 2.5f; break;
            case TransitionGrade::Steep:
            default:                       shore_fade = 1.5f; break;
        }
        if (d < shore_fade) {
            float t = 1.0f - d / shore_fade;
            // Lighten toward shore (shallower = brighter)
            Col3 lighter = {std::min(255.0f, wc.r * 1.15f + 10.0f * t),
                            std::min(255.0f, wc.g * 1.10f + 8.0f * t),
                            std::min(255.0f, wc.b * 1.05f + 5.0f * t)};
            wc = lerp_col(wc, lighter, t * 0.5f);
            // Foam near boundary (shallow coasts only)
            if (grade == TransitionGrade::Shallow && d < 1.5f) {
                float foam = hash01(gpx, gpy, 18500u);
                if (foam > 0.6f)
                    wc = lerp_col(wc, Col3{210, 218, 225}, (1.0f - d / 1.5f) * 0.3f);
            }
        }
        return wc;
    }

    // Land side: grade determines transition material and width.
    // Shallow = wide gentle beach, Medium = rocky shore, Steep = cliff ledge.
    if (grade == TransitionGrade::Shallow) {
        // Wide gentle beach: wet sand → dry sand → land over 5.5px
        if (d < 2.5f) {
            Col3 wet_sand = {162, 155, 135};
            Col3 sand = {190, 180, 155};
            float t = d / 2.5f;
            Col3 result = lerp_col(wet_sand, sand, t);
            float grain = (hash01(gpx, gpy, 18000u) - 0.5f) * 3.5f;
            result = {result.r + grain, result.g + grain * 0.9f, result.b + grain * 0.7f};
            return result;
        } else if (d < 5.5f) {
            Col3 sand = {190, 180, 155};
            Col3 land = render_land_pixel(gpx, gpy, land_mat, pv, tp, water_level, seed);
            float t = (d - 2.5f) / 3.0f;
            return lerp_col(sand, land, t);
        } else {
            return render_land_pixel(gpx, gpy, land_mat, pv, tp, water_level, seed);
        }
    } else if (grade == TransitionGrade::Medium) {
        // Rocky shore: wet rock → rock → land over 3.5px
        if (d < 1.8f) {
            Col3 wet_rock = {108, 100, 85};
            Col3 rock = {145, 135, 118};
            float t = d / 1.8f;
            Col3 result = lerp_col(wet_rock, rock, t);
            if (hash01(gpx, gpy, 18100u) > 0.82f)
                result = lerp_col(result, Col3{75, 95, 62}, 0.20f);
            return result;
        } else if (d < 3.5f) {
            Col3 rock = {145, 135, 118};
            Col3 land = render_land_pixel(gpx, gpy, land_mat, pv, tp, water_level, seed);
            float t = (d - 1.8f) / 1.7f;
            return lerp_col(rock, land, t);
        } else {
            return render_land_pixel(gpx, gpy, land_mat, pv, tp, water_level, seed);
        }
    } else {
        // Steep cliff coast: dark shadow → ledge → land over 2px
        if (d < 0.8f) {
            Col3 shadow = {62, 55, 42};
            Col3 ledge = {118, 110, 92};
            float t = d / 0.8f;
            return lerp_col(shadow, ledge, t);
        } else if (d < 2.0f) {
            Col3 ledge = {118, 110, 92};
            Col3 land = render_land_pixel(gpx, gpy, land_mat, pv, tp, water_level, seed);
            float t = (d - 0.8f) / 1.2f;
            return lerp_col(ledge, land, t);
        } else {
            return render_land_pixel(gpx, gpy, land_mat, pv, tp, water_level, seed);
        }
    }
}

// ── Raw tile renderer (no edge blending) ────────────────────────────

// Render a single tile's raw pixels into a 16×16 buffer. No neighbor blending.
void render_tile_raw(const Terrain& terrain, const TemplateAtlas& atlas, int tx, int ty,
                     uint32_t seed, float water_level, uint32_t* out_16x16) {
    constexpr int SZ = 16;
    int w = static_cast<int>(terrain.width);
    int h = static_cast<int>(terrain.height);

    if (tx < 0 || ty < 0 || tx >= w || ty >= h) {
        // Out of bounds — fill with deep water
        float depth = 0.20f;
        for (int y = 0; y < SZ; ++y)
            for (int x = 0; x < SZ; ++x)
                out_16x16[y * SZ + x] = pack_rgba(render_water_pixel(tx * SZ + x, ty * SZ + y, depth));
        return;
    }

    const auto& tile = terrain.tile_at(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty));
    Mat self_mat = classify_tile_mat(tile, water_level);
    TileClassification cls = classify_tile(terrain, tx, ty, water_level);

    // Per-tile hash for variant selection
    uint32_t tile_hash = hash2d(tx, ty, seed + 40000u);
    int variant = static_cast<int>(tile_hash % 3);

    // Pattern variation — bilinear interpolation of corner values for spatial continuity.
    // Adjacent tiles share corner coordinates, so interpolated values match at boundaries.
    float cs_nw = (hash01(tx, ty, seed + 50000u) - 0.5f) * 0.6f;
    float cs_ne = (hash01(tx + 1, ty, seed + 50000u) - 0.5f) * 0.6f;
    float cs_sw = (hash01(tx, ty + 1, seed + 50000u) - 0.5f) * 0.6f;
    float cs_se = (hash01(tx + 1, ty + 1, seed + 50000u) - 0.5f) * 0.6f;
    float dm_nw = (hash01(tx, ty, seed + 50100u) - 0.5f) * 0.5f;
    float dm_ne = (hash01(tx + 1, ty, seed + 50100u) - 0.5f) * 0.5f;
    float dm_sw = (hash01(tx, ty + 1, seed + 50100u) - 0.5f) * 0.5f;
    float dm_se = (hash01(tx + 1, ty + 1, seed + 50100u) - 0.5f) * 0.5f;

    // Terrain property interpolation — hillshade, elevation, slope are bilinearly
    // interpolated from the 4 nearest tile centers to eliminate per-tile step artifacts.
    // For pixel at (u,v) within tile, the pixel is between tile center (0.5, 0.5)
    // and neighboring tile centers. We pre-fetch the 4 neighbor props.
    TileProps tp_center = get_tile_props(terrain, tx, ty);
    TileProps tp_E = get_tile_props(terrain, tx + 1, ty);
    TileProps tp_S = get_tile_props(terrain, tx, ty + 1);
    TileProps tp_SE = get_tile_props(terrain, tx + 1, ty + 1);
    TileProps tp_W = get_tile_props(terrain, tx - 1, ty);
    TileProps tp_N = get_tile_props(terrain, tx, ty - 1);
    TileProps tp_NW = get_tile_props(terrain, tx - 1, ty - 1);
    TileProps tp_NE = get_tile_props(terrain, tx + 1, ty - 1);
    TileProps tp_SW = get_tile_props(terrain, tx - 1, ty + 1);

    // Helper: interpolate TileProps for pixel at (u, v) in [0,1]
    // Uses tile-center-to-tile-center interpolation. Pixel at u=0.5,v=0.5 gets
    // exact center props. Pixels near edges blend with the appropriate neighbor.
    auto interp_props = [&](float u, float v) -> TileProps {
        // Determine which quadrant: pick the 4 tile centers forming a box around (u,v)
        // u < 0.5 → left half (center + W), u >= 0.5 → right half (center + E)
        float su = u - 0.5f;
        float sv = v - 0.5f;
        float wu = std::abs(su);
        float wv = std::abs(sv);
        const TileProps& h_nb = (su >= 0) ? tp_E : tp_W;
        const TileProps& v_nb = (sv >= 0) ? tp_S : tp_N;
        const TileProps& d_nb = (su >= 0) ? ((sv >= 0) ? tp_SE : tp_NE) : ((sv >= 0) ? tp_SW : tp_NW);
        return interp_tile_props(tp_center, h_nb, v_nb, d_nb, wu, wv);
    };

    if (cls.family == TileFamily::PureTerrain) {
        // ── Pure terrain: material pattern + hillshade + hypsometric ──
        if (self_mat == Mat::Water) {
            // For water tiles, compute depth from a 3×3 smoothed elevation
            // to eliminate tile-grid-aligned shelf banding. The smoothed
            // elevation is then bilinearly interpolated per-pixel.
            auto smooth_elev = [&](int sx, int sy) -> float {
                float sum = 0; int cnt = 0;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        int nx = std::clamp(sx + dx, 0, w - 1);
                        int ny = std::clamp(sy + dy, 0, h - 1);
                        sum += terrain.tile_at(static_cast<uint32_t>(nx),
                                               static_cast<uint32_t>(ny)).elev01;
                        cnt++;
                    }
                }
                return sum / static_cast<float>(cnt);
            };
            // Smoothed elevation at the 4 tile corners for bilinear interp
            float se_nw = smooth_elev(tx, ty);
            float se_ne = smooth_elev(tx + 1, ty);
            float se_sw = smooth_elev(tx, ty + 1);
            float se_se = smooth_elev(tx + 1, ty + 1);

            for (int y = 0; y < SZ; ++y) {
                float v = (static_cast<float>(y) + 0.5f) / 16.0f;
                for (int x = 0; x < SZ; ++x) {
                    float u = (static_cast<float>(x) + 0.5f) / 16.0f;
                    // Bilinear interpolation of smoothed elevation
                    float elev_smooth = se_nw * (1 - u) * (1 - v) + se_ne * u * (1 - v) +
                                        se_sw * (1 - u) * v + se_se * u * v;
                    float depth = std::max(0.0f, water_level - elev_smooth);
                    out_16x16[y * SZ + x] =
                        pack_rgba(render_water_pixel(tx * SZ + x, ty * SZ + y, depth));
                }
            }
        } else {
            for (int y = 0; y < SZ; ++y) {
                float v = (static_cast<float>(y) + 0.5f) / 16.0f;
                for (int x = 0; x < SZ; ++x) {
                    float u = (static_cast<float>(x) + 0.5f) / 16.0f;
                    PatVar pv_px;
                    pv_px.color_shift = cs_nw * (1 - u) * (1 - v) + cs_ne * u * (1 - v) +
                                        cs_sw * (1 - u) * v + cs_se * u * v;
                    pv_px.detail_mod = dm_nw * (1 - u) * (1 - v) + dm_ne * u * (1 - v) +
                                       dm_sw * (1 - u) * v + dm_se * u * v;
                    TileProps tp_px = interp_props(u, v);
                    int gpx = tx * SZ + x;
                    int gpy = ty * SZ + y;
                    Col3 col =
                        render_land_pixel(gpx, gpy, self_mat, pv_px, tp_px, water_level, seed);

                    // Subtle micro-relief on PureTerrain: slope-gated brightness
                    // variation using 4px-scale correlated noise. Gives non-sloping
                    // tiles some visual texture so they don't look starkly flat
                    // compared to transition tiles.
                    float relief_h = hash01(gpx / 4, gpy / 4, seed + 60000u);
                    float relief = (relief_h - 0.5f) * 0.04f;
                    // Scale by slope: flat tiles get more relief texture (they need it),
                    // steep tiles already have transition rendering.
                    float slope_inv = std::clamp(1.0f - tp_px.slope01 * 3.0f, 0.2f, 1.0f);
                    col = col * (1.0f + relief * slope_inv);

                    out_16x16[y * SZ + x] = pack_rgba(col);
                }
            }
        }
        return;
    }

    // ── Transition tile (Coast or Elevation) ─────────────────────────
    int entry_idx = static_cast<int>(cls.edges.entry);
    int exit_idx = static_cast<int>(cls.edges.exit);
    const RotationEntry& rot = atlas.rotation_lut[entry_idx][exit_idx];

    int shape_variant = variant % TemplateAtlas::SHAPE_VARIANTS;

    // Determine the land material for coast tiles
    Mat land_mat = self_mat;
    if (cls.family == TileFamily::Coast && land_mat == Mat::Water) {
        // Find nearest land material
        land_mat = Mat::SoilLoam;
        for (auto [ddx, ddy] :
             std::initializer_list<std::pair<int, int>>{{0, -1}, {1, 0}, {0, 1}, {-1, 0}}) {
            int nx = tx + ddx, ny = ty + ddy;
            if (nx >= 0 && ny >= 0 && nx < w && ny < h) {
                const auto& nt = terrain.tile_at(static_cast<uint32_t>(nx), static_cast<uint32_t>(ny));
                if (!nt.is_ocean) {
                    land_mat = classify_tile_mat(nt, water_level);
                    break;
                }
            }
        }
    }

    // Reconstruct the bezier curve from the canonical pair + variant.
    // Edge midpoints at 7.5 (grid center) so rotation maps midpoints exactly.
    // Control points constrained for perpendicular entry/exit (no kinks at tile edges).
    Vec2 p0_canon, p1_canon, p2_canon;
    if (rot.canonical == CanonicalPair::NS) {
        p0_canon = {7.5f, 0.0f};
        p2_canon = {7.5f, 15.0f};
        float y_offsets[] = {0.0f, -2.0f, 2.0f};
        p1_canon = {7.5f, 7.5f + y_offsets[shape_variant]};
    } else {
        p0_canon = {7.5f, 0.0f};
        p2_canon = {15.0f, 7.5f};
        // P1 = (7.5, 7.5) ensures perpendicular entry at N and exit at E
        (void)shape_variant;
        p1_canon = {7.5f, 7.5f};
    }

    // Transform curve points by rotation + flip to match the actual edge pair
    auto transform_point = [&](Vec2 pt) -> Vec2 {
        if (rot.flip) {
            pt.x = 15.0f - pt.x;
        }
        int steps = ((rot.rotation_steps % 4) + 4) % 4;
        for (int s = 0; s < steps; ++s) {
            float nx = 15.0f - pt.y;
            float ny = pt.x;
            pt = {nx, ny};
        }
        return pt;
    };

    Vec2 p0 = transform_point(p0_canon);
    Vec2 p1 = transform_point(p1_canon);
    Vec2 p2 = transform_point(p2_canon);

    // Perturb control point with per-tile hash noise. ±3.5px offset makes
    // contour lines feel organic. Endpoints p0/p2 stay at edge midpoints
    // for seamless tiling — only the control point p1 moves.
    {
        float perturb_x = (hash01(tx, ty, seed + 55000u) - 0.5f) * 7.0f;
        float perturb_y = (hash01(tx, ty, seed + 55100u) - 0.5f) * 7.0f;
        p1.x = std::clamp(p1.x + perturb_x, 2.0f, 13.0f);
        p1.y = std::clamp(p1.y + perturb_y, 2.0f, 13.0f);
    }

    // ── Determine water side by checking a known-underwater corner ──────
    // Instead of using flipped_polarity (which can be inconsistent between
    // adjacent tiles), we find a corner that's actually underwater and check
    // which side of the bezier curve it falls on. That sign = water side.
    // This is guaranteed consistent because adjacent tiles sharing a boundary
    // will agree on which physical corner is underwater.

    auto safe_elev = [&](int cx, int cy) -> float {
        cx = std::clamp(cx, 0, w - 1);
        cy = std::clamp(cy, 0, h - 1);
        return terrain.tile_at(static_cast<uint32_t>(cx), static_cast<uint32_t>(cy)).elev01;
    };

    // Check all 4 corners to find one that's clearly underwater
    struct CornerInfo {
        float elev;
        Vec2 pixel_pos;  // position in tile pixel coords
    };
    CornerInfo corners[4] = {
        {safe_elev(tx, ty), {0.5f, 0.5f}},          // NW
        {safe_elev(tx + 1, ty), {15.5f, 0.5f}},     // NE
        {safe_elev(tx, ty + 1), {0.5f, 15.5f}},     // SW
        {safe_elev(tx + 1, ty + 1), {15.5f, 15.5f}}, // SE
    };

    // Find the water sign: the bezier_side sign of an underwater corner
    float water_sign = 1.0f;  // default
    if (cls.family == TileFamily::Coast) {
        float best_depth = 0.0f;
        for (auto& c : corners) {
            float d = water_level - c.elev;
            if (d > best_depth) {
                best_depth = d;
                water_sign = bezier_side(c.pixel_pos, p0, p1, p2);
            }
        }
        // Fallback: if no corner is underwater (edge case at terrain boundaries
        // where safe_elev clamps), use flipped_polarity from classification
        if (best_depth <= 0.0f) {
            water_sign = cls.flipped_polarity ? -1.0f : 1.0f;
        }
        // Normalize to +1 or -1
        water_sign = (water_sign >= 0.0f) ? 1.0f : -1.0f;
    } else {
        // Elevation transition: find the LOW corner (below threshold)
        // Use the entry edge corner that's on the low side
        float best_low = 1.0f;
        for (auto& c : corners) {
            if (c.elev < best_low) {
                best_low = c.elev;
                water_sign = bezier_side(c.pixel_pos, p0, p1, p2);
            }
        }
        water_sign = (water_sign >= 0.0f) ? 1.0f : -1.0f;
    }

    for (int y = 0; y < SZ; ++y) {
        float v = (static_cast<float>(y) + 0.5f) / 16.0f;
        for (int x = 0; x < SZ; ++x) {
            float u = (static_cast<float>(x) + 0.5f) / 16.0f;
            PatVar pv_px;
            pv_px.color_shift = cs_nw * (1 - u) * (1 - v) + cs_ne * u * (1 - v) +
                                cs_sw * (1 - u) * v + cs_se * u * v;
            pv_px.detail_mod = dm_nw * (1 - u) * (1 - v) + dm_ne * u * (1 - v) +
                               dm_sw * (1 - u) * v + dm_se * u * v;
            TileProps tp_px = interp_props(u, v);

            int idx = y * SZ + x;
            int gpx = tx * SZ + x;
            int gpy = ty * SZ + y;

            Vec2 p = {static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f};
            auto [dist, side] = bezier_dist_side(p, p0, p1, p2);

            // is_water_side: true if this pixel is on the same side as the underwater corner
            bool is_water_side = (side * water_sign > 0.0f);

            Col3 col;
            float px_depth = std::max(0.0f, water_level - tp_px.elev01);
            if (cls.family == TileFamily::Coast) {
                // Use interpolated elevation as the distance proxy for coast
                // transitions. This follows the continuous terrain field and
                // produces smooth coastlines rather than tile-aligned bezier stairs.
                // elev_margin: positive = above water (land), negative = below (water)
                float elev_margin = tp_px.elev01 - water_level;
                // Scale elevation difference to pixel-like units for the transition.
                // At 16px/tile with typical elevation range across a coast tile,
                // ~100× scaling maps ~0.06 elevation span to ~6px transition width.
                float elev_dist = elev_margin * 100.0f;

                float zone_width;
                switch (cls.grade) {
                    case TransitionGrade::Shallow: zone_width = 6.0f; break;
                    case TransitionGrade::Medium:  zone_width = 4.0f; break;
                    case TransitionGrade::Steep:   zone_width = 2.5f; break;
                    default:                       zone_width = 4.0f; break;
                }

                float abs_dist = std::abs(elev_dist);
                if (abs_dist < zone_width) {
                    float side_sign = (elev_margin >= 0) ? 1.0f : -1.0f;
                    col = coast_transition_color(abs_dist, side_sign, cls.grade, land_mat, gpx, gpy,
                                                 pv_px, px_depth, tp_px, water_level, seed);
                } else if (elev_margin < 0) {
                    col = render_water_pixel(gpx, gpy, px_depth);
                } else {
                    col = render_land_pixel(gpx, gpy, land_mat, pv_px, tp_px, water_level, seed);
                }
            } else {
                // Elevation transition: smooth continuous gradient across the tile.
                // No hard edge at the bezier — just a smooth brightness ramp
                // from high side (bright) to low side (dark). The curve position
                // only determines where the midpoint of the ramp falls.
                col = render_land_pixel(gpx, gpy, self_mat, pv_px, tp_px, water_level, seed);

                // Three structurally different treatments per grade:
                // Shallow: broad subtle gradient, no edges (rolling hill)
                // Medium: monotonic ramp shading (incline/ramp)
                // Steep: dual-rim ledge + cast shadow (impassable wall)
                bool is_low_side = is_water_side;
                float sd = is_low_side ? -dist : dist;  // +high, -low

                // All grades use full-tile gradients. The boundary position
                // determines where the midpoint falls; the entire tile shades
                // from bright (high side) to dark (low side).
                // sd: positive = high side, negative = low side
                // Normalized to [-1, +1] across the tile using tanh.

                if (cls.grade == TransitionGrade::Shallow) {
                    // ── Shallow: gentle full-tile gradient, no edge ──
                    float ramp = std::tanh(sd * 0.12f);  // broad S-curve
                    col = col * (1.0f + 0.12f * ramp);   // ±12% brightness

                } else if (cls.grade == TransitionGrade::Medium) {
                    // ── Medium: steeper full-tile gradient + breakline ──
                    float ramp = std::tanh(sd * 0.20f);  // steeper S-curve
                    col = col * (1.0f + 0.20f * ramp);   // ±20% brightness
                    // Breakline darkness modulated by hillshade (light-aware)
                    if (dist < 1.2f) {
                        float line_t = 1.0f - dist / 1.2f;
                        float break_dark = 0.12f + 0.12f * (1.0f - tp_px.shade);
                        col = col * (1.0f - break_dark * line_t);
                    }
                    // Cool tint on low side (full-tile, fading with distance)
                    if (is_low_side) {
                        float cool_t = std::clamp(dist / 8.0f, 0.0f, 1.0f);
                        col.b += (1.0f - cool_t) * 5.0f;
                    }

                } else {
                    // ── Steep: hard terrace step (impassable cliff) ──
                    // Two-surface model: flat top plane + vertical face + cast shadow.
                    // The cliff face is dark and flat (minimal texture noise).
                    if (!is_low_side && dist < 1.5f) {
                        // CAP: continuous bright edge along cliff top (1.5px)
                        float cap_t = 1.0f - dist / 1.5f;
                        col = {std::min(255.0f, col.r + cap_t * 40.0f),
                               std::min(255.0f, col.g + cap_t * 30.0f),
                               col.b + cap_t * 6.0f};
                    } else if (!is_low_side && dist >= 1.5f && dist < 2.0f) {
                        // Hard 0.5px edge line (clean terrace step)
                        col = col * 0.55f;
                    } else if (is_low_side && dist < 5.0f) {
                        // CLIFF FACE + CAST SHADOW: 5px dark zone below edge.
                        // Flatten texture (reduce material contrast on the face)
                        // to make the silhouette read as a wall.
                        float face_t = dist / 5.0f;  // 0 at edge, 1 at far end
                        float shadow = 0.50f * (1.0f - face_t * face_t);  // quadratic falloff
                        // Flatten toward a mid-tone (reduce material noise)
                        float mid_r = col.r * 0.7f + 90.0f * 0.3f;
                        float mid_g = col.g * 0.7f + 85.0f * 0.3f;
                        float mid_b = col.b * 0.7f + 95.0f * 0.3f;
                        col = {mid_r * (1.0f - shadow),
                               mid_g * (1.0f - shadow),
                               mid_b * (1.0f - shadow) + shadow * 12.0f};
                    }
                    // High side beyond cap: normal land (already rendered)
                    // Low side beyond shadow: normal land
                }
            }

            col.r = std::clamp(col.r, 0.0f, 255.0f);
            col.g = std::clamp(col.g, 0.0f, 255.0f);
            col.b = std::clamp(col.b, 0.0f, 255.0f);
            out_16x16[idx] = pack_rgba(col);
        }
    }
}

// ── Main tile renderer with edge blending ───────────────────────────

Col3 unpack_col(uint32_t px) {
    return {static_cast<float>(px & 0xFF), static_cast<float>((px >> 8) & 0xFF),
            static_cast<float>((px >> 16) & 0xFF)};
}

float smoothstep_f(float edge0, float edge1, float x) {
    float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

void render_tile_from_atlas(const Terrain& terrain, const TemplateAtlas& atlas, int tx, int ty,
                            uint32_t seed, float water_level, uint32_t* out_16x16) {
    constexpr int SZ = 16;
    constexpr int BLEND_W = 6;  // blend zone width in pixels from tile edge
    int w = static_cast<int>(terrain.width);
    int h = static_cast<int>(terrain.height);

    // Render this tile's raw pixels
    render_tile_raw(terrain, atlas, tx, ty, seed, water_level, out_16x16);

    // ── Universal edge blend pass ──────────────────────────────────────
    // For each of 4 cardinal neighbors, if the neighboring tile has a different
    // material or tile family, render the neighbor's raw edge strip and blend
    // pixels near the shared boundary. This eliminates hard color transitions
    // at ALL tile boundaries (land↔water, land↔land, coast↔pureterrain, etc.)

    // Get this tile's classification for skip-check
    Mat self_mat = Mat::Water;
    TileFamily self_family = TileFamily::PureTerrain;
    float self_elev = 0.0f;
    if (tx >= 0 && ty >= 0 && tx < w && ty < h) {
        const auto& tile = terrain.tile_at(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty));
        self_mat = classify_tile_mat(tile, water_level);
        self_family = classify_tile(terrain, tx, ty, water_level).family;
        self_elev = tile.elev01;
    }

    // Weighted accumulation buffers — eliminates order-dependent corner bias
    // by gathering all neighbor contributions before blending.
    struct BlendAccum {
        float r = 0, g = 0, b = 0;
        float weight = 0;
    };
    BlendAccum accum[SZ * SZ] = {};

    uint32_t nb_buf[SZ * SZ];

    float self_slope = 0.0f;
    if (tx >= 0 && ty >= 0 && tx < w && ty < h)
        self_slope = terrain.tile_at(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty)).slope01;

    auto need_blend_check = [&](int nx, int ny) -> bool {
        if (nx < 0 || ny < 0 || nx >= w || ny >= h)
            return false;
        const auto& nb_tile = terrain.tile_at(static_cast<uint32_t>(nx), static_cast<uint32_t>(ny));
        Mat nb_mat = classify_tile_mat(nb_tile, water_level);
        TileFamily nb_family = classify_tile(terrain, nx, ny, water_level).family;

        if ((nb_mat != self_mat) || (nb_family != self_family) ||
            (self_family != TileFamily::PureTerrain) || (nb_family != TileFamily::PureTerrain))
            return true;
        // Blend same-material tiles when elevation or slope differs
        // (catches hillshade and hypsometric tint discontinuities)
        float elev_diff = std::abs(self_elev - nb_tile.elev01);
        float slope_diff = std::abs(self_slope - nb_tile.slope01);
        if (self_mat == Mat::Water)
            return (elev_diff > 0.02f);
        return (elev_diff > 0.01f) || (slope_diff > 0.02f);
    };

    // ── Cardinal neighbors (N=0, E=1, S=2, W=3) ───────────────────────
    constexpr int DX[4] = {0, 1, 0, -1};
    constexpr int DY[4] = {-1, 0, 1, 0};

    for (int dir = 0; dir < 4; ++dir) {
        int nx = tx + DX[dir];
        int ny = ty + DY[dir];
        if (!need_blend_check(nx, ny))
            continue;

        render_tile_raw(terrain, atlas, nx, ny, seed, water_level, nb_buf);

        for (int i = 0; i < SZ; ++i) {
            for (int d = 0; d < BLEND_W; ++d) {
                float t = 1.0f - smoothstep_f(0.0f, static_cast<float>(BLEND_W),
                                              static_cast<float>(d) + 0.5f);
                t *= 0.6f;  // max 60% blend at boundary for smoother transitions

                int sx, sy, nbx, nby;
                switch (dir) {
                    case 0: sx = i; sy = d; nbx = i; nby = SZ - 1 - d; break;
                    case 1: sx = SZ - 1 - d; sy = i; nbx = d; nby = i; break;
                    case 2: sx = i; sy = SZ - 1 - d; nbx = i; nby = d; break;
                    case 3:
                    default: sx = d; sy = i; nbx = SZ - 1 - d; nby = i; break;
                }

                Col3 nb_col = unpack_col(nb_buf[nby * SZ + nbx]);
                auto& a = accum[sy * SZ + sx];
                a.r += nb_col.r * t;
                a.g += nb_col.g * t;
                a.b += nb_col.b * t;
                a.weight += t;
            }
        }
    }

    // ── Diagonal corners (NW=0, NE=1, SE=2, SW=3) ─────────────────────
    constexpr int CDX[4] = {-1, 1, 1, -1};
    constexpr int CDY[4] = {-1, -1, 1, 1};

    for (int corner = 0; corner < 4; ++corner) {
        int cnx = tx + CDX[corner];
        int cny = ty + CDY[corner];
        if (!need_blend_check(cnx, cny))
            continue;

        render_tile_raw(terrain, atlas, cnx, cny, seed, water_level, nb_buf);

        int base_sx = (CDX[corner] < 0) ? 0 : SZ - 1;
        int base_sy = (CDY[corner] < 0) ? 0 : SZ - 1;
        int base_nx = (CDX[corner] < 0) ? SZ - 1 : 0;
        int base_ny = (CDY[corner] < 0) ? SZ - 1 : 0;
        int step_sx = (CDX[corner] < 0) ? 1 : -1;
        int step_sy = (CDY[corner] < 0) ? 1 : -1;
        int step_nx = (CDX[corner] < 0) ? -1 : 1;
        int step_ny = (CDY[corner] < 0) ? -1 : 1;

        for (int dx = 0; dx < BLEND_W; ++dx) {
            for (int dy = 0; dy < BLEND_W; ++dy) {
                float dist = std::sqrt(static_cast<float>(dx * dx + dy * dy));
                float t = 1.0f - smoothstep_f(0.0f, static_cast<float>(BLEND_W), dist);
                t *= 0.35f;  // weaker than cardinal
                if (t <= 0.001f)
                    continue;

                int sx = base_sx + dx * step_sx;
                int sy = base_sy + dy * step_sy;
                int nbx = base_nx + dx * step_nx;
                int nby = base_ny + dy * step_ny;

                Col3 nb_col = unpack_col(nb_buf[nby * SZ + nbx]);
                auto& a = accum[sy * SZ + sx];
                a.r += nb_col.r * t;
                a.g += nb_col.g * t;
                a.b += nb_col.b * t;
                a.weight += t;
            }
        }
    }

    // ── Apply accumulated blends ───────────────────────────────────────
    for (int i = 0; i < SZ * SZ; ++i) {
        if (accum[i].weight <= 0.001f)
            continue;
        Col3 self_col = unpack_col(out_16x16[i]);
        float w_total = accum[i].weight;
        // Weighted average of all neighbor contributions
        Col3 nb_avg = {accum[i].r / w_total, accum[i].g / w_total, accum[i].b / w_total};
        // Blend strength: total weight capped at 60% to keep self dominant
        float blend_t = std::min(w_total, 0.6f);
        out_16x16[i] = pack_rgba(lerp_col(self_col, nb_avg, blend_t));
    }
}

// Box-filter downsample a 16×16 tile to smaller resolution
void downsample_tile(const uint32_t* src_16x16, uint32_t* out, int out_ppt) {
    assert(out_ppt > 0 && 16 % out_ppt == 0 && "out_ppt must evenly divide 16");
    int factor = 16 / out_ppt;
    float inv_area = 1.0f / static_cast<float>(factor * factor);

    for (int oy = 0; oy < out_ppt; ++oy) {
        for (int ox = 0; ox < out_ppt; ++ox) {
            float r = 0, g = 0, b = 0;
            int sy = oy * factor;
            int sx = ox * factor;
            for (int dy = 0; dy < factor; ++dy) {
                for (int dx = 0; dx < factor; ++dx) {
                    uint32_t px = src_16x16[(sy + dy) * 16 + (sx + dx)];
                    r += static_cast<float>(px & 0xFF);
                    g += static_cast<float>((px >> 8) & 0xFF);
                    b += static_cast<float>((px >> 16) & 0xFF);
                }
            }
            r *= inv_area;
            g *= inv_area;
            b *= inv_area;
            out[oy * out_ppt + ox] = pack_rgba(static_cast<uint8_t>(r), static_cast<uint8_t>(g),
                                               static_cast<uint8_t>(b));
        }
    }
}

}  // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
// Public API
// ═══════════════════════════════════════════════════════════════════════════

uint32_t eval_terrain_pixel(const Terrain& terrain, float world_x, float world_y,
                            float /*pixels_per_tile*/, uint32_t seed, float water_level) {
    assert(g_atlas.valid && "generate_template_atlas() must be called before rendering");

    int tx =
        std::clamp(static_cast<int>(std::floor(world_x)), 0, static_cast<int>(terrain.width) - 1);
    int ty =
        std::clamp(static_cast<int>(std::floor(world_y)), 0, static_cast<int>(terrain.height) - 1);

    // Cache the last rendered tile to avoid re-rendering 256 pixels for single-pixel queries
    thread_local int cached_tx = -1, cached_ty = -1;
    thread_local uint32_t cached_seed = 0;
    thread_local uint32_t tile_buf[16 * 16];

    if (tx != cached_tx || ty != cached_ty || seed != cached_seed) {
        render_tile_from_atlas(terrain, g_atlas, tx, ty, seed, water_level, tile_buf);
        cached_tx = tx;
        cached_ty = ty;
        cached_seed = seed;
    }

    float fx = (world_x - static_cast<float>(tx)) * 16.0f;
    float fy = (world_y - static_cast<float>(ty)) * 16.0f;
    int px = std::clamp(static_cast<int>(fx), 0, 15);
    int py = std::clamp(static_cast<int>(fy), 0, 15);
    return tile_buf[py * 16 + px];
}

void render_terrain_region(const Terrain& terrain, float world_x0, float world_y0,
                           float world_per_pixel, int out_w, int out_h, uint32_t seed,
                           uint32_t* out_pixels, int out_stride, float water_level,
                           BakeProgressFn progress, const std::atomic<bool>* cancel) {
    assert(g_atlas.valid && "generate_template_atlas() must be called before rendering");
    const TemplateAtlas& atlas = g_atlas;

    float ppt = 1.0f / world_per_pixel;  // pixels per tile in output

    int tw = static_cast<int>(terrain.width);
    int th = static_cast<int>(terrain.height);

    // Determine rendering strategy based on resolution
    // At native 16 px/tile: blit directly from template
    // At 8 px/tile: downsample 16→8
    // At 4 px/tile: downsample 16→4

    // For each output pixel, compute which tile and sub-tile position it maps to.
    // But we render tile-by-tile for cache efficiency.

    // Compute the range of tiles visible in the output region
    float world_x1 = world_x0 + static_cast<float>(out_w) * world_per_pixel;
    float world_y1 = world_y0 + static_cast<float>(out_h) * world_per_pixel;
    int tile_x0 = std::max(0, static_cast<int>(std::floor(world_x0)));
    int tile_y0 = std::max(0, static_cast<int>(std::floor(world_y0)));
    int tile_x1 = std::min(tw - 1, static_cast<int>(std::floor(world_x1)));
    int tile_y1 = std::min(th - 1, static_cast<int>(std::floor(world_y1)));

    // Pre-render tiles and blit to output
    // At ~16 px/tile, we blit tile pixels directly.
    // At ~8 px/tile, we downsample each tile to 8×8.
    // At ~4 px/tile, we downsample each tile to 4×4.

    int effective_ppt;
    if (ppt >= 12.0f) {
        effective_ppt = 16;
    } else if (ppt >= 6.0f) {
        effective_ppt = 8;
    } else {
        effective_ppt = 4;
    }

    // Thread worker function
    auto render_rows = [&](int ty_start, int ty_end) {
        uint32_t tile_buf[16 * 16];
        uint32_t ds_buf[16 * 16];  // downsampled tile (max 16×16)

        for (int ty = ty_start; ty <= ty_end; ++ty) {
            if (cancel && cancel->load(std::memory_order_relaxed))
                return;

            for (int tx = tile_x0; tx <= tile_x1; ++tx) {
                // Render the full 16×16 tile
                render_tile_from_atlas(terrain, atlas, tx, ty, seed, water_level, tile_buf);

                // Downsample if needed
                const uint32_t* src = tile_buf;
                int src_ppt = 16;
                if (effective_ppt < 16) {
                    downsample_tile(tile_buf, ds_buf, effective_ppt);
                    src = ds_buf;
                    src_ppt = effective_ppt;
                }

                // Blit to output buffer
                // World-space bounds of this tile
                float tile_wx0 = static_cast<float>(tx);
                float tile_wy0 = static_cast<float>(ty);

                // Output pixel range for this tile
                float ox0f = (tile_wx0 - world_x0) * ppt;
                float oy0f = (tile_wy0 - world_y0) * ppt;
                int ox0 = static_cast<int>(std::round(ox0f));
                int oy0 = static_cast<int>(std::round(oy0f));
                int tile_out_w = static_cast<int>(std::round(ppt));
                int tile_out_h = static_cast<int>(std::round(ppt));
                if (tile_out_w < 1)
                    tile_out_w = 1;
                if (tile_out_h < 1)
                    tile_out_h = 1;

                for (int py = 0; py < tile_out_h; ++py) {
                    int out_y = oy0 + py;
                    if (out_y < 0 || out_y >= out_h)
                        continue;

                    // Map output pixel to source tile pixel
                    int sy = py * src_ppt / tile_out_h;
                    if (sy >= src_ppt)
                        sy = src_ppt - 1;

                    for (int px = 0; px < tile_out_w; ++px) {
                        int out_x = ox0 + px;
                        if (out_x < 0 || out_x >= out_w)
                            continue;

                        int sx = px * src_ppt / tile_out_w;
                        if (sx >= src_ppt)
                            sx = src_ppt - 1;

                        out_pixels[out_y * out_stride + out_x] = src[sy * src_ppt + sx];
                    }
                }
            }
        }
    };

    // Use threads for parallel rendering
    unsigned int n_threads = std::max(1u, std::thread::hardware_concurrency());
    int total_tile_rows = tile_y1 - tile_y0 + 1;
    n_threads = std::min(n_threads, static_cast<unsigned int>(std::max(total_tile_rows, 1)));

    if (n_threads <= 1) {
        // Single-threaded with optional progress
        for (int ty = tile_y0; ty <= tile_y1; ++ty) {
            if (cancel && cancel->load(std::memory_order_relaxed))
                return;
            render_rows(ty, ty);
            if (progress && ((ty - tile_y0) % 4 == 0 || ty == tile_y1)) {
                progress(ty - tile_y0 + 1, total_tile_rows);
            }
        }
        return;
    }

    // Multi-threaded
    std::atomic<int> rows_done{0};
    std::vector<std::thread> threads;
    threads.reserve(n_threads);

    int rows_per_thread = total_tile_rows / static_cast<int>(n_threads);
    int remainder = total_tile_rows % static_cast<int>(n_threads);

    int row_start = tile_y0;
    for (unsigned int t = 0; t < n_threads; ++t) {
        int chunk = rows_per_thread + (static_cast<int>(t) < remainder ? 1 : 0);
        int row_end = row_start + chunk - 1;

        threads.emplace_back([&, row_start, row_end]() {
            uint32_t local_tile_buf[16 * 16];
            uint32_t local_ds_buf[16 * 16];

            for (int ty = row_start; ty <= row_end; ++ty) {
                if (cancel && cancel->load(std::memory_order_relaxed))
                    return;

                for (int tx = tile_x0; tx <= tile_x1; ++tx) {
                    render_tile_from_atlas(terrain, atlas, tx, ty, seed, water_level,
                                           local_tile_buf);

                    const uint32_t* src = local_tile_buf;
                    int src_ppt = 16;
                    if (effective_ppt < 16) {
                        downsample_tile(local_tile_buf, local_ds_buf, effective_ppt);
                        src = local_ds_buf;
                        src_ppt = effective_ppt;
                    }

                    float tile_wx0 = static_cast<float>(tx);
                    float tile_wy0 = static_cast<float>(ty);
                    float ox0f = (tile_wx0 - world_x0) * ppt;
                    float oy0f = (tile_wy0 - world_y0) * ppt;
                    int ox0 = static_cast<int>(std::round(ox0f));
                    int oy0 = static_cast<int>(std::round(oy0f));
                    int tile_out_w = static_cast<int>(std::round(ppt));
                    int tile_out_h = static_cast<int>(std::round(ppt));
                    if (tile_out_w < 1)
                        tile_out_w = 1;
                    if (tile_out_h < 1)
                        tile_out_h = 1;

                    for (int py = 0; py < tile_out_h; ++py) {
                        int out_y = oy0 + py;
                        if (out_y < 0 || out_y >= out_h)
                            continue;
                        int sy = py * src_ppt / tile_out_h;
                        if (sy >= src_ppt)
                            sy = src_ppt - 1;
                        for (int px = 0; px < tile_out_w; ++px) {
                            int out_x = ox0 + px;
                            if (out_x < 0 || out_x >= out_w)
                                continue;
                            int sx = px * src_ppt / tile_out_w;
                            if (sx >= src_ppt)
                                sx = src_ppt - 1;
                            out_pixels[out_y * out_stride + out_x] = src[sy * src_ppt + sx];
                        }
                    }
                }
                rows_done.fetch_add(1, std::memory_order_relaxed);
            }
        });
        row_start = row_end + 1;
    }

    if (progress) {
        while (true) {
            int done = rows_done.load(std::memory_order_relaxed);
            progress(done, total_tile_rows);
            if (done >= total_tile_rows)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    for (auto& t : threads)
        t.join();
    if (progress) progress(total_tile_rows, total_tile_rows);
}

// ═══════════════════════════════════════════════════════════════════════════
// Legacy API
// ═══════════════════════════════════════════════════════════════════════════

TilePixels generate_tile_texture(const Terrain& terrain, int tile_x, int tile_y, uint32_t seed,
                                 float water_level) {
    // Lazy init for legacy API callers (tests, tile-test mode)
    if (!g_atlas.valid) {
        generate_template_atlas(g_atlas, seed);
    }

    TilePixels out{};
    int tw = static_cast<int>(terrain.width);
    int th = static_cast<int>(terrain.height);
    if (tile_x < 0 || tile_y < 0 || tile_x >= tw || tile_y >= th)
        return out;

    render_tile_from_atlas(terrain, g_atlas, tile_x, tile_y, seed, water_level, out.pixels);
    return out;
}

void render_tile_patch(const Terrain& terrain, int patch_x, int patch_y, int patch_w, int patch_h,
                       uint32_t seed, uint32_t* out_pixels, int out_stride, float water_level) {
    if (!g_atlas.valid) {
        generate_template_atlas(g_atlas, seed);
    }

    constexpr int SZ = TilePixels::SIZE;
    uint32_t tile_buf[SZ * SZ];

    for (int ty = 0; ty < patch_h; ++ty) {
        for (int tx = 0; tx < patch_w; ++tx) {
            int wx = patch_x + tx;
            int wy = patch_y + ty;
            if (wx < 0 || wy < 0 || wx >= static_cast<int>(terrain.width) ||
                wy >= static_cast<int>(terrain.height))
                continue;

            render_tile_from_atlas(terrain, g_atlas, wx, wy, seed, water_level, tile_buf);

            for (int py = 0; py < SZ; ++py) {
                int out_y = ty * SZ + py;
                for (int px = 0; px < SZ; ++px) {
                    int out_x = tx * SZ + px;
                    out_pixels[out_y * out_stride + out_x] = tile_buf[py * SZ + px];
                }
            }
        }
    }
}

}  // namespace sandbox
