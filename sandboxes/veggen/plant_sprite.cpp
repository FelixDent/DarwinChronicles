#include "plant_sprite.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>
#include <vector>

namespace veggen {

namespace {

// ── Pixel utilities ──────────────────────────────────────────────────────────

uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) |
           (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(a) << 24);
}

void set_pixel(Sprite& s, int x, int y, uint32_t color) {
    if (x >= 0 && x < s.w && y >= 0 && y < s.h)
        s.pixels[static_cast<size_t>(y * s.w + x)] = color;
}

uint32_t get_pixel(const Sprite& s, int x, int y) {
    if (x >= 0 && x < s.w && y >= 0 && y < s.h)
        return s.pixels[static_cast<size_t>(y * s.w + x)];
    return 0;
}

uint32_t pixel_hash(int x, int y, uint32_t seed) {
    uint32_t h = static_cast<uint32_t>(x * 374761393 + y * 668265263) + seed;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return h;
}

// ── Material buffer ──────────────────────────────────────────────────────────

enum class Mat : uint8_t { Empty = 0, Wood = 1, Leaf = 2, Accent = 3, Shadow = 4 };

struct MatBuf {
    int w = 0, h = 0;
    std::vector<Mat> data;
    std::vector<uint8_t> wood_order;  // branch order for Wood pixels (0=trunk, 1=primary, 2=twig)
    void init(int w_, int h_) {
        w = w_;
        h = h_;
        data.assign(static_cast<size_t>(w * h), Mat::Empty);
        wood_order.assign(static_cast<size_t>(w * h), 0);
    }
    void set(int x, int y, Mat m) {
        if (x >= 0 && x < w && y >= 0 && y < h)
            data[static_cast<size_t>(y * w + x)] = m;
    }
    void set_wood(int x, int y, uint8_t order) {
        if (x >= 0 && x < w && y >= 0 && y < h)
            wood_order[static_cast<size_t>(y * w + x)] = order;
    }
    uint8_t get_order(int x, int y) const {
        if (x >= 0 && x < w && y >= 0 && y < h)
            return wood_order[static_cast<size_t>(y * w + x)];
        return 0;
    }
    Mat get(int x, int y) const {
        if (x >= 0 && x < w && y >= 0 && y < h)
            return data[static_cast<size_t>(y * w + x)];
        return Mat::Empty;
    }
};

void set_px(Sprite& s, MatBuf& mat, int x, int y, uint32_t color, Mat m) {
    set_pixel(s, x, y, color);
    mat.set(x, y, m);
}

// ── Vector helpers ───────────────────────────────────────────────────────────

struct Vec2 {
    float x, y;
};

Vec2 vec2_lerp(Vec2 a, Vec2 b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}

float vec2_len(Vec2 v) {
    return std::sqrt(v.x * v.x + v.y * v.y);
}

Vec2 vec2_rotate(Vec2 dir, float angle_rad) {
    float c = std::cos(angle_rad);
    float s = std::sin(angle_rad);
    return {dir.x * c - dir.y * s, dir.x * s + dir.y * c};
}

// ── Lighting helpers ─────────────────────────────────────────────────────────

int shade_index(float light) {
    // Clean banding — no per-pixel dithering noise
    if (light < 0.25f) return 0;
    if (light < 0.42f) return 1;
    if (light < 0.60f) return 2;
    if (light < 0.80f) return 3;
    return 4;
}

float surface_light(int px, int py, int cx, int cy, int rx, int ry) {
    constexpr float light_x = -0.6f;
    constexpr float light_y = -0.75f;
    float nx = (rx > 0) ? static_cast<float>(px - cx) / static_cast<float>(rx) : 0.0f;
    float ny = (ry > 0) ? static_cast<float>(py - cy) / static_cast<float>(ry) : 0.0f;
    float len = std::sqrt(nx * nx + ny * ny + 1.0f);
    nx /= len;
    ny /= len;
    float nz = 1.0f / len;
    constexpr float lz_raw = 0.4f;
    float ll = std::sqrt(light_x * light_x + light_y * light_y + lz_raw * lz_raw);
    float lx = light_x / ll;
    float ly = light_y / ll;
    float lz = lz_raw / ll;
    float dot = -(nx * lx + ny * ly) + nz * lz;
    return std::clamp(dot * 0.45f + 0.55f, 0.0f, 1.0f);
}

// ── Color palette ────────────────────────────────────────────────────────────

struct Palette {
    uint32_t leaf[5];   // [0]=deep shadow -> [4]=bright highlight
    uint32_t trunk[3];  // [0]=shadow, [1]=base, [2]=highlight
    uint32_t accent;
    uint32_t accent_dark;
    bool has_accent;
};

uint32_t shift_color(uint32_t base, float brightness, float cool_warm) {
    float r = static_cast<float>(base & 0xFF);
    float g = static_cast<float>((base >> 8) & 0xFF);
    float b = static_cast<float>((base >> 16) & 0xFF);

    r *= brightness;
    g *= brightness;
    b *= brightness;

    if (cool_warm < 0.0f) {
        float shift = -cool_warm * 25.0f;
        r -= shift * 0.6f;
        g -= shift * 0.2f;
        b += shift * 0.5f;
    } else if (cool_warm > 0.0f) {
        float shift = cool_warm * 25.0f;
        r += shift * 0.5f;
        g += shift * 0.3f;
        b -= shift * 0.4f;
    }

    return rgba(static_cast<uint8_t>(std::clamp(r, 0.0f, 255.0f)),
                static_cast<uint8_t>(std::clamp(g, 0.0f, 255.0f)),
                static_cast<uint8_t>(std::clamp(b, 0.0f, 255.0f)));
}

uint32_t hue_to_leaf_rgb(int hue_byte, int leafiness) {
    struct Anchor {
        float pos;
        float r, g, b;
    };
    static const Anchor anchors[] = {
        {0.00f, 170.0f, 150.0f, 45.0f},
        {0.18f, 135.0f, 140.0f, 35.0f},
        {0.30f, 28.0f, 110.0f, 20.0f},
        {0.40f, 48.0f, 145.0f, 32.0f},
        {0.52f, 20.0f, 62.0f, 38.0f},
        {0.65f, 72.0f, 98.0f, 58.0f},
        {1.00f, 85.0f, 108.0f, 78.0f},
    };
    constexpr int N = 7;

    float hf = std::clamp(static_cast<float>(hue_byte) / 255.0f, 0.0f, 1.0f);
    int i = 0;
    while (i < N - 2 && anchors[i + 1].pos < hf)
        ++i;

    float seg = std::max(anchors[i + 1].pos - anchors[i].pos, 0.001f);
    float t = std::clamp((hf - anchors[i].pos) / seg, 0.0f, 1.0f);

    float r = anchors[i].r + (anchors[i + 1].r - anchors[i].r) * t;
    float g = anchors[i].g + (anchors[i + 1].g - anchors[i].g) * t;
    float b = anchors[i].b + (anchors[i + 1].b - anchors[i].b) * t;

    float lf = static_cast<float>(leafiness) / 255.0f;
    float scale = 0.65f + lf * 0.50f;
    r = std::clamp(r * scale, 0.0f, 255.0f);
    g = std::clamp(g * scale, 0.0f, 255.0f);
    b = std::clamp(b * scale, 0.0f, 255.0f);

    return rgba(static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b));
}

Palette make_palette(const PlantTraits& traits, std::mt19937& rng,
                     float color_warmth = 0.0f) {
    Palette p{};

    std::uniform_int_distribution<int> shift(-5, 5);
    int hue = std::clamp(static_cast<int>(traits.base_hue) + shift(rng), 0, 255);
    uint32_t base = hue_to_leaf_rgb(hue, traits.leafiness);

    // Apply color warmth from environment adaptation
    // Positive warmth = yellow-green shift; negative = blue-green shift
    float warm_shift = color_warmth * 0.4f;
    p.leaf[0] = shift_color(base, 0.55f, -0.6f + warm_shift);
    p.leaf[1] = shift_color(base, 0.72f, -0.3f + warm_shift);
    p.leaf[2] = shift_color(base, 1.0f, warm_shift);
    p.leaf[3] = shift_color(base, 1.15f, 0.25f + warm_shift);
    p.leaf[4] = shift_color(base, 1.30f, 0.5f + warm_shift);

    float hf = static_cast<float>(hue) / 255.0f;
    std::uniform_int_distribution<int> bv(-8, 8);
    int bk = bv(rng);

    uint32_t trunk_base;
    if (hf < 0.25f) {
        trunk_base = rgba(static_cast<uint8_t>(std::clamp(125 + bk, 85, 165)),
                          static_cast<uint8_t>(std::clamp(95 + bk, 60, 130)),
                          static_cast<uint8_t>(std::clamp(55 + bk, 30, 85)));
    } else if (hf < 0.40f) {
        trunk_base = rgba(static_cast<uint8_t>(std::clamp(80 + bk, 50, 110)),
                          static_cast<uint8_t>(std::clamp(50 + bk, 30, 80)),
                          static_cast<uint8_t>(std::clamp(30 + bk, 15, 55)));
    } else if (hf < 0.55f) {
        trunk_base = rgba(static_cast<uint8_t>(std::clamp(95 + bk, 65, 125)),
                          static_cast<uint8_t>(std::clamp(68 + bk, 42, 95)),
                          static_cast<uint8_t>(std::clamp(38 + bk, 18, 60)));
    } else {
        trunk_base = rgba(static_cast<uint8_t>(std::clamp(120 + bk, 95, 150)),
                          static_cast<uint8_t>(std::clamp(100 + bk, 75, 130)),
                          static_cast<uint8_t>(std::clamp(75 + bk, 50, 105)));
    }

    p.trunk[0] = shift_color(trunk_base, 0.65f, -0.3f);
    p.trunk[1] = trunk_base;
    p.trunk[2] = shift_color(trunk_base, 1.25f, 0.2f);

    p.has_accent = false;
    std::uniform_int_distribution<int> roll(0, 99);
    switch (traits.archetype) {
        case PlantArchetype::Shrub:
            if (roll(rng) < 40) {
                static const uint32_t colors[] = {
                    rgba(220, 180, 200), rgba(240, 240, 220),
                    rgba(240, 220, 100), rgba(200, 140, 200),
                };
                p.accent = colors[roll(rng) % 4];
                p.accent_dark = shift_color(p.accent, 0.65f, -0.2f);
                p.has_accent = true;
            }
            break;
        case PlantArchetype::BroadleafTree:
            if (roll(rng) < 40) {
                static const uint32_t colors[] = {
                    rgba(160, 70, 55), rgba(175, 130, 45), rgba(145, 50, 60),
                };
                p.accent = colors[roll(rng) % 3];
                p.accent_dark = shift_color(p.accent, 0.65f, -0.2f);
                p.has_accent = true;
            }
            break;
        case PlantArchetype::Succulent:
            if (roll(rng) < 30) {
                static const uint32_t colors[] = {
                    rgba(230, 100, 120), rgba(230, 60, 60), rgba(240, 230, 80),
                };
                p.accent = colors[roll(rng) % 3];
                p.accent_dark = shift_color(p.accent, 0.65f, -0.2f);
                p.has_accent = true;
            }
            break;
        case PlantArchetype::Palm:
            if (roll(rng) < 55) {
                static const uint32_t colors[] = {
                    rgba(180, 140, 60),  // coconut
                    rgba(200, 160, 80),  // date
                    rgba(220, 80, 60),   // red palm fruit
                };
                p.accent = colors[roll(rng) % 3];
                p.accent_dark = shift_color(p.accent, 0.65f, -0.2f);
                p.has_accent = true;
            }
            break;
        case PlantArchetype::TropicalTree:
            if (roll(rng) < 60) {
                static const uint32_t colors[] = {
                    rgba(240, 60, 80),   // red tropical flower
                    rgba(255, 140, 0),   // orange blossom
                    rgba(200, 50, 200),  // orchid purple
                    rgba(255, 220, 50),  // golden fruit
                };
                p.accent = colors[roll(rng) % 4];
                p.accent_dark = shift_color(p.accent, 0.65f, -0.2f);
                p.has_accent = true;
            }
            break;
        default:
            break;
    }

    return p;
}

// ── Phase helpers ────────────────────────────────────────────────────────────

float foliage_fraction(GrowthPhase phase) {
    switch (phase) {
        case GrowthPhase::Small:
            return 0.15f;
        case GrowthPhase::Medium:
            return 0.65f;
        case GrowthPhase::Large:
            return 1.0f;
    }
    return 1.0f;
}

void get_canvas_size(PlantArchetype arch, GrowthPhase phase, int& w, int& h) {
    switch (arch) {
        case PlantArchetype::Conifer:
        case PlantArchetype::BroadleafTree:
            switch (phase) {
                case GrowthPhase::Small:
                    w = 16;
                    h = 24;
                    break;
                case GrowthPhase::Medium:
                    w = 32;
                    h = 44;
                    break;
                case GrowthPhase::Large:
                    w = 48;
                    h = 64;
                    break;
            }
            break;
        case PlantArchetype::Shrub:
            switch (phase) {
                case GrowthPhase::Small:
                    w = 16;
                    h = 20;
                    break;
                case GrowthPhase::Medium:
                    w = 28;
                    h = 32;
                    break;
                case GrowthPhase::Large:
                    w = 36;
                    h = 44;
                    break;
            }
            break;
        case PlantArchetype::Grass:
        case PlantArchetype::Succulent:
            switch (phase) {
                case GrowthPhase::Small:
                    w = 12;
                    h = 16;
                    break;
                case GrowthPhase::Medium:
                    w = 20;
                    h = 24;
                    break;
                case GrowthPhase::Large:
                    w = 28;
                    h = 32;
                    break;
            }
            break;
        case PlantArchetype::Moss:
            switch (phase) {
                case GrowthPhase::Small:
                    w = 16;
                    h = 10;
                    break;
                case GrowthPhase::Medium:
                    w = 24;
                    h = 14;
                    break;
                case GrowthPhase::Large:
                    w = 32;
                    h = 18;
                    break;
            }
            break;
        case PlantArchetype::Palm:
            switch (phase) {
                case GrowthPhase::Small:
                    w = 16;
                    h = 24;
                    break;
                case GrowthPhase::Medium:
                    w = 28;
                    h = 48;
                    break;
                case GrowthPhase::Large:
                    w = 40;
                    h = 64;
                    break;
            }
            break;
        case PlantArchetype::TropicalTree:
            switch (phase) {
                case GrowthPhase::Small:
                    w = 18;
                    h = 24;
                    break;
                case GrowthPhase::Medium:
                    w = 36;
                    h = 48;
                    break;
                case GrowthPhase::Large:
                    w = 48;
                    h = 56;
                    break;
            }
            break;
    }
}

// ── Plant graph structures ───────────────────────────────────────────────────

struct Segment {
    int parent;    // -1 for root
    Vec2 p0, p1;   // start/end in sprite coords
    float r0, r1;  // radius at p0/p1
    uint8_t order; // 0=trunk, 1=branch, 2=twig
};

enum class StampKind : uint8_t { LeafBlob, NeedleLayer, FlowerDot };

struct FoliageStamp {
    Vec2 pos;
    float rx, ry;
    StampKind kind;
    uint32_t seed;
};

// Frond: a curved leaf arc drawn as continuous leaf pixels with feather leaflets.
// Used for palm fronds — these are leaves, not wood.
struct FrondSpec {
    Vec2 base;      // attachment point (crown)
    Vec2 tip;       // endpoint before curvature
    float width;    // max leaflet half-width at widest point
    float curve;    // curvature: positive = droop further along length
    uint32_t seed;
};

struct TriangleEnvelope {
    bool active = false;
    float apex_x = 0, apex_y = 0;
    float base_y = 0;
    float base_half_w = 0;
    float margin = 1.5f;    // soft edge in pixels
    float waviness = 0.0f;  // 0=strict triangle, 1=undulating bushy profile
    uint32_t seed = 0;      // for deterministic wave pattern
};

struct PlantGraph {
    std::vector<Segment> segs;
    std::vector<FoliageStamp> stamps;
    std::vector<FrondSpec> fronds;
    TriangleEnvelope envelope;
};

// ── Curved trunk helper ─────────────────────────────────────────────────────
// Generates a polyline trunk with n_pts control points and slight curvature,
// then pushes multiple connected segments into the graph. Returns the index
// of the last segment and the final top point.

struct TrunkResult {
    int last_seg;
    Vec2 top;
};

TrunkResult gen_curved_trunk(PlantGraph& g, Vec2 base, float height, float r0, float r1,
                             int n_pts, float max_drift, std::mt19937& rng) {
    std::uniform_real_distribution<float> drift(-max_drift, max_drift);

    // Item 8: slight consistent trunk tilt (-3° to +3°)
    std::uniform_real_distribution<float> tilt_dist(-0.052f, 0.052f);  // ~3° in radians
    float tilt = tilt_dist(rng);

    // Build control points from base upward
    std::vector<Vec2> pts(static_cast<size_t>(n_pts));
    pts[0] = base;
    for (int i = 1; i < n_pts; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(n_pts - 1);
        float y = base.y - height * t;
        // Consistent tilt + per-point wobble
        float x = base.x + tilt * height * t + drift(rng) * t;
        pts[static_cast<size_t>(i)] = {x, y};
    }

    // Push segments
    int prev_seg = -1;
    for (int i = 0; i < n_pts - 1; ++i) {
        float t0 = static_cast<float>(i) / static_cast<float>(n_pts - 1);
        float t1 = static_cast<float>(i + 1) / static_cast<float>(n_pts - 1);
        float seg_r0 = r0 + (r1 - r0) * t0;
        float seg_r1 = r0 + (r1 - r0) * t1;
        int parent = (i == 0) ? -1 : prev_seg;
        g.segs.push_back({parent, pts[static_cast<size_t>(i)],
                          pts[static_cast<size_t>(i + 1)], seg_r0, seg_r1, 0});
        prev_seg = static_cast<int>(g.segs.size()) - 1;
    }

    return {prev_seg, pts[static_cast<size_t>(n_pts - 1)]};
}

// Helper: get a point along the trunk polyline at parameter t (0=base, 1=top)
Vec2 trunk_point_at(const PlantGraph& g, int first_trunk_seg, int num_trunk_segs, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    float seg_t = t * static_cast<float>(num_trunk_segs);
    int seg_idx = std::min(static_cast<int>(seg_t), num_trunk_segs - 1);
    float local_t = seg_t - static_cast<float>(seg_idx);
    const auto& seg = g.segs[static_cast<size_t>(first_trunk_seg + seg_idx)];
    return vec2_lerp(seg.p0, seg.p1, local_t);
}

// ── Graph rasterization ──────────────────────────────────────────────────────

void rasterize_segment(Sprite& s, MatBuf& mat, const Segment& seg, const Palette& pal) {
    float max_r = std::max(seg.r0, seg.r1);
    int min_x = static_cast<int>(std::floor(std::min(seg.p0.x, seg.p1.x) - max_r - 1));
    int max_x = static_cast<int>(std::ceil(std::max(seg.p0.x, seg.p1.x) + max_r + 1));
    int min_y = static_cast<int>(std::floor(std::min(seg.p0.y, seg.p1.y) - max_r - 1));
    int max_y = static_cast<int>(std::ceil(std::max(seg.p0.y, seg.p1.y) + max_r + 1));

    min_x = std::max(0, min_x);
    max_x = std::min(s.w - 1, max_x);
    min_y = std::max(0, min_y);
    max_y = std::min(s.h - 1, max_y);

    float dx = seg.p1.x - seg.p0.x;
    float dy = seg.p1.y - seg.p0.y;
    float seg_len_sq = dx * dx + dy * dy;
    if (seg_len_sq < 0.001f)
        seg_len_sq = 0.001f;
    float seg_len = std::sqrt(seg_len_sq);

    // Perpendicular normal (for shading)
    float perp_x = -dy / seg_len;
    float perp_y = dx / seg_len;

    for (int py = min_y; py <= max_y; ++py) {
        for (int px = min_x; px <= max_x; ++px) {
            float ppx = static_cast<float>(px) + 0.5f;
            float ppy = static_cast<float>(py) + 0.5f;

            float apx = ppx - seg.p0.x;
            float apy = ppy - seg.p0.y;
            float t = std::clamp((apx * dx + apy * dy) / seg_len_sq, 0.0f, 1.0f);

            float closest_x = seg.p0.x + dx * t;
            float closest_y = seg.p0.y + dy * t;
            float dist_sq = (ppx - closest_x) * (ppx - closest_x) +
                            (ppy - closest_y) * (ppy - closest_y);

            float radius = seg.r0 + (seg.r1 - seg.r0) * t;
            if (dist_sq <= radius * radius) {
                // Cross product for left/right shading
                float cross = (ppx - closest_x) * perp_x + (ppy - closest_y) * perp_y;
                float norm_dist = cross / std::max(radius, 0.1f);

                int idx;
                if (norm_dist < -0.3f)
                    idx = 2;  // highlight (left, lit side)
                else if (norm_dist > 0.3f)
                    idx = 0;  // shadow (right)
                else
                    idx = 1;  // base

                set_px(s, mat, px, py, pal.trunk[idx], Mat::Wood);
                mat.set_wood(px, py, seg.order);
            }
        }
    }
}

// ── Cluster stamp library ────────────────────────────────────────────────────
// Small pixel-art cluster shapes for foliage. Each is a bitmask of offsets
// from the center pixel. No single isolated pixels.

struct ClusterShape {
    int offsets[12][2];
    int count;
};

// 2x2 square
static constexpr ClusterShape CLUSTER_2X2 = {{{0, 0}, {1, 0}, {0, 1}, {1, 1}}, 4};

// 3x3 rounded blob (no corners)
static constexpr ClusterShape CLUSTER_3X3_ROUND = {
    {{0, -1}, {-1, 0}, {0, 0}, {1, 0}, {0, 1}}, 5};

// 3x3 diamond
static constexpr ClusterShape CLUSTER_DIAMOND = {
    {{0, -1}, {-1, 0}, {0, 0}, {1, 0}, {0, 1}, {-1, -1}, {1, 1}}, 7};

// 3x3 full (square)
static constexpr ClusterShape CLUSTER_3X3_FULL = {
    {{-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {0, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}}, 9};

// 3x2 wide (for needles)
static constexpr ClusterShape CLUSTER_WIDE = {
    {{-1, 0}, {0, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}}, 6};

// 4x2 wide strip (for needles)
static constexpr ClusterShape CLUSTER_STRIP = {
    {{-1, 0}, {0, 0}, {1, 0}, {2, 0}, {-1, 1}, {0, 1}, {1, 1}, {2, 1}}, 8};

static const ClusterShape* LEAF_CLUSTERS[] = {
    &CLUSTER_2X2, &CLUSTER_3X3_ROUND, &CLUSTER_DIAMOND, &CLUSTER_3X3_FULL};
static constexpr int NUM_LEAF_CLUSTERS = 4;

static const ClusterShape* NEEDLE_CLUSTERS[] = {&CLUSTER_WIDE, &CLUSTER_STRIP, &CLUSTER_2X2};
static constexpr int NUM_NEEDLE_CLUSTERS = 3;

void place_cluster(Sprite& s, MatBuf& mat, int bx, int by, const ClusterShape& shape,
                   uint32_t color, bool over_wood) {
    for (int i = 0; i < shape.count; ++i) {
        int px = bx + shape.offsets[i][0];
        int py = by + shape.offsets[i][1];
        if (px < 0 || px >= s.w || py < 0 || py >= s.h)
            continue;
        if (!over_wood && mat.get(px, py) == Mat::Wood)
            continue;
        set_px(s, mat, px, py, color, Mat::Leaf);
    }
}

// Cluster-only foliage stamping (items 1, 2, 3, 6)
// - All foliage from cluster shapes, no solid fills, no single pixel dots
// - Edge bias: inner clusters placed at ~50%, edge clusters at ~100%
// - Small noise offset per cluster for organic irregularity (item 3)
// - Clusters may extend slightly beyond envelope for natural silhouette

void stamp_leaf_blob(Sprite& s, MatBuf& mat, float cx, float cy, float rx, float ry,
                     const Palette& pal, uint32_t seed, bool over_wood = false) {
    int icx = static_cast<int>(cx);
    int icy = static_cast<int>(cy);
    int irx = std::max(1, static_cast<int>(rx));
    int iry = std::max(1, static_cast<int>(ry));

    // Scan area slightly larger than envelope so edge clusters can extend out
    int margin = 2;
    int min_x = std::max(0, static_cast<int>(std::floor(cx - rx)) - margin);
    int max_x = std::min(s.w - 1, static_cast<int>(std::ceil(cx + rx)) + margin);
    int min_y = std::max(0, static_cast<int>(std::floor(cy - ry)) - margin);
    int max_y = std::min(s.h - 1, static_cast<int>(std::ceil(cy + ry)) + margin);

    constexpr int STEP = 3;
    for (int gy = min_y; gy <= max_y; gy += STEP) {
        for (int gx = min_x; gx <= max_x; gx += STEP) {
            // Noise offset per cluster position (item 3: organic irregularity)
            uint32_t nh = pixel_hash(gx, gy, seed + 33);
            int nox = static_cast<int>(nh & 3) - 1;          // -1 to +2
            int noy = static_cast<int>((nh >> 2) & 3) - 1;

            float fdx = (static_cast<float>(gx + nox) - cx) / std::max(rx, 0.5f);
            float fdy = (static_cast<float>(gy + noy) - cy) / std::max(ry, 0.5f);
            float d2 = fdx * fdx + fdy * fdy;

            // Item 6: soft envelope — probabilistic guide, not hard clip
            // Item 3: density never below 0.4 inside canopy
            float density;
            if (d2 > 1.3f) {
                continue;
            } else if (d2 > 0.9f) {
                density = 1.0f - (d2 - 0.9f) * 2.0f;
                density = std::clamp(density, 0.2f, 1.0f);
            } else {
                density = 0.4f + 0.6f * std::clamp(d2, 0.0f, 1.0f);
            }
            float roll = static_cast<float>((nh >> 8) & 0xFF) / 255.0f;
            if (roll > density)
                continue;

            uint32_t h = pixel_hash(gx, gy, seed + 77);
            const auto* shape = LEAF_CLUSTERS[h % NUM_LEAF_CLUSTERS];

            float light = surface_light(gx + nox, gy + noy, icx, icy, irx, iry);
            int idx = shade_index(light);
            place_cluster(s, mat, gx + nox, gy + noy, *shape, pal.leaf[idx], over_wood);
        }
    }
}

void stamp_needle_layer(Sprite& s, MatBuf& mat, float cx, float cy, float half_w, float half_h,
                        const Palette& pal, uint32_t seed, bool over_wood = false) {
    int icx = static_cast<int>(cx);
    int icy = static_cast<int>(cy);
    int irx = std::max(1, static_cast<int>(half_w));
    int iry = std::max(1, static_cast<int>(half_h));

    int margin = 2;
    int min_x = std::max(0, static_cast<int>(std::floor(cx - half_w)) - margin);
    int max_x = std::min(s.w - 1, static_cast<int>(std::ceil(cx + half_w)) + margin);
    int min_y = std::max(0, static_cast<int>(std::floor(cy - half_h)) - margin);
    int max_y = std::min(s.h - 1, static_cast<int>(std::ceil(cy + half_h)) + margin);

    constexpr int STEP = 3;
    for (int gy = min_y; gy <= max_y; gy += STEP) {
        for (int gx = min_x; gx <= max_x; gx += STEP) {
            uint32_t nh = pixel_hash(gx, gy, seed + 33);
            int nox = static_cast<int>(nh & 3) - 1;
            int noy = static_cast<int>((nh >> 2) & 3) - 1;

            float fdx = (static_cast<float>(gx + nox) - cx) / std::max(half_w, 0.5f);
            float fdy = (static_cast<float>(gy + noy) - cy) / std::max(half_h, 0.5f);
            float d2 = fdx * fdx + fdy * fdy;

            // Item 6: soft envelope — probabilistic falloff instead of hard clip
            // Items 2/3: density never below 0.4 inside, smooth falloff at edge
            float density;
            if (d2 > 1.3f) {
                continue;  // hard cutoff well outside envelope
            } else if (d2 > 0.9f) {
                // Soft edge: probability drops from 1.0 to 0.2
                density = 1.0f - (d2 - 0.9f) * 2.0f;
                density = std::clamp(density, 0.2f, 1.0f);
            } else {
                density = 0.4f + 0.6f * std::clamp(d2, 0.0f, 1.0f);
            }
            float roll = static_cast<float>((nh >> 8) & 0xFF) / 255.0f;
            if (roll > density)
                continue;

            uint32_t h = pixel_hash(gx, gy, seed + 77);
            const auto* shape = NEEDLE_CLUSTERS[h % NUM_NEEDLE_CLUSTERS];

            float light = surface_light(gx + nox, gy + noy, icx, icy, irx, iry);
            int idx = shade_index(light);
            place_cluster(s, mat, gx + nox, gy + noy, *shape, pal.leaf[idx], over_wood);
        }
    }
}

// ── Frond drawing ────────────────────────────────────────────────────────────
// Draws a curved leaf arc with feather-like leaflets perpendicular to the rachis.
// Each frond is entirely leaf material (no wood) — the rachis is just 1px of darker leaf.

void stamp_frond(Sprite& s, MatBuf& mat, const FrondSpec& frond, const Palette& pal,
                 bool over_wood = false) {
    // Walk along the frond in small steps, applying curvature (gravity droop)
    Vec2 dir = {frond.tip.x - frond.base.x, frond.tip.y - frond.base.y};
    float total_len = vec2_len(dir);
    if (total_len < 1.0f)
        return;

    int n_steps = std::max(4, static_cast<int>(total_len * 1.5f));
    float step = 1.0f / static_cast<float>(n_steps);

    // Track the previous point for rachis continuity
    std::mt19937 frond_rng(frond.seed);
    std::uniform_real_distribution<float> jitter(-0.5f, 0.5f);

    for (int i = 0; i <= n_steps; ++i) {
        float t = static_cast<float>(i) * step;

        // Position along frond with quadratic curvature (gravity droop)
        float cx = frond.base.x + dir.x * t;
        float cy = frond.base.y + dir.y * t + frond.curve * t * t * total_len;

        int ix = static_cast<int>(cx);
        int iy = static_cast<int>(cy);

        // Rachis pixel: slightly darker leaf color
        float light = 0.3f + 0.4f * (1.0f - t);  // darker toward tip
        int shade = shade_index(light);
        shade = std::min(shade + 1, 3);  // push one shade darker for rachis
        if (over_wood || mat.get(ix, iy) != Mat::Wood)
            set_px(s, mat, ix, iy, pal.leaf[shade], Mat::Leaf);

        // Leaflet width: widest at 20-60% of frond length, taper at base and tip
        float width_t;
        if (t < 0.15f)
            width_t = t / 0.15f;         // ramp up from base
        else if (t > 0.7f)
            width_t = (1.0f - t) / 0.3f; // taper to tip
        else
            width_t = 1.0f;              // full width in middle
        float leaflet_w = frond.width * width_t;

        if (leaflet_w < 0.5f)
            continue;

        // Recalculate local direction (accounting for curvature)
        float next_t = std::min(t + step, 1.0f);
        float nx = frond.base.x + dir.x * next_t;
        float ny = frond.base.y + dir.y * next_t + frond.curve * next_t * next_t * total_len;
        Vec2 local_dir = {nx - cx, ny - cy};
        float ld = vec2_len(local_dir);
        if (ld < 0.01f)
            continue;
        local_dir.x /= ld;
        local_dir.y /= ld;

        // Perpendicular to local direction
        float perp_x = -local_dir.y;
        float perp_y = local_dir.x;

        // Draw leaflets on both sides
        int leaflet_len = std::max(1, static_cast<int>(leaflet_w + 0.5f));
        for (int side = -1; side <= 1; side += 2) {
            for (int li = 1; li <= leaflet_len; ++li) {
                float lf = static_cast<float>(li);
                // Leaflets angle slightly toward tip (not perfectly perpendicular)
                float px = cx + perp_x * lf * static_cast<float>(side)
                           + local_dir.x * lf * 0.15f;
                float py = cy + perp_y * lf * static_cast<float>(side)
                           + local_dir.y * lf * 0.15f;
                // Small jitter for organic feel
                px += jitter(frond_rng) * 0.4f;
                py += jitter(frond_rng) * 0.4f;

                int lpx = static_cast<int>(px);
                int lpy = static_cast<int>(py);

                if (lpx < 0 || lpx >= s.w || lpy < 0 || lpy >= s.h)
                    continue;

                // Shade: outer leaflets lighter (catching more light)
                float leaf_light = 0.3f + 0.5f * (static_cast<float>(li) /
                                   static_cast<float>(leaflet_len));
                // Upper fronds lighter, lower fronds darker
                leaf_light += (1.0f - t) * 0.15f;
                int li_shade = shade_index(leaf_light);

                if (over_wood || mat.get(lpx, lpy) != Mat::Wood)
                    set_px(s, mat, lpx, lpy, pal.leaf[li_shade], Mat::Leaf);
            }
        }
    }
}

// Rendering mode for rasterize_graph
enum class GraphRenderMode {
    Normal,       // branches preserve under foliage
    TrunkOnTop,   // foliage overwrites branches; trunk redrawn on top
};

void rasterize_graph(Sprite& s, MatBuf& mat, const PlantGraph& graph, const Palette& pal,
                     GraphRenderMode mode = GraphRenderMode::Normal) {
    bool foliage_over_wood = (mode == GraphRenderMode::TrunkOnTop);

    // Pass 1: Draw all wood segments
    for (const auto& seg : graph.segs) {
        rasterize_segment(s, mat, seg, pal);
    }

    // Pass 2: Stamp foliage (LeafBlob + NeedleLayer only — no accents yet)
    for (const auto& stamp : graph.stamps) {
        switch (stamp.kind) {
            case StampKind::LeafBlob:
                stamp_leaf_blob(s, mat, stamp.pos.x, stamp.pos.y, stamp.rx, stamp.ry, pal,
                                stamp.seed, foliage_over_wood);
                break;
            case StampKind::NeedleLayer:
                stamp_needle_layer(s, mat, stamp.pos.x, stamp.pos.y, stamp.rx, stamp.ry, pal,
                                   stamp.seed, foliage_over_wood);
                break;
            default:
                break;
        }
    }

    // Pass 3: Draw fronds (palm leaf arcs — entirely leaf material)
    for (const auto& frond : graph.fronds) {
        stamp_frond(s, mat, frond, pal, foliage_over_wood);
    }

    // Pass 4: Re-draw only the lower trunk (item 5: hide trunk in upper canopy)
    // Only redraw trunk segments whose midpoint is in the bottom 20% of the sprite
    if (mode == GraphRenderMode::TrunkOnTop) {
        float visible_threshold = static_cast<float>(s.h) * 0.80f;
        for (const auto& seg : graph.segs) {
            if (seg.order == 0) {
                float mid_y = (seg.p0.y + seg.p1.y) * 0.5f;
                if (mid_y >= visible_threshold)
                    rasterize_segment(s, mat, seg, pal);
            }
        }
    }

    // Pass 5: Accents (fruit, flowers) drawn LAST so they sit on top of foliage
    for (const auto& stamp : graph.stamps) {
        if (stamp.kind != StampKind::FlowerDot)
            continue;
        // rx encodes size hint — skip tiny accents (Small phase)
        if (!pal.has_accent || stamp.rx < 1.0f)
            continue;
        int ix = static_cast<int>(stamp.pos.x);
        int iy = static_cast<int>(stamp.pos.y);
        // Only draw accent where there's foliage underneath
        if (mat.get(ix, iy) == Mat::Leaf || mat.get(ix, iy) == Mat::Accent) {
            set_px(s, mat, ix, iy, pal.accent, Mat::Accent);
            if (stamp.rx >= 1.5f) {
                set_px(s, mat, ix + 1, iy, pal.accent_dark, Mat::Accent);
                set_px(s, mat, ix, iy + 1, pal.accent_dark, Mat::Accent);
            }
            if (stamp.rx >= 2.0f) {
                set_px(s, mat, ix - 1, iy, pal.accent_dark, Mat::Accent);
                set_px(s, mat, ix, iy - 1, pal.accent, Mat::Accent);
            }
        }
    }

    // Pass 6: Envelope clip — erase foliage pixels outside the profile
    // With waviness > 0, the triangle edge undulates to create a bushier look
    if (graph.envelope.active) {
        const auto& env = graph.envelope;
        float env_h = std::max(env.base_y - env.apex_y, 0.1f);
        for (int py = 0; py < s.h; ++py) {
            for (int px = 0; px < s.w; ++px) {
                Mat m = mat.get(px, py);
                if (m != Mat::Leaf && m != Mat::Accent)
                    continue;
                float fpx = static_cast<float>(px);
                float fpy = static_cast<float>(py);
                if (fpy < env.apex_y - env.margin || fpy > env.base_y + env.margin) {
                    set_pixel(s, px, py, rgba(0, 0, 0, 0));
                    mat.set(px, py, Mat::Empty);
                    continue;
                }
                float t = (fpy - env.apex_y) / env_h;
                t = std::clamp(t, 0.0f, 1.0f);
                float half_w = env.base_half_w * t + env.margin;

                // Waviness: low-frequency symmetric bulges along the height
                if (env.waviness > 0.0f) {
                    // Two overlapping sine waves at different frequencies for organic feel
                    float phase1 = static_cast<float>(env.seed & 0xFF) * 0.1f;
                    float phase2 = static_cast<float>((env.seed >> 8) & 0xFF) * 0.1f;
                    float wave1 = std::sin(t * 3.14159f * 2.5f + phase1);
                    float wave2 = std::sin(t * 3.14159f * 4.5f + phase2) * 0.5f;
                    float wave = (wave1 + wave2) * 0.5f;
                    // Modulate envelope width — bulges outward, symmetric on both sides
                    // Scale with t so top stays pointy, lower tiers can bulge more
                    float bulge = wave * env.waviness * env.base_half_w * 0.35f * t;
                    half_w += bulge;
                }

                float dist = std::abs(fpx - env.apex_x) - half_w;
                if (dist > 0.0f) {
                    set_pixel(s, px, py, rgba(0, 0, 0, 0));
                    mat.set(px, py, Mat::Empty);
                }
            }
        }
    }
}

// ── Ground shadow ────────────────────────────────────────────────────────────

void draw_ground_shadow(Sprite& s, MatBuf& mat, int cx, int cy, int rx, int ry) {
    for (int py = cy - ry; py <= cy + ry; ++py) {
        for (int px = cx - rx; px <= cx + rx; ++px) {
            float fdx = static_cast<float>(px - cx) / std::max(static_cast<float>(rx), 0.5f);
            float fdy = static_cast<float>(py - cy) / std::max(static_cast<float>(ry), 0.5f);
            if (fdx * fdx + fdy * fdy <= 1.0f) {
                // Only draw shadow on empty pixels
                if (px >= 0 && px < s.w && py >= 0 && py < s.h &&
                    mat.get(px, py) == Mat::Empty) {
                    uint8_t alpha = 20;
                    set_pixel(s, px, py, rgba(0, 0, 0, alpha));
                    mat.set(px, py, Mat::Shadow);
                }
            }
        }
    }
}

// ── Canopy envelope enforcement ──────────────────────────────────────────

bool in_envelope_ellipse(float px, float py, float cx, float cy, float rx, float ry) {
    float dx = (px - cx) / std::max(rx, 0.1f);
    float dy = (py - cy) / std::max(ry, 0.1f);
    return dx * dx + dy * dy <= 1.0f;
}

[[maybe_unused]] bool in_envelope_triangle(float px, float py, float apex_x, float apex_y, float base_y,
                          float base_half_w) {
    if (py < apex_y || py > base_y)
        return false;
    float t = (py - apex_y) / std::max(base_y - apex_y, 0.1f);
    float half_w = base_half_w * t;
    return std::abs(px - apex_x) <= half_w;
}

// ── Derived visual parameters ────────────────────────────────────────────────
// Computed once from PlantTraits so all generators use a consistent set of
// visual knobs driven by ecological properties — not just leafiness/branchiness.

struct VisualParams {
    float leaf_density;    // 0..1  how filled the canopy is
    float canopy_spread;   // 0..1  how wide relative to height
    float trunk_thickness; // 0.5..1.5  multiplier on base trunk radius
    float branch_droop;    // 0..1  gravitropism intensity
    float branch_spread;   // 0..1  how wide branches angle out
    float compactness;     // 0..1  1=tight crown, 0=loose open
    float color_warmth;    // -1..1  shifts palette: -1=blue-cool, +1=yellow-warm
    float foliage_top_bias;// 0..1  1=leaves only at top (palm), 0=evenly distributed
};

VisualParams derive_visual_params(const PlantTraits& t) {
    VisualParams v{};
    float leaf_f = static_cast<float>(t.leafiness) / 255.0f;
    float branch_f = static_cast<float>(t.branchiness) / 255.0f;

    // Water-loving plants have denser foliage; drought-tolerant are sparser
    float moisture_factor = std::clamp(t.water_need * 1.2f - t.drought_tol * 0.5f, 0.0f, 1.0f);
    v.leaf_density = std::clamp(leaf_f * 0.5f + moisture_factor * 0.5f, 0.1f, 1.0f);

    // Warm-adapted plants spread wider; cold-adapted stay compact
    float warmth = std::clamp((t.temp_opt_c - 10.0f) / 25.0f, 0.0f, 1.0f);
    v.canopy_spread = std::clamp(0.3f + warmth * 0.4f + branch_f * 0.3f, 0.2f, 1.0f);

    // Drought-tolerant = thicker trunks (water storage); wet-adapted = thinner
    v.trunk_thickness = std::clamp(0.7f + t.drought_tol * 0.6f, 0.5f, 1.5f);

    // Humid/warm = droopier branches; dry/cold = more upright
    v.branch_droop = std::clamp(moisture_factor * 0.6f + warmth * 0.3f, 0.0f, 0.8f);

    // Branch spread: high branchiness + warm = wide; low + cold = narrow
    v.branch_spread = std::clamp(branch_f * 0.6f + warmth * 0.3f, 0.1f, 1.0f);

    // Cold-adapted or drought-tolerant = compact crown
    float cold = std::clamp((10.0f - t.temp_opt_c) / 20.0f, 0.0f, 1.0f);
    v.compactness = std::clamp(cold * 0.4f + t.drought_tol * 0.3f + (1.0f - branch_f) * 0.3f,
                               0.0f, 1.0f);

    // Color warmth: drought shifts yellow-warm; cold shifts blue-cool
    v.color_warmth = std::clamp(t.drought_tol * 0.8f - cold * 0.6f - moisture_factor * 0.3f,
                                -1.0f, 1.0f);

    // Foliage top bias: archetype-driven, then modulated by ecology
    // Palm = 1.0 (all at top), TropicalTree = 0.7 (dense crown), Conifer handled separately
    // BroadleafTree = 0.3-0.5, Shrub = 0.1 (fairly even)
    switch (t.archetype) {
        case PlantArchetype::Palm:
            v.foliage_top_bias = 1.0f;
            break;
        case PlantArchetype::TropicalTree:
            v.foliage_top_bias = 0.65f + warmth * 0.15f;
            break;
        case PlantArchetype::BroadleafTree:
            v.foliage_top_bias = 0.25f + warmth * 0.2f;  // warm = more top-heavy
            break;
        case PlantArchetype::Conifer:
            v.foliage_top_bias = 0.4f;
            break;
        case PlantArchetype::Shrub:
            v.foliage_top_bias = 0.1f;
            break;
        default:
            v.foliage_top_bias = 0.0f;
            break;
    }

    return v;
}

// ── Graph generators ─────────────────────────────────────────────────────────

PlantGraph gen_broadleaf_graph(const PlantTraits& traits, GrowthPhase phase, int w, int h,
                               std::mt19937& rng, const VisualParams& vp) {
    PlantGraph g;
    float ff = foliage_fraction(phase);
    float cx = static_cast<float>(w) / 2.0f;

    std::uniform_real_distribution<float> unit(0.0f, 1.0f);

    // Trunk height: compact species have proportionally more trunk visible
    float trunk_frac = 0.35f + vp.compactness * 0.10f;
    float trunk_h = static_cast<float>(h) * trunk_frac * ff;
    trunk_h = std::max(trunk_h, 3.0f);
    // Trunk thickness driven by drought tolerance (water storage)
    float trunk_r0 = std::max(2.0f, 3.0f * ff * vp.trunk_thickness);
    float trunk_r1 = std::max(1.5f, 1.5f * ff * vp.trunk_thickness);
    Vec2 trunk_base = {cx, static_cast<float>(h) - 1.0f};

    int n_trunk_pts = (phase == GrowthPhase::Small) ? 2 : 4;
    float max_drift = 2.0f * ff;
    int first_trunk = static_cast<int>(g.segs.size());
    auto trunk = gen_curved_trunk(g, trunk_base, trunk_h, trunk_r0, trunk_r1,
                                  n_trunk_pts, max_drift, rng);
    int num_trunk_segs = n_trunk_pts - 1;
    Vec2 trunk_top = trunk.top;

    if (phase == GrowthPhase::Small) {
        g.stamps.push_back(
            {trunk_top, std::max(2.0f, 3.0f * ff), std::max(2.0f, 3.0f * ff), StampKind::LeafBlob,
             traits.sprite_seed});
        return g;
    }

    // Canopy envelope: shape driven by spread and compactness
    float env_cx = trunk_top.x;
    float canopy_h = static_cast<float>(h) * (0.45f + vp.canopy_spread * 0.15f) * ff;
    float env_cy = trunk_top.y + canopy_h * 0.15f;
    // Wide-spreading species have wider canopy; compact species are narrower/taller
    float spread_rx = 0.30f + vp.canopy_spread * 0.20f;
    float env_rx = static_cast<float>(w) * spread_rx;
    float env_ry = canopy_h * (0.40f + vp.compactness * 0.15f);

    // More branchy species have more branches
    int n_branches = 2 + static_cast<int>(vp.branch_spread * 3.0f);
    n_branches = std::clamp(n_branches, 2, 6);

    std::vector<Vec2> leaf_positions;

    // Item 1: Branch attachment heights with jitter + min vertical spacing
    std::vector<float> branch_ts;
    {
        float min_spacing = 0.08f;  // minimum t-spacing between branches
        for (int i = 0; i < n_branches; ++i) {
            float t = 0.35f + static_cast<float>(i) * (0.55f / static_cast<float>(n_branches));
            // Jitter: ±2px equivalent in t-space
            float jitter = (unit(rng) - 0.5f) * 0.15f;
            t += jitter;
            t = std::clamp(t, 0.25f, 0.92f);
            // Enforce min spacing from previous branch
            if (!branch_ts.empty()) {
                float prev = branch_ts.back();
                if (t - prev < min_spacing)
                    t = prev + min_spacing;
            }
            t = std::clamp(t, 0.25f, 0.92f);
            branch_ts.push_back(t);
        }
    }

    for (int i = 0; i < n_branches; ++i) {
        float t = branch_ts[static_cast<size_t>(i)];

        Vec2 branch_start = trunk_point_at(g, first_trunk, num_trunk_segs, t);

        // Alternate left/right; spread angle driven by branch_spread
        float side = (i % 2 == 0) ? -1.0f : 1.0f;
        float base_angle = 0.3f + vp.branch_spread * 0.7f;
        float angle = side * (base_angle + unit(rng) * 0.3f);
        Vec2 up_dir = {0.0f, -1.0f};
        Vec2 branch_dir = vec2_rotate(up_dir, angle);

        // Gravitropism: droop driven by visual params (humid/warm = droopier)
        float droop = 0.05f + vp.branch_droop * 0.25f;
        branch_dir.y += droop;
        float bl = vec2_len(branch_dir);
        if (bl > 0.01f) {
            branch_dir.x /= bl;
            branch_dir.y /= bl;
        }

        float branch_len = trunk_h * (0.25f + unit(rng) * 0.20f);

        Vec2 branch_end = {branch_start.x + branch_dir.x * branch_len,
                           branch_start.y + branch_dir.y * branch_len};

        branch_end.x = std::clamp(branch_end.x, 2.0f, static_cast<float>(w) - 3.0f);
        branch_end.y = std::clamp(branch_end.y, 2.0f, static_cast<float>(h) - 3.0f);

        // Item 4: branch thickness hierarchy (primary = trunk * 0.6)
        // Item 5: primary branches minimum 2px radius at base
        float br0 = std::max(2.0f, trunk_r0 * 0.6f);
        float br1 = std::max(1.0f, trunk_r0 * 0.25f);

        int seg_id = static_cast<int>(g.segs.size());
        g.segs.push_back({trunk.last_seg, branch_start, branch_end, br0, br1, 1});

        // Leaf cluster at branch tip — size driven by leaf density
        float cluster_r = 3.0f + vp.leaf_density * 6.0f;
        cluster_r *= ff;
        cluster_r = std::max(cluster_r, 2.0f);
        if (in_envelope_ellipse(branch_end.x, branch_end.y, env_cx, env_cy, env_rx, env_ry)) {
            g.stamps.push_back({branch_end, cluster_r, cluster_r * 0.85f, StampKind::LeafBlob,
                                traits.sprite_seed + static_cast<uint32_t>(i * 17)});
            leaf_positions.push_back(branch_end);
        }

        // Optional twig + leaf from branch midpoint
        if (vp.branch_spread > 0.3f && unit(rng) < 0.6f) {
            float tt = 0.4f + unit(rng) * 0.3f;
            Vec2 twig_start = vec2_lerp(branch_start, branch_end, tt);
            float twig_angle = side * (0.3f + unit(rng) * 0.4f);
            Vec2 twig_dir = vec2_rotate(branch_dir, twig_angle);
            // Phototropism: twigs bend upward
            twig_dir.y -= 0.2f;
            float twig_len = branch_len * 0.4f;
            Vec2 twig_end = {twig_start.x + twig_dir.x * twig_len,
                             twig_start.y + twig_dir.y * twig_len};
            twig_end.x = std::clamp(twig_end.x, 1.0f, static_cast<float>(w) - 2.0f);
            twig_end.y = std::clamp(twig_end.y, 1.0f, static_cast<float>(h) - 2.0f);

            // Item 4: secondary = trunk * 0.35; Item 5: twigs minimum 1px
            float twig_r0 = std::max(1.0f, trunk_r0 * 0.35f);
            float twig_r1 = std::max(1.0f, trunk_r0 * 0.18f);
            g.segs.push_back({seg_id, twig_start, twig_end, twig_r0, twig_r1, 2});

            float twig_r = cluster_r * 0.7f;
            if (in_envelope_ellipse(twig_end.x, twig_end.y, env_cx, env_cy,
                                    env_rx * 1.1f, env_ry * 1.1f)) {
                g.stamps.push_back(
                    {twig_end, twig_r, twig_r * 0.85f, StampKind::LeafBlob,
                     traits.sprite_seed + static_cast<uint32_t>(i * 31 + 100)});
                leaf_positions.push_back(twig_end);
            }
        }
    }

    // Crown cluster at trunk top
    float crown_r = 4.0f + vp.leaf_density * 6.0f;
    crown_r *= ff;
    crown_r = std::max(crown_r, 2.5f);
    g.stamps.push_back(
        {trunk_top, crown_r, crown_r * 0.8f, StampKind::LeafBlob, traits.sprite_seed + 777});
    leaf_positions.push_back(trunk_top);

    // Canopy envelope fill: density driven by leaf_density
    if (leaf_positions.size() >= 2) {
        int n_fill = 3 + static_cast<int>(vp.leaf_density * 7.0f);
        for (int i = 0; i < n_fill; ++i) {
            float angle = static_cast<float>(i) * 6.28f / static_cast<float>(n_fill);
            // Two radial rings: inner fill + outer fill
            float fr = (i % 2 == 0) ? (0.25f + unit(rng) * 0.3f) : (0.55f + unit(rng) * 0.35f);
            float fx = env_cx + std::cos(angle) * env_rx * fr;
            float fy = env_cy + std::sin(angle) * env_ry * fr;
            fx = std::clamp(fx, 2.0f, static_cast<float>(w) - 3.0f);
            fy = std::clamp(fy, 2.0f, static_cast<float>(h) - 3.0f);

            if (in_envelope_ellipse(fx, fy, env_cx, env_cy, env_rx, env_ry)) {
                float fill_r = 2.5f + vp.leaf_density * 5.0f;
                fill_r *= ff;
                g.stamps.push_back({Vec2{fx, fy}, fill_r, fill_r * 0.85f, StampKind::LeafBlob,
                                    traits.sprite_seed + static_cast<uint32_t>(i * 53 + 200)});
            }
        }
    }

    // Accent flowers/fruit scattered on canopy
    int n_accents = 4 + static_cast<int>(vp.leaf_density * 4.0f);
    for (int i = 0; i < n_accents; ++i) {
        if (leaf_positions.size() <= 1)
            break;
        auto& lp = leaf_positions[static_cast<size_t>(i) % leaf_positions.size()];
        float fx = lp.x + (unit(rng) - 0.5f) * 5.0f;
        float fy = lp.y + (unit(rng) - 0.5f) * 4.0f;
        g.stamps.push_back({Vec2{fx, fy}, 2.0f, 2.0f, StampKind::FlowerDot,
                            traits.sprite_seed + static_cast<uint32_t>(i + 500)});
    }

    return g;
}

PlantGraph gen_conifer_graph(const PlantTraits& traits, GrowthPhase phase, int w, int h,
                             std::mt19937& rng, const VisualParams& vp) {
    PlantGraph g;
    float ff = foliage_fraction(phase);
    float cx = static_cast<float>(w) / 2.0f;

    std::uniform_real_distribution<float> unit(0.0f, 1.0f);

    // Curved trunk: slight wobble, nearly full height
    float trunk_h = static_cast<float>(h) * 0.85f * ff;
    trunk_h = std::max(trunk_h, 4.0f);
    Vec2 trunk_base = {cx, static_cast<float>(h) - 1.0f};
    // Trunk thickness driven by adaptation
    float trunk_r0 = std::max(2.0f, 2.5f * ff * vp.trunk_thickness);
    float trunk_r1 = std::max(1.5f, 0.8f * ff * vp.trunk_thickness);

    int n_trunk_pts = (phase == GrowthPhase::Small) ? 2 : 5;
    float max_drift = 0.8f * ff;  // conifers are straighter than broadleaf
    int first_trunk = static_cast<int>(g.segs.size());
    auto trunk = gen_curved_trunk(g, trunk_base, trunk_h, trunk_r0, trunk_r1,
                                  n_trunk_pts, max_drift, rng);
    int num_trunk_segs = n_trunk_pts - 1;
    Vec2 trunk_top = trunk.top;

    if (phase == GrowthPhase::Small) {
        g.stamps.push_back(
            {trunk_top, 2.0f, 3.0f, StampKind::NeedleLayer, traits.sprite_seed});
        return g;
    }

    // Branch tiers: more tiers for dense species, fewer for sparse
    int n_tiers = 3 + static_cast<int>(vp.leaf_density * 3.0f + vp.branch_spread * 2.0f);
    n_tiers = std::clamp(n_tiers, 3, 8);

    // Canopy occupies upper portion of the trunk — leave bare trunk at bottom
    float canopy_start = 0.25f;
    float canopy_end = 0.85f;
    float tier_spacing = (canopy_end - canopy_start) / static_cast<float>(n_tiers);

    // Triangular envelope: spread affects width
    float base_half_w = static_cast<float>(w) * (0.30f + vp.canopy_spread * 0.20f);

    // Bushiness: sinusoidal tier-width modulation for organic profiles
    // Driven by canopy_spread + branch_spread — sparse/compact species stay triangular
    float bushiness = std::clamp(
        vp.leaf_density * 0.4f + vp.canopy_spread * 0.3f + vp.branch_spread * 0.3f, 0.0f, 1.0f);
    float bush_phase1 = static_cast<float>(traits.sprite_seed & 0xFF) * 0.1f;
    float bush_phase2 = static_cast<float>((traits.sprite_seed >> 8) & 0xFF) * 0.1f;

    for (int ti = 0; ti < n_tiers; ++ti) {
        float t = canopy_start + static_cast<float>(ti) * tier_spacing;
        // Item 1: jitter tier attachment heights to break shelf patterns
        t += (unit(rng) - 0.5f) * tier_spacing * 0.4f;
        t = std::clamp(t, canopy_start, canopy_end);
        Vec2 branch_origin = trunk_point_at(g, first_trunk, num_trunk_segs, t);

        float tier_frac = static_cast<float>(ti) / static_cast<float>(n_tiers - 1);

        // Triangular: wider at bottom, narrow at top
        float branch_len = base_half_w * ff * (1.0f - tier_frac * 0.70f);

        // Bushy modulation: sinusoidal bulges along the height
        if (bushiness > 0.1f) {
            float wave1 = std::sin(tier_frac * 3.14159f * 2.5f + bush_phase1);
            float wave2 = std::sin(tier_frac * 3.14159f * 4.5f + bush_phase2) * 0.5f;
            float wave = (wave1 + wave2) * 0.5f;
            branch_len += wave * bushiness * base_half_w * ff * 0.20f * (1.0f - tier_frac * 0.5f);
        }

        // Item 7: low-frequency noise to canopy width — subtle per-instance jitter
        float noise_scale = 0.06f + bushiness * 0.08f;
        float width_noise = (unit(rng) - 0.5f) * branch_len * noise_scale;
        branch_len += width_noise;
        branch_len = std::max(branch_len, 2.5f * ff);

        // Slight droop (gravitropism) - more droop on lower, longer branches
        float droop = (1.0f - tier_frac) * 1.5f + unit(rng) * 1.0f;

        // Item 4: asymmetry — left and right branches differ slightly
        float asym = (unit(rng) - 0.5f) * branch_len * 0.08f;

        Vec2 left_end = {branch_origin.x - (branch_len + asym), branch_origin.y + droop};
        left_end.x = std::max(1.0f, left_end.x);
        // Item 5: conifer branches minimum 1px radius
        float br_base = std::max(1.0f, 0.7f * ff);
        float br_tip = std::max(1.0f, br_base * 0.3f);
        g.segs.push_back({trunk.last_seg, branch_origin, left_end, br_base, br_tip, 1});

        Vec2 right_end = {branch_origin.x + (branch_len - asym), branch_origin.y + droop};
        right_end.x = std::min(static_cast<float>(w) - 2.0f, right_end.x);
        g.segs.push_back({trunk.last_seg, branch_origin, right_end, br_base, br_tip, 1});

        // Tier-aligned stamp: provides base coverage (hides branches)
        float layer_hw = branch_len + 2.0f;
        float tier_height_px = trunk_h * tier_spacing;
        float layer_hh = std::max(tier_height_px * 1.1f, 3.5f * ff);
        g.stamps.push_back({Vec2{branch_origin.x, branch_origin.y + droop * 0.3f}, layer_hw,
                            layer_hh, StampKind::NeedleLayer,
                            traits.sprite_seed + static_cast<uint32_t>(ti * 13)});

        // Infill stamp between tiers
        if (ti < n_tiers - 1) {
            float mid_t = t + tier_spacing * 0.5f;
            Vec2 mid_pt = trunk_point_at(g, first_trunk, num_trunk_segs, mid_t);
            float mid_hw = branch_len * 0.6f;
            float mid_hh = tier_height_px * 0.5f;
            g.stamps.push_back({mid_pt, mid_hw, std::max(mid_hh, 2.0f),
                                StampKind::NeedleLayer,
                                traits.sprite_seed + static_cast<uint32_t>(ti * 13 + 500)});
        }
    }

    // Item 2: Poisson scatter ON TOP of tier stamps to break repetition
    // These add organic variation without being responsible for base coverage
    {
        float apex_y = trunk_top.y;
        float base_y = trunk_base.y - trunk_h * (1.0f - canopy_start);
        float env_height = base_y - apex_y;
        if (env_height < 1.0f) env_height = 1.0f;

        int n_scatter = 6 + static_cast<int>(ff * 8.0f);
        float min_dist = 4.0f;
        std::vector<Vec2> placed;

        int attempts = 0;
        while (static_cast<int>(placed.size()) < n_scatter && attempts < n_scatter * 8) {
            ++attempts;
            float fy = apex_y + unit(rng) * env_height;
            float t_env = (fy - apex_y) / env_height;
            float half_w_at_y = base_half_w * ff * t_env;
            float fx = trunk_top.x + (unit(rng) - 0.5f) * 2.0f * half_w_at_y;

            bool too_close = false;
            for (const auto& p : placed) {
                float ddx = fx - p.x;
                float ddy = fy - p.y;
                if (ddx * ddx + ddy * ddy < min_dist * min_dist) {
                    too_close = true;
                    break;
                }
            }
            if (too_close)
                continue;
            placed.push_back({fx, fy});

            float size_scale = 0.4f + t_env * 0.6f;
            float stamp_hw = (3.0f + unit(rng) * 3.0f) * ff * size_scale;
            float stamp_hh = (2.0f + unit(rng) * 2.0f) * ff * size_scale;
            stamp_hw = std::max(stamp_hw, 2.0f);
            stamp_hh = std::max(stamp_hh, 1.5f);
            g.stamps.push_back({Vec2{fx, fy}, stamp_hw, stamp_hh,
                                StampKind::NeedleLayer,
                                traits.sprite_seed + static_cast<uint32_t>(attempts * 17 + 700)});
        }
    }

    // Upper canopy fill
    float upper_y = trunk_point_at(g, first_trunk, num_trunk_segs, 0.75f).y;
    float upper_hw = base_half_w * ff * 0.35f;
    float upper_hh = trunk_h * 0.20f;
    g.stamps.push_back(
        {Vec2{trunk_top.x, upper_y}, upper_hw, upper_hh,
         StampKind::NeedleLayer, traits.sprite_seed + 998});

    // Crown tip
    float tip_h = trunk_h * tier_spacing + 3.0f;
    g.stamps.push_back(
        {Vec2{trunk_top.x, trunk_top.y + 2.0f}, 3.5f * ff,
         std::max(tip_h * ff, 4.5f * ff), StampKind::NeedleLayer, traits.sprite_seed + 999});

    // Triangular envelope — clips ragged edges to a clean conifer profile
    // Bushier species (high leaf_density, canopy_spread) get undulating edges
    g.envelope.active = true;
    g.envelope.apex_x = trunk_top.x;
    g.envelope.apex_y = trunk_top.y - 1.0f;
    g.envelope.base_y = trunk_base.y - trunk_h * (1.0f - canopy_end);
    g.envelope.base_half_w = base_half_w * ff + 2.0f;
    g.envelope.margin = 2.0f;
    g.envelope.waviness = std::clamp(
        vp.leaf_density * 0.4f + vp.canopy_spread * 0.3f + vp.branch_spread * 0.3f, 0.0f, 1.0f);
    g.envelope.seed = traits.sprite_seed;

    return g;
}

PlantGraph gen_shrub_graph(const PlantTraits& traits, GrowthPhase phase, int w, int h,
                           std::mt19937& rng, const VisualParams& vp) {
    PlantGraph g;
    float ff = foliage_fraction(phase);
    float cx = static_cast<float>(w) / 2.0f;

    std::uniform_real_distribution<float> unit(0.0f, 1.0f);

    // Very short trunk — shrubs are mostly foliage down to the ground
    float trunk_h = static_cast<float>(h) * 0.12f * ff;
    trunk_h = std::max(trunk_h, 2.0f);
    Vec2 trunk_base = {cx, static_cast<float>(h) - 1.0f};
    float trunk_r0 = std::max(1.5f, 1.5f * ff * vp.trunk_thickness);
    float trunk_r1 = std::max(1.0f, 1.0f * ff * vp.trunk_thickness);

    int n_trunk_pts = (phase == GrowthPhase::Small) ? 2 : 3;
    float max_drift = 1.0f * ff;
    auto trunk = gen_curved_trunk(g, trunk_base, trunk_h, trunk_r0, trunk_r1,
                                  n_trunk_pts, max_drift, rng);
    Vec2 trunk_top = trunk.top;

    if (phase == GrowthPhase::Small) {
        g.stamps.push_back(
            {trunk_top, 2.5f, 2.0f, StampKind::LeafBlob, traits.sprite_seed});
        return g;
    }

    // Shrub envelope: width driven by canopy_spread
    float env_rx = static_cast<float>(w) * (0.30f + vp.canopy_spread * 0.20f);
    float env_ry = static_cast<float>(h) * (0.35f + vp.leaf_density * 0.15f) * ff;
    float env_cy = static_cast<float>(h) - env_ry * 0.85f;

    // Branch count driven by branch_spread
    int n_branches = 3 + static_cast<int>(vp.branch_spread * 3.0f);
    n_branches = std::clamp(n_branches, 3, 7);

    for (int i = 0; i < n_branches; ++i) {
        // Branches start from near the base, not just trunk top
        float start_t = 0.3f + unit(rng) * 0.6f;
        Vec2 start = vec2_lerp(trunk_base, trunk_top, start_t);

        // Wide spread: -80 to +80 degrees
        float angle_range = 2.8f;
        float angle = -angle_range / 2.0f +
                      static_cast<float>(i) * angle_range / static_cast<float>(n_branches - 1);
        angle += (unit(rng) - 0.5f) * 0.3f;

        Vec2 dir = vec2_rotate(Vec2{0.0f, -1.0f}, angle);
        // Outer branches droop more
        float side_factor = std::abs(angle) / (angle_range / 2.0f);
        dir.y += side_factor * 0.3f;

        float branch_len = static_cast<float>(h) * 0.30f * ff * (0.7f + unit(rng) * 0.3f);

        Vec2 end = {start.x + dir.x * branch_len, start.y + dir.y * branch_len};
        end.x = std::clamp(end.x, 2.0f, static_cast<float>(w) - 3.0f);
        end.y = std::clamp(end.y, 2.0f, static_cast<float>(h) - 3.0f);

        // Item 5: shrub branches minimum 1px
        float br = std::max(1.0f, trunk_r1 * 0.5f);
        g.segs.push_back({trunk.last_seg, start, end, br, std::max(1.0f, br * 0.3f), 1});

        float cr = 3.0f + vp.leaf_density * 5.0f;
        cr *= ff;
        cr = std::max(cr, 2.0f);
        g.stamps.push_back({end, cr, cr * 0.85f, StampKind::LeafBlob,
                            traits.sprite_seed + static_cast<uint32_t>(i * 23)});
    }

    // Canopy fill — density driven by leaf_density
    float canopy_rx = env_rx * 0.75f;
    float canopy_ry = env_ry * 0.65f;
    int n_fill = 3 + static_cast<int>(vp.leaf_density * 6.0f);
    for (int i = 0; i < n_fill; ++i) {
        float fa = static_cast<float>(i) * 6.28f / static_cast<float>(n_fill);
        float fr = 0.3f + unit(rng) * 0.5f;
        float fx = cx + std::cos(fa) * canopy_rx * fr;
        float fy = env_cy + std::sin(fa) * canopy_ry * fr;
        fx = std::clamp(fx, 2.0f, static_cast<float>(w) - 3.0f);
        fy = std::clamp(fy, 2.0f, static_cast<float>(h) - 3.0f);

        float fill_r = 2.5f + vp.leaf_density * 4.5f;
        fill_r *= ff;
        g.stamps.push_back({Vec2{fx, fy}, fill_r, fill_r * 0.8f, StampKind::LeafBlob,
                            traits.sprite_seed + static_cast<uint32_t>(i * 53 + 200)});
    }

    // Central dome
    g.stamps.push_back(
        {Vec2{cx, env_cy}, canopy_rx * 0.8f, canopy_ry * 0.7f, StampKind::LeafBlob,
         traits.sprite_seed + 888});

    // Flowers — shrubs are often flowering
    int n_flowers = 4 + static_cast<int>(vp.leaf_density * 4.0f);
    for (int i = 0; i < n_flowers; ++i) {
        float fa = static_cast<float>(i) * 6.28f / static_cast<float>(n_flowers);
        float fr = 0.2f + unit(rng) * 0.5f;
        float fx = cx + std::cos(fa) * canopy_rx * fr;
        float fy = env_cy + std::sin(fa) * canopy_ry * fr;
        g.stamps.push_back({Vec2{fx, fy}, 2.0f, 2.0f, StampKind::FlowerDot,
                            traits.sprite_seed + static_cast<uint32_t>(i + 600)});
    }

    return g;
}

PlantGraph gen_palm_graph(const PlantTraits& traits, GrowthPhase phase, int w, int h,
                          std::mt19937& rng, const VisualParams& vp) {
    PlantGraph g;
    float ff = foliage_fraction(phase);
    float cx = static_cast<float>(w) / 2.0f;

    std::uniform_real_distribution<float> unit(0.0f, 1.0f);

    // Tall, slender trunk — no branches, slight curve
    float trunk_h = static_cast<float>(h) * 0.75f * ff;
    trunk_h = std::max(trunk_h, 4.0f);
    Vec2 trunk_base = {cx, static_cast<float>(h) - 1.0f};
    // Palms have thin trunks — thickness barely varies with adaptation
    float trunk_r0 = std::max(1.5f, 2.0f * ff * (0.8f + vp.trunk_thickness * 0.2f));
    float trunk_r1 = std::max(1.0f, 1.2f * ff);

    int n_trunk_pts = (phase == GrowthPhase::Small) ? 2 : 5;
    float max_drift = 2.5f * ff;  // palms can lean more than other trees
    auto trunk = gen_curved_trunk(g, trunk_base, trunk_h, trunk_r0, trunk_r1,
                                  n_trunk_pts, max_drift, rng);
    Vec2 trunk_top = trunk.top;

    if (phase == GrowthPhase::Small) {
        // Just a couple of small fronds — short leaf arcs
        for (int i = 0; i < 3; ++i) {
            float angle = -1.2f + static_cast<float>(i) * 1.2f;
            Vec2 dir = vec2_rotate(Vec2{0.0f, -1.0f}, angle);
            float len = 3.0f + unit(rng) * 2.0f;
            Vec2 tip = {trunk_top.x + dir.x * len, trunk_top.y + dir.y * len};
            tip.x = std::clamp(tip.x, 1.0f, static_cast<float>(w) - 2.0f);
            tip.y = std::clamp(tip.y, 1.0f, static_cast<float>(h) - 2.0f);
            g.fronds.push_back({trunk_top, tip, 1.5f, 0.15f,
                                traits.sprite_seed + static_cast<uint32_t>(i * 13)});
        }
        return g;
    }

    // Fronds radiate from crown point — each is a curved leaf arc, NOT a wood segment
    int n_fronds = 6 + static_cast<int>(vp.leaf_density * 4.0f);
    n_fronds = std::clamp(n_fronds, 5, 12);

    float frond_len = static_cast<float>(h) * 0.35f * ff * (0.7f + vp.canopy_spread * 0.3f);
    frond_len = std::max(frond_len, 6.0f);

    // Leaflet width: wider for lusher species
    float base_leaflet_w = 2.0f + vp.leaf_density * 3.0f;
    base_leaflet_w *= ff;
    base_leaflet_w = std::max(base_leaflet_w, 1.5f);

    for (int i = 0; i < n_fronds; ++i) {
        // Spread fronds in a fan: angles from ~-2.5 to +2.5 radians
        float t = static_cast<float>(i) / static_cast<float>(n_fronds - 1);
        float angle = -2.2f + t * 4.4f;
        // Random variation
        angle += (unit(rng) - 0.5f) * 0.4f;

        Vec2 dir = vec2_rotate(Vec2{0.0f, -1.0f}, angle);

        // Individual frond length varies; central fronds are longer
        float center_d = std::abs(t - 0.5f) * 2.0f;
        float this_len = frond_len * (0.75f + unit(rng) * 0.25f);
        this_len *= (1.0f - center_d * 0.25f);

        Vec2 tip = {trunk_top.x + dir.x * this_len,
                    trunk_top.y + dir.y * this_len};
        tip.x = std::clamp(tip.x, 1.0f, static_cast<float>(w) - 2.0f);
        tip.y = std::clamp(tip.y, 1.0f, static_cast<float>(h) - 2.0f);

        // Curvature: outer fronds droop more; species droop from visual params
        float curvature = 0.15f + std::abs(angle) * 0.08f + vp.branch_droop * 0.12f;
        // Add some per-frond variation
        curvature += (unit(rng) - 0.3f) * 0.08f;
        curvature = std::max(curvature, 0.05f);

        // Leaflet width: slightly varies per frond
        float leaflet_w = base_leaflet_w * (0.8f + unit(rng) * 0.4f);

        g.fronds.push_back({trunk_top, tip, leaflet_w, curvature,
                            traits.sprite_seed + static_cast<uint32_t>(i * 31)});

        // Optional coconut/fruit clusters near crown base
        if (i > 0 && i < n_fronds - 1 && unit(rng) < 0.15f) {
            Vec2 fruit_pos = {trunk_top.x + dir.x * this_len * 0.08f,
                              trunk_top.y + dir.y * this_len * 0.08f + 1.5f};
            g.stamps.push_back({fruit_pos, 1.0f, 1.0f, StampKind::FlowerDot,
                                traits.sprite_seed + static_cast<uint32_t>(i + 800)});
        }
    }

    return g;
}

// ── Tropical / rainforest canopy tree ─────────────────────────────────────────
// Tall, straight trunk with buttress-like flared base. Wide, flat-topped canopy
// with dense foliage concentrated in the upper crown. Emergent crown shape.

PlantGraph gen_tropical_graph(const PlantTraits& traits, GrowthPhase phase, int w, int h,
                               std::mt19937& rng, const VisualParams& vp) {
    PlantGraph g;
    float ff = foliage_fraction(phase);
    float cx = static_cast<float>(w) / 2.0f;

    std::uniform_real_distribution<float> unit(0.0f, 1.0f);

    // Tall straight trunk — tropical trees invest in height to reach light
    float trunk_frac = 0.50f + vp.compactness * 0.05f;
    float trunk_h = static_cast<float>(h) * trunk_frac * ff;
    trunk_h = std::max(trunk_h, 5.0f);
    Vec2 trunk_base = {cx, static_cast<float>(h) - 1.0f};

    // Thick trunk with buttress flare at base
    float trunk_r0 = std::max(2.5f, 3.5f * ff * vp.trunk_thickness);
    float trunk_r1 = std::max(1.5f, 2.0f * ff * vp.trunk_thickness);

    int n_trunk_pts = (phase == GrowthPhase::Small) ? 2 : 4;
    float max_drift = 1.0f * ff;  // tropical trees grow straighter than temperate
    int first_trunk = static_cast<int>(g.segs.size());
    auto trunk = gen_curved_trunk(g, trunk_base, trunk_h, trunk_r0, trunk_r1,
                                  n_trunk_pts, max_drift, rng);
    int num_trunk_segs = n_trunk_pts - 1;
    Vec2 trunk_top = trunk.top;

    // Buttress roots: short flared segments at base
    if (phase != GrowthPhase::Small) {
        for (int i = 0; i < 2; ++i) {
            float side = (i == 0) ? -1.0f : 1.0f;
            float bx = trunk_base.x + side * (trunk_r0 * 1.5f + unit(rng) * 2.0f);
            float by = trunk_base.y - 1.0f - unit(rng) * 2.0f;
            Vec2 bstart = {trunk_base.x + side * trunk_r0 * 0.5f, trunk_base.y - trunk_h * 0.05f};
            Vec2 bend = {bx, by};
            g.segs.push_back({0, bstart, bend, trunk_r0 * 0.7f, trunk_r0 * 0.2f, 1});
        }
    }

    if (phase == GrowthPhase::Small) {
        g.stamps.push_back(
            {trunk_top, std::max(3.0f, 4.0f * ff), std::max(2.5f, 3.0f * ff),
             StampKind::LeafBlob, traits.sprite_seed});
        return g;
    }

    // Wide, flat-topped canopy — wider than tall
    // Crown is an oblate ellipse sitting on top of the trunk
    float canopy_h = static_cast<float>(h) * 0.40f * ff;
    float canopy_w = static_cast<float>(w) * (0.38f + vp.canopy_spread * 0.15f);
    // Crown center sits slightly above trunk top (emergent)
    float env_cx = trunk_top.x;
    float env_cy = trunk_top.y - canopy_h * 0.1f;
    float env_rx = canopy_w;
    float env_ry = canopy_h * 0.45f;

    // Horizontal spreading branches — tropical trees branch mainly at the top
    int n_branches = 3 + static_cast<int>(vp.branch_spread * 3.0f);
    n_branches = std::clamp(n_branches, 3, 7);

    std::vector<Vec2> leaf_positions;

    // Branches only in upper portion (foliage_top_bias drives this)
    float branch_min_t = 0.55f + vp.foliage_top_bias * 0.25f;

    for (int i = 0; i < n_branches; ++i) {
        float t = branch_min_t + static_cast<float>(i) * (0.40f / static_cast<float>(n_branches));
        t += (unit(rng) - 0.5f) * 0.08f;
        t = std::clamp(t, branch_min_t, 0.95f);

        Vec2 branch_start = trunk_point_at(g, first_trunk, num_trunk_segs, t);

        // Wide-spreading horizontal branches
        float side = (i % 2 == 0) ? -1.0f : 1.0f;
        // More horizontal than broadleaf — tropical trees spread for light
        float base_angle = 0.6f + vp.branch_spread * 0.5f;
        float angle = side * (base_angle + unit(rng) * 0.3f);
        Vec2 branch_dir = vec2_rotate(Vec2{0.0f, -1.0f}, angle);
        // Slight upward tendency (reaching for canopy gap light)
        branch_dir.y -= 0.1f;
        float bl = vec2_len(branch_dir);
        if (bl > 0.01f) {
            branch_dir.x /= bl;
            branch_dir.y /= bl;
        }

        float branch_len = trunk_h * (0.30f + unit(rng) * 0.15f);

        Vec2 branch_end = {branch_start.x + branch_dir.x * branch_len,
                           branch_start.y + branch_dir.y * branch_len};
        branch_end.x = std::clamp(branch_end.x, 2.0f, static_cast<float>(w) - 3.0f);
        branch_end.y = std::clamp(branch_end.y, 2.0f, static_cast<float>(h) - 3.0f);

        float br0 = std::max(2.0f, trunk_r0 * 0.55f);
        float br1 = std::max(1.0f, trunk_r0 * 0.2f);
        int seg_id = static_cast<int>(g.segs.size());
        g.segs.push_back({trunk.last_seg, branch_start, branch_end, br0, br1, 1});

        // Large leaf cluster at branch tip
        float cluster_r = 4.0f + vp.leaf_density * 7.0f;
        cluster_r *= ff;
        cluster_r = std::max(cluster_r, 3.0f);
        if (in_envelope_ellipse(branch_end.x, branch_end.y, env_cx, env_cy,
                                env_rx * 1.1f, env_ry * 1.1f)) {
            g.stamps.push_back({branch_end, cluster_r, cluster_r * 0.75f, StampKind::LeafBlob,
                                traits.sprite_seed + static_cast<uint32_t>(i * 19)});
            leaf_positions.push_back(branch_end);
        }

        // Sub-branches for denser canopy
        if (vp.branch_spread > 0.2f && unit(rng) < 0.7f) {
            float tt = 0.45f + unit(rng) * 0.25f;
            Vec2 sub_start = vec2_lerp(branch_start, branch_end, tt);
            float sub_angle = side * (0.2f + unit(rng) * 0.4f);
            Vec2 sub_dir = vec2_rotate(branch_dir, sub_angle);
            sub_dir.y -= 0.15f;  // reach upward
            float sub_len = branch_len * 0.5f;
            Vec2 sub_end = {sub_start.x + sub_dir.x * sub_len,
                            sub_start.y + sub_dir.y * sub_len};
            sub_end.x = std::clamp(sub_end.x, 1.0f, static_cast<float>(w) - 2.0f);
            sub_end.y = std::clamp(sub_end.y, 1.0f, static_cast<float>(h) - 2.0f);

            float sr0 = std::max(1.0f, trunk_r0 * 0.3f);
            float sr1 = std::max(1.0f, trunk_r0 * 0.15f);
            g.segs.push_back({seg_id, sub_start, sub_end, sr0, sr1, 2});

            float sub_r = cluster_r * 0.75f;
            if (in_envelope_ellipse(sub_end.x, sub_end.y, env_cx, env_cy,
                                    env_rx * 1.1f, env_ry * 1.1f)) {
                g.stamps.push_back({sub_end, sub_r, sub_r * 0.8f, StampKind::LeafBlob,
                                    traits.sprite_seed + static_cast<uint32_t>(i * 37 + 100)});
                leaf_positions.push_back(sub_end);
            }
        }
    }

    // Crown cluster at trunk top (large, dominant)
    float crown_r = 5.0f + vp.leaf_density * 7.0f;
    crown_r *= ff;
    crown_r = std::max(crown_r, 3.5f);
    g.stamps.push_back(
        {trunk_top, crown_r, crown_r * 0.7f, StampKind::LeafBlob, traits.sprite_seed + 777});
    leaf_positions.push_back(trunk_top);

    // Dense canopy fill — heavier toward the top (foliage_top_bias)
    int n_fill = 5 + static_cast<int>(vp.leaf_density * 10.0f);
    for (int i = 0; i < n_fill; ++i) {
        float angle = static_cast<float>(i) * 6.28f / static_cast<float>(n_fill);
        float fr = (i % 2 == 0) ? (0.2f + unit(rng) * 0.35f) : (0.5f + unit(rng) * 0.4f);
        float fx = env_cx + std::cos(angle) * env_rx * fr;
        float fy = env_cy + std::sin(angle) * env_ry * fr;

        // Top bias: shift fill points upward and reject those too low
        fy -= vp.foliage_top_bias * env_ry * 0.3f;
        float height_in_canopy = (fy - (env_cy - env_ry)) / (2.0f * env_ry);
        // Lower density in bottom portion
        if (height_in_canopy > 0.7f && unit(rng) > 0.3f)
            continue;

        fx = std::clamp(fx, 2.0f, static_cast<float>(w) - 3.0f);
        fy = std::clamp(fy, 2.0f, static_cast<float>(h) - 3.0f);

        if (in_envelope_ellipse(fx, fy, env_cx, env_cy, env_rx, env_ry)) {
            float fill_r = 3.0f + vp.leaf_density * 6.0f;
            fill_r *= ff;
            g.stamps.push_back({Vec2{fx, fy}, fill_r, fill_r * 0.8f, StampKind::LeafBlob,
                                traits.sprite_seed + static_cast<uint32_t>(i * 59 + 300)});
        }
    }

    // Accent flowers/fruit in canopy — tropical trees are lush with color
    int n_accents = 5 + static_cast<int>(vp.leaf_density * 5.0f);
    for (int i = 0; i < n_accents; ++i) {
        if (leaf_positions.size() <= 1)
            break;
        auto& lp = leaf_positions[static_cast<size_t>(i) % leaf_positions.size()];
        float fx = lp.x + (unit(rng) - 0.5f) * 6.0f;
        float fy = lp.y + (unit(rng) - 0.5f) * 5.0f;
        g.stamps.push_back({Vec2{fx, fy}, 2.0f, 2.0f, StampKind::FlowerDot,
                            traits.sprite_seed + static_cast<uint32_t>(i + 600)});
    }

    return g;
}

// ── Direct-paint generators (grass, succulent, moss) ─────────────────────────

void generate_grass(Sprite& s, MatBuf& mat, const PlantTraits& traits, const Palette& pal,
                    GrowthPhase phase, std::mt19937& rng) {
    int cx = s.w / 2;
    float ff = foliage_fraction(phase);
    int variant = static_cast<int>(traits.sprite_seed % 3);

    // Ground shadow
    draw_ground_shadow(s, mat, cx, s.h - 1, s.w / 3, 1);

    if (phase == GrowthPhase::Small) {
        set_px(s, mat, cx, s.h - 1, pal.leaf[0], Mat::Leaf);
        set_px(s, mat, cx, s.h - 2, pal.leaf[1], Mat::Leaf);
        set_px(s, mat, cx, s.h - 3, pal.leaf[3], Mat::Leaf);
        if (s.w > 10)
            set_px(s, mat, cx + 1, s.h - 2, pal.leaf[1], Mat::Leaf);
        return;
    }

    float leaf_norm = static_cast<float>(traits.leafiness) / 255.0f;
    int base_blades = (variant == 1) ? 6 : (variant == 2 ? 4 : 5);
    int max_blades = base_blades + static_cast<int>(leaf_norm * 4.0f);
    int num_blades = std::max(2, static_cast<int>(static_cast<float>(max_blades) * ff));

    std::uniform_int_distribution<int> lean_dist(-2, 2);

    for (int i = 0; i < num_blades; ++i) {
        int max_h = static_cast<int>(static_cast<float>(s.h - 2) * ff);
        if (variant == 1)
            max_h = max_h * 2 / 3;
        if (variant == 2)
            max_h = std::min(max_h + 2, s.h - 1);

        std::uniform_int_distribution<int> h_var(std::max(1, max_h / 2), std::max(2, max_h));
        int blade_h = h_var(rng);

        int spread = s.w / (num_blades + 1);
        spread = std::max(spread, 1);
        int base_x = cx + (i - num_blades / 2) * spread;
        std::uniform_int_distribution<int> jitter(-1, 1);
        base_x += jitter(rng);
        base_x = std::clamp(base_x, 1, s.w - 2);

        int lean = lean_dist(rng);
        int blade_base_w = (variant == 1) ? 4 : 3;

        for (int y = s.h - 1; y >= s.h - blade_h; --y) {
            float vert_t = static_cast<float>(s.h - 1 - y) / static_cast<float>(blade_h);
            int drift = static_cast<int>(static_cast<float>(lean) * vert_t * vert_t + 0.5f);

            int idx;
            if (vert_t < 0.15f)
                idx = 0;
            else if (vert_t < 0.30f)
                idx = 1;
            else if (vert_t < 0.70f)
                idx = 2;
            else if (vert_t < 0.85f)
                idx = 3;
            else
                idx = 4;

            int bw = std::max(1, static_cast<int>(static_cast<float>(blade_base_w) *
                                                      (1.0f - vert_t * 0.7f) +
                                                  0.5f));
            int x0 = base_x + drift - bw / 2;
            for (int dx = 0; dx < bw; ++dx) {
                int edge_idx = (dx == 0 || dx == bw - 1) ? std::max(idx - 1, 0) : idx;
                set_px(s, mat, x0 + dx, y, pal.leaf[edge_idx], Mat::Leaf);
            }
        }

        // Seed heads for variant 2
        if (variant == 2 && phase == GrowthPhase::Large) {
            float tip_drift = static_cast<float>(lean) * 1.0f;
            int tip_x = base_x + static_cast<int>(tip_drift + 0.5f);
            int tip_y = s.h - blade_h;
            uint32_t seed_col = rgba(180, 160, 80);
            uint32_t seed_dark = rgba(140, 120, 60);
            set_px(s, mat, tip_x, tip_y, seed_col, Mat::Accent);
            set_px(s, mat, tip_x - 1, tip_y, seed_dark, Mat::Accent);
            set_px(s, mat, tip_x + 1, tip_y, seed_dark, Mat::Accent);
            set_px(s, mat, tip_x, tip_y - 1, seed_col, Mat::Accent);
        }
    }
}

void generate_succulent(Sprite& s, MatBuf& mat, const PlantTraits& traits, const Palette& pal,
                        GrowthPhase phase, std::mt19937& rng) {
    float ff = foliage_fraction(phase);
    float fcx = static_cast<float>(s.w) / 2.0f;
    // Rosette center is in the lower third
    float fcy = static_cast<float>(s.h) * 0.75f;
    int icx = s.w / 2;

    draw_ground_shadow(s, mat, icx, s.h - 1, s.w / 4, 1);

    std::uniform_real_distribution<float> unit(0.0f, 1.0f);

    if (phase == GrowthPhase::Small) {
        set_px(s, mat, icx, s.h - 2, pal.leaf[2], Mat::Leaf);
        set_px(s, mat, icx, s.h - 3, pal.leaf[3], Mat::Leaf);
        set_px(s, mat, icx - 1, s.h - 2, pal.leaf[1], Mat::Leaf);
        set_px(s, mat, icx + 1, s.h - 2, pal.leaf[1], Mat::Leaf);
        return;
    }

    // Succulent variants: rosettes (0-2), columnar cactus (3), barrel cactus (4)
    int variant = static_cast<int>(traits.sprite_seed % 5);

    if (variant >= 3) {
        // ── Cylindrical cactus variants ─────────────────────────────────────
        bool is_barrel = (variant == 4);

        // Body dimensions
        float body_h = static_cast<float>(s.h) * (is_barrel ? 0.45f : 0.70f) * ff;
        body_h = std::max(body_h, 4.0f);
        float body_w = is_barrel ? (body_h * 0.7f) : (body_h * 0.3f);
        body_w = std::max(body_w, 2.0f);

        int base_y = s.h - 2;
        int top_y = base_y - static_cast<int>(body_h);
        top_y = std::max(top_y, 1);

        // Draw the cylindrical body with vertical ribs
        int n_ribs = is_barrel ? 5 : 3;
        for (int y = top_y; y <= base_y; ++y) {
            float yt = static_cast<float>(y - top_y) / std::max(static_cast<float>(base_y - top_y), 1.0f);
            // Width profile: barrel is widest at 40%, columnar tapers slightly at top
            float width_at_y;
            if (is_barrel) {
                // Barrel: oval shape, widest at 40%
                float shape_t = std::abs(yt - 0.4f) / 0.6f;
                width_at_y = body_w * (1.0f - shape_t * shape_t * 0.4f);
            } else {
                // Columnar: slight taper toward top, small bulge at base
                width_at_y = body_w * (0.85f + 0.15f * yt);
            }

            int hw = std::max(1, static_cast<int>(width_at_y + 0.5f));

            for (int dx = -hw; dx <= hw; ++dx) {
                int px = icx + dx;
                if (px < 0 || px >= s.w)
                    continue;

                float edge_frac = (hw > 0) ? std::abs(static_cast<float>(dx)) /
                                             static_cast<float>(hw) : 0.0f;

                // Rib shading: vertical stripes for cylindrical look
                float rib_phase = static_cast<float>(dx + hw) /
                                  std::max(static_cast<float>(hw * 2), 1.0f) *
                                  static_cast<float>(n_ribs) * 3.14159f;
                float rib_val = 0.5f + 0.5f * std::cos(rib_phase);

                // Combine edge shading with rib shading
                int idx;
                if (edge_frac > 0.85f)
                    idx = 0;  // dark edge outline
                else if (edge_frac > 0.6f)
                    idx = 1;  // shadow edge
                else if (rib_val > 0.7f)
                    idx = 3;  // rib highlight
                else if (rib_val < 0.3f)
                    idx = 1;  // rib valley
                else
                    idx = 2;  // mid tone

                // Top cap is rounder/lighter
                if (yt < 0.1f && edge_frac < 0.5f)
                    idx = std::min(idx + 1, 4);

                set_px(s, mat, px, y, pal.leaf[idx], Mat::Leaf);
            }
        }

        // Saguaro-style arms for columnar variant (some seeds)
        if (!is_barrel && phase == GrowthPhase::Large && (traits.sprite_seed / 5) % 3 != 0) {
            int n_arms = 1 + static_cast<int>((traits.sprite_seed / 15) % 2);
            for (int ai = 0; ai < n_arms; ++ai) {
                float side = (ai == 0) ? -1.0f : 1.0f;
                // Arm attaches at 30-60% up the body
                int attach_y = base_y - static_cast<int>(body_h * (0.3f + unit(rng) * 0.3f));
                int arm_w = std::max(1, static_cast<int>(body_w * 0.5f));
                int elbow_x = icx + static_cast<int>(side * (body_w + 1.0f + unit(rng) * 2.0f));
                int elbow_y = attach_y;
                // Horizontal segment
                int x_start = icx + static_cast<int>(side * body_w * 0.5f);
                for (int x = std::min(x_start, elbow_x); x <= std::max(x_start, elbow_x); ++x) {
                    if (x < 0 || x >= s.w)
                        continue;
                    for (int dy = -arm_w / 2; dy <= arm_w / 2; ++dy) {
                        int ay = elbow_y + dy;
                        if (ay >= 0 && ay < s.h)
                            set_px(s, mat, x, ay, pal.leaf[2], Mat::Leaf);
                    }
                }
                // Vertical segment going up from elbow
                int arm_h = static_cast<int>(body_h * (0.2f + unit(rng) * 0.2f));
                for (int y = elbow_y; y >= elbow_y - arm_h && y >= 0; --y) {
                    for (int dx = -arm_w / 2; dx <= arm_w / 2; ++dx) {
                        int ax = elbow_x + dx;
                        if (ax >= 0 && ax < s.w) {
                            float ef = (arm_w > 1) ? std::abs(static_cast<float>(dx)) /
                                       static_cast<float>(arm_w / 2) : 0.0f;
                            int idx = (ef > 0.7f) ? 1 : 2;
                            set_px(s, mat, ax, y, pal.leaf[idx], Mat::Leaf);
                        }
                    }
                }
                // Arm cap
                if (elbow_y - arm_h >= 0 && elbow_x >= 0 && elbow_x < s.w)
                    set_px(s, mat, elbow_x, elbow_y - arm_h, pal.leaf[3], Mat::Leaf);
            }
        }

        // Flower on top
        if (pal.has_accent && phase != GrowthPhase::Small) {
            int fy = std::max(0, top_y - 1);
            set_px(s, mat, icx, fy, pal.accent, Mat::Accent);
            set_px(s, mat, icx - 1, fy, pal.accent_dark, Mat::Accent);
            set_px(s, mat, icx + 1, fy, pal.accent_dark, Mat::Accent);
            if (fy > 0)
                set_px(s, mat, icx, fy - 1, pal.accent, Mat::Accent);
        }
    } else {
    // ── Rosette succulent variants (0-2) ────────────────────────────────────

    // Back leaves (drawn first, further out and drooping)
    // Then front leaves (drawn on top, more upright)
    struct LeafSpec {
        float angle;
        float len_frac;
        float width;
    };

    std::vector<LeafSpec> leaves;

    if (variant == 0) {
        // Aloe-like: ~7 leaves in a fan, central ones tall
        int n = 5 + static_cast<int>(ff * 2.0f);
        for (int i = 0; i < n; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(n - 1);
            float a = -1.0f + t * 2.0f;
            float center_d = std::abs(t - 0.5f) * 2.0f;
            float len = 1.0f - center_d * 0.4f;
            leaves.push_back({a, len, 1.5f + (1.0f - center_d) * 0.5f});
        }
    } else if (variant == 1) {
        // Agave-like: fewer, wider, more spread
        int n = 4 + static_cast<int>(ff * 2.0f);
        for (int i = 0; i < n; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(n - 1);
            float a = -1.3f + t * 2.6f;
            float center_d = std::abs(t - 0.5f) * 2.0f;
            float len = 1.0f - center_d * 0.3f;
            leaves.push_back({a, len, 2.0f + (1.0f - center_d) * 0.5f});
        }
    } else {
        // Round rosette: evenly spaced
        int n = 6 + static_cast<int>(ff * 2.0f);
        for (int i = 0; i < n; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(n - 1);
            float a = -1.1f + t * 2.2f;
            leaves.push_back({a, 0.85f + unit(rng) * 0.15f, 1.5f});
        }
    }

    float max_leaf_len = static_cast<float>(s.h) * 0.55f * ff;

    // Sort by angle magnitude descending so outer leaves are drawn first (behind)
    std::sort(leaves.begin(), leaves.end(),
              [](const LeafSpec& a, const LeafSpec& b) {
                  return std::abs(a.angle) > std::abs(b.angle);
              });

    for (const auto& leaf : leaves) {
        Vec2 dir = vec2_rotate(Vec2{0.0f, -1.0f}, leaf.angle);
        float leaf_len = max_leaf_len * leaf.len_frac;
        float base_w = leaf.width * ff;

        int steps = std::max(2, static_cast<int>(leaf_len));
        for (int step = 0; step < steps; ++step) {
            float lt = static_cast<float>(step) / static_cast<float>(steps - 1);
            float px = fcx + dir.x * static_cast<float>(step);
            float py = fcy + dir.y * static_cast<float>(step);

            float w = base_w * (1.0f - lt * 0.85f);
            int hw = static_cast<int>(w + 0.5f);

            float perp_x = -dir.y;
            float perp_y = dir.x;

            for (int d = -hw; d <= hw; ++d) {
                int ix = static_cast<int>(px + perp_x * static_cast<float>(d) + 0.5f);
                int iy = static_cast<int>(py + perp_y * static_cast<float>(d) + 0.5f);
                if (ix < 0 || ix >= s.w || iy < 0 || iy >= s.h)
                    continue;

                float edge_frac = (hw > 0) ? std::abs(static_cast<float>(d)) /
                                             static_cast<float>(hw) : 0.0f;
                int idx = 2;
                if (edge_frac > 0.6f)
                    idx = 1;
                if (lt > 0.75f)
                    idx = std::min(idx + 1, 4);
                if (lt < 0.15f && edge_frac < 0.3f)
                    idx = 1;

                set_px(s, mat, ix, iy, pal.leaf[idx], Mat::Leaf);
            }
        }

        // Bright tip pixel
        float tip_x = fcx + dir.x * leaf_len;
        float tip_y = fcy + dir.y * leaf_len;
        int itx = static_cast<int>(tip_x);
        int ity = static_cast<int>(tip_y);
        if (itx >= 0 && itx < s.w && ity >= 0 && ity < s.h) {
            set_px(s, mat, itx, ity, pal.leaf[4], Mat::Leaf);
        }
    }

    // Flower stalk from center
    if (pal.has_accent && phase == GrowthPhase::Large) {
        int stalk_h = static_cast<int>(max_leaf_len * 0.4f);
        int stalk_y = static_cast<int>(fcy) - static_cast<int>(max_leaf_len * 0.6f);
        for (int y = stalk_y; y > stalk_y - stalk_h && y >= 0; --y) {
            set_px(s, mat, icx, y, pal.leaf[1], Mat::Wood);
        }
        int fy = std::max(1, stalk_y - stalk_h);
        set_px(s, mat, icx, fy, pal.accent, Mat::Accent);
        set_px(s, mat, icx - 1, fy, pal.accent_dark, Mat::Accent);
        set_px(s, mat, icx + 1, fy, pal.accent_dark, Mat::Accent);
        set_px(s, mat, icx, fy - 1, pal.accent, Mat::Accent);
    }

    }  // end rosette variants
}

void generate_moss(Sprite& s, MatBuf& mat, const PlantTraits& traits, const Palette& pal,
                   GrowthPhase phase, std::mt19937& /*rng*/) {
    float ff = foliage_fraction(phase);
    int cx = s.w / 2;

    if (phase == GrowthPhase::Small) {
        set_px(s, mat, cx, s.h - 1, pal.leaf[1], Mat::Leaf);
        set_px(s, mat, cx - 1, s.h - 1, pal.leaf[0], Mat::Leaf);
        set_px(s, mat, cx + 1, s.h - 1, pal.leaf[2], Mat::Leaf);
        return;
    }

    // Moss is a flat, wide dome
    int dome_rx = std::max(2, static_cast<int>(static_cast<float>(s.w) * 0.4f * ff));
    int dome_ry = std::max(2, static_cast<int>(static_cast<float>(s.h) * 0.4f * ff));
    int dome_cy = s.h - 1;

    // Fill dome with cluster stamps — no per-pixel noise
    constexpr int STEP = 2;
    for (int gy = dome_cy - dome_ry; gy <= dome_cy; gy += STEP) {
        for (int gx = cx - dome_rx; gx <= cx + dome_rx; gx += STEP) {
            float fdx = static_cast<float>(gx - cx) / static_cast<float>(dome_rx);
            float fdy = static_cast<float>(gy - dome_cy) / static_cast<float>(dome_ry);
            float d2 = fdx * fdx + fdy * fdy;
            if (d2 > 1.0f)
                continue;

            float light = surface_light(gx, gy, cx, dome_cy, dome_rx, dome_ry);
            int idx = shade_index(light);

            uint32_t h = pixel_hash(gx, gy, traits.sprite_seed + 77);
            const auto* shape = LEAF_CLUSTERS[h % NUM_LEAF_CLUSTERS];
            place_cluster(s, mat, gx, gy, *shape, pal.leaf[idx], false);
        }
    }
}

// ── Protected wood mask ──────────────────────────────────────────────────
// Built from the segment graph skeleton so cleanup never deletes structural wood.

struct ProtectedMask {
    int w = 0, h = 0;
    std::vector<bool> data;
    void init(int w_, int h_) {
        w = w_;
        h = h_;
        data.assign(static_cast<size_t>(w * h), false);
    }
    void set(int x, int y) {
        if (x >= 0 && x < w && y >= 0 && y < h)
            data[static_cast<size_t>(y * w + x)] = true;
    }
    bool get(int x, int y) const {
        if (x >= 0 && x < w && y >= 0 && y < h)
            return data[static_cast<size_t>(y * w + x)];
        return false;
    }
};

// Build a mask of all pixels that belong to the wood skeleton
ProtectedMask build_wood_mask(const Sprite& s, const MatBuf& mat) {
    ProtectedMask mask;
    mask.init(s.w, s.h);
    for (int y = 0; y < s.h; ++y) {
        for (int x = 0; x < s.w; ++x) {
            if (mat.get(x, y) == Mat::Wood)
                mask.set(x, y);
        }
    }
    return mask;
}

// ── Cleanup passes ───────────────────────────────────────────────────────────

void remove_orphans(Sprite& s, MatBuf& mat, const ProtectedMask* protect = nullptr) {
    for (int y = 1; y < s.h - 1; ++y) {
        for (int x = 1; x < s.w - 1; ++x) {
            Mat m = mat.get(x, y);
            if (m == Mat::Empty || m == Mat::Shadow)
                continue;

            // Never delete protected wood pixels
            if (protect && protect->get(x, y))
                continue;

            int neighbors = 0;
            if (mat.get(x - 1, y) != Mat::Empty && mat.get(x - 1, y) != Mat::Shadow)
                neighbors++;
            if (mat.get(x + 1, y) != Mat::Empty && mat.get(x + 1, y) != Mat::Shadow)
                neighbors++;
            if (mat.get(x, y - 1) != Mat::Empty && mat.get(x, y - 1) != Mat::Shadow)
                neighbors++;
            if (mat.get(x, y + 1) != Mat::Empty && mat.get(x, y + 1) != Mat::Shadow)
                neighbors++;

            if (neighbors == 0) {
                set_pixel(s, x, y, 0);
                mat.set(x, y, Mat::Empty);
            }
        }
    }
}

void fill_holes(Sprite& s, MatBuf& mat) {
    for (int y = 1; y < s.h - 1; ++y) {
        for (int x = 1; x < s.w - 1; ++x) {
            if (mat.get(x, y) != Mat::Empty)
                continue;

            Mat left = mat.get(x - 1, y);
            Mat right = mat.get(x + 1, y);
            Mat up = mat.get(x, y - 1);
            Mat down = mat.get(x, y + 1);

            bool left_ok = (left == Mat::Leaf || left == Mat::Wood);
            bool right_ok = (right == Mat::Leaf || right == Mat::Wood);
            bool up_ok = (up == Mat::Leaf || up == Mat::Wood);
            bool down_ok = (down == Mat::Leaf || down == Mat::Wood);

            if (left_ok && right_ok && up_ok && down_ok) {
                // Fill with left neighbor's color and material
                set_px(s, mat, x, y, get_pixel(s, x - 1, y), left);
            }
        }
    }
}

// ── Cluster cleanup ──────────────────────────────────────────────────────

// Flood-fill to find connected component size and pixels
void flood_fill(const MatBuf& mat, int x, int y, std::vector<bool>& visited,
                std::vector<std::pair<int, int>>& component) {
    if (x < 0 || x >= mat.w || y < 0 || y >= mat.h)
        return;
    int idx = y * mat.w + x;
    if (visited[static_cast<size_t>(idx)])
        return;
    Mat m = mat.data[static_cast<size_t>(idx)];
    if (m == Mat::Empty || m == Mat::Shadow)
        return;

    visited[static_cast<size_t>(idx)] = true;
    component.emplace_back(x, y);

    flood_fill(mat, x - 1, y, visited, component);
    flood_fill(mat, x + 1, y, visited, component);
    flood_fill(mat, x, y - 1, visited, component);
    flood_fill(mat, x, y + 1, visited, component);
}

void remove_small_clusters(Sprite& s, MatBuf& mat, int min_size,
                           const ProtectedMask* protect = nullptr) {
    std::vector<bool> visited(static_cast<size_t>(mat.w * mat.h), false);

    for (int y = 0; y < mat.h; ++y) {
        for (int x = 0; x < mat.w; ++x) {
            Mat m = mat.get(x, y);
            if (m == Mat::Empty || m == Mat::Shadow)
                continue;
            if (visited[static_cast<size_t>(y * mat.w + x)])
                continue;

            std::vector<std::pair<int, int>> component;
            flood_fill(mat, x, y, visited, component);

            if (static_cast<int>(component.size()) < min_size) {
                // Check if any pixel in the component is protected
                bool has_protected = false;
                if (protect) {
                    for (auto [cx, cy] : component) {
                        if (protect->get(cx, cy)) {
                            has_protected = true;
                            break;
                        }
                    }
                }
                if (!has_protected) {
                    for (auto [cx, cy] : component) {
                        set_pixel(s, cx, cy, 0);
                        mat.set(cx, cy, Mat::Empty);
                    }
                }
            }
        }
    }
}

// ── Connectivity enforcement ──────────────────────────────────────────────
// Flood-fill from the trunk base pixel. Any non-empty pixel not reached is
// disconnected from the main structure and gets deleted. This guarantees
// the hard invariant: all branches are connected to the trunk.

void flood_fill_iterative(const MatBuf& mat, int start_x, int start_y,
                          std::vector<bool>& visited) {
    std::vector<std::pair<int, int>> stack;
    stack.reserve(256);
    auto try_push = [&](int x, int y) {
        if (x < 0 || x >= mat.w || y < 0 || y >= mat.h)
            return;
        int idx = y * mat.w + x;
        if (visited[static_cast<size_t>(idx)])
            return;
        Mat m = mat.data[static_cast<size_t>(idx)];
        if (m == Mat::Empty || m == Mat::Shadow)
            return;
        visited[static_cast<size_t>(idx)] = true;
        stack.emplace_back(x, y);
    };

    try_push(start_x, start_y);
    while (!stack.empty()) {
        auto [x, y] = stack.back();
        stack.pop_back();
        try_push(x - 1, y);
        try_push(x + 1, y);
        try_push(x, y - 1);
        try_push(x, y + 1);
    }
}

void enforce_connectivity(Sprite& s, MatBuf& mat) {
    // Find trunk base: lowest Wood pixel
    int base_x = -1, base_y = -1;
    for (int y = s.h - 1; y >= 0; --y) {
        for (int x = 0; x < s.w; ++x) {
            if (mat.get(x, y) == Mat::Wood) {
                base_x = x;
                base_y = y;
                goto found_base;
            }
        }
    }
found_base:
    if (base_x < 0)
        return;  // no wood at all

    std::vector<bool> reachable(static_cast<size_t>(s.w * s.h), false);
    flood_fill_iterative(mat, base_x, base_y, reachable);

    for (int y = 0; y < s.h; ++y) {
        for (int x = 0; x < s.w; ++x) {
            Mat m = mat.get(x, y);
            if (m == Mat::Empty || m == Mat::Shadow)
                continue;
            if (!reachable[static_cast<size_t>(y * s.w + x)]) {
                set_pixel(s, x, y, 0);
                mat.set(x, y, Mat::Empty);
            }
        }
    }
}

// ── Bridge gaps pass ─────────────────────────────────────────────────────
// After rasterization, scan for nearby disconnected Wood pixels and draw
// Bresenham lines to connect them. Fixes sub-pixel gaps in thin branches.

void bridge_wood_gaps(Sprite& s, MatBuf& mat, const Palette& pal) {
    // For each wood pixel, check if there's a gap of 1-2 empty pixels to
    // another wood pixel in 4 cardinal + 4 diagonal directions
    struct BridgePx {
        int x, y;
    };
    std::vector<BridgePx> bridges;

    static const int dx8[] = {1, 1, 0, -1, 1, -1, -1, 0};
    static const int dy8[] = {0, 1, 1, 1, -1, -1, 0, -1};

    for (int y = 0; y < s.h; ++y) {
        for (int x = 0; x < s.w; ++x) {
            if (mat.get(x, y) != Mat::Wood)
                continue;

            for (int d = 0; d < 8; ++d) {
                // Check gap of 1
                int mx = x + dx8[d];
                int my = y + dy8[d];
                if (mx < 0 || mx >= s.w || my < 0 || my >= s.h)
                    continue;
                if (mat.get(mx, my) != Mat::Empty)
                    continue;

                // Check if wood on the other side (gap=1)
                int fx = mx + dx8[d];
                int fy = my + dy8[d];
                if (fx >= 0 && fx < s.w && fy >= 0 && fy < s.h &&
                    mat.get(fx, fy) == Mat::Wood) {
                    bridges.push_back({mx, my});
                    continue;
                }

                // Check gap of 2
                if (fx < 0 || fx >= s.w || fy < 0 || fy >= s.h)
                    continue;
                if (mat.get(fx, fy) != Mat::Empty)
                    continue;
                int gx = fx + dx8[d];
                int gy = fy + dy8[d];
                if (gx >= 0 && gx < s.w && gy >= 0 && gy < s.h &&
                    mat.get(gx, gy) == Mat::Wood) {
                    bridges.push_back({mx, my});
                    bridges.push_back({fx, fy});
                }
            }
        }
    }

    for (auto [bx, by] : bridges) {
        if (mat.get(bx, by) == Mat::Empty) {
            set_px(s, mat, bx, by, pal.trunk[1], Mat::Wood);
        }
    }
}

// ── Outline pass ─────────────────────────────────────────────────────────

void add_outline(Sprite& s, MatBuf& mat) {
    // 1px dark outline on external silhouette edges
    // We scan all non-empty pixels and mark empty neighbors as outline candidates
    std::vector<std::pair<int, int>> outline_pixels;

    for (int y = 0; y < s.h; ++y) {
        for (int x = 0; x < s.w; ++x) {
            Mat m = mat.get(x, y);
            if (m == Mat::Empty || m == Mat::Shadow)
                continue;

            // Check 4-connected neighbors; if any is empty/shadow, those are outline sites
            static const int dx[] = {-1, 1, 0, 0};
            static const int dy[] = {0, 0, -1, 1};
            for (int d = 0; d < 4; ++d) {
                int nx = x + dx[d];
                int ny = y + dy[d];
                if (nx < 0 || nx >= s.w || ny < 0 || ny >= s.h)
                    continue;
                Mat nm = mat.get(nx, ny);
                if (nm == Mat::Empty || nm == Mat::Shadow) {
                    outline_pixels.emplace_back(nx, ny);
                }
            }
        }
    }

    // Determine outline color from the nearest filled pixel
    for (auto [ox, oy] : outline_pixels) {
        if (mat.get(ox, oy) != Mat::Empty && mat.get(ox, oy) != Mat::Shadow)
            continue;  // already filled by another outline pixel

        // Find the darkest adjacent filled pixel and darken it further
        uint32_t darkest = rgba(20, 15, 10);
        static const int dx[] = {-1, 1, 0, 0};
        static const int dy[] = {0, 0, -1, 1};
        for (int d = 0; d < 4; ++d) {
            int nx = ox + dx[d];
            int ny = oy + dy[d];
            Mat nm = mat.get(nx, ny);
            if (nm != Mat::Empty && nm != Mat::Shadow) {
                uint32_t px = get_pixel(s, nx, ny);
                auto r = static_cast<float>(px & 0xFF);
                auto g = static_cast<float>((px >> 8) & 0xFF);
                auto b = static_cast<float>((px >> 16) & 0xFF);
                // Darken significantly for outline
                darkest = rgba(static_cast<uint8_t>(std::clamp(r * 0.30f, 0.0f, 60.0f)),
                               static_cast<uint8_t>(std::clamp(g * 0.30f, 0.0f, 60.0f)),
                               static_cast<uint8_t>(std::clamp(b * 0.30f, 0.0f, 60.0f)));
                break;
            }
        }

        set_pixel(s, ox, oy, darkest);
        // Don't set mat — outline is cosmetic, not structural
    }
}

// ── Health effects ───────────────────────────────────────────────────────────

void add_dead_branches(Sprite& s, MatBuf& mat, uint32_t seed, int trunk_top_y,
                       PlantArchetype archetype = PlantArchetype::BroadleafTree) {
    uint32_t branch_col = rgba(100, 80, 60);
    uint32_t branch_dark = rgba(75, 62, 48);
    uint32_t branch_light = rgba(120, 95, 70);

    // Find trunk x at given y by scanning for wood. Returns -1 if no wood found.
    auto find_trunk_x = [&](int y) -> int {
        for (int x = 0; x < s.w; ++x) {
            if (mat.get(x, y) == Mat::Wood)
                return x;
        }
        return -1;  // no wood at this height
    };

    int zone_top = std::max(trunk_top_y, 1);
    int zone_bot = std::min(s.h - 3, zone_top + s.h / 2);
    int zone_h = std::max(zone_bot - zone_top, 1);

    // Conifers: more branches, evenly spaced tiers, both sides per tier
    // Broadleaf: moderate branches for canopy skeleton
    // Shrub: fewer, shorter
    bool is_conifer = (archetype == PlantArchetype::Conifer);
    bool is_broadleaf = (archetype == PlantArchetype::BroadleafTree);
    int num_branches = is_conifer    ? (5 + static_cast<int>(pixel_hash(7, 7, seed) % 3))
                       : is_broadleaf ? (4 + static_cast<int>(pixel_hash(7, 7, seed) % 3))
                                      : (3 + static_cast<int>(pixel_hash(7, 7, seed) % 3));

    for (int bi = 0; bi < num_branches; ++bi) {
        int branch_y;
        if (is_conifer) {
            // Evenly spaced tiers
            branch_y = zone_top + (zone_h * (bi + 1)) / (num_branches + 1);
        } else {
            branch_y = zone_top + static_cast<int>(pixel_hash(bi, 60, seed) %
                                                    static_cast<uint32_t>(zone_h));
        }
        branch_y = std::clamp(branch_y, 1, s.h - 2);

        int trunk_x = find_trunk_x(branch_y);
        // INVARIANT: only draw branches from actual wood attachment points.
        // If no wood exists at this height, skip — don't create floating branches.
        if (trunk_x < 0)
            continue;
        float height_frac = static_cast<float>(zone_bot - branch_y) / static_cast<float>(zone_h);

        // For conifers: draw both left and right; for others: alternate
        int start_dir = is_conifer ? -1 : ((bi % 2 == 0) ? -1 : 1);
        int end_dir = is_conifer ? 1 : start_dir;

        for (int dir = start_dir; dir <= end_dir; dir += 2) {
            // Branch lengths scale with canvas and archetype
            int max_reach = is_conifer ? (s.w / 2 - 2)
                            : is_broadleaf ? (s.w / 3)
                                           : 10;
            int base_len = is_conifer ? (max_reach / 2 + static_cast<int>(
                                            pixel_hash(bi, 61 + dir, seed) %
                                            static_cast<uint32_t>(max_reach / 3 + 1)))
                           : is_broadleaf ? (max_reach / 2 + static_cast<int>(
                                                pixel_hash(bi, 61 + dir, seed) %
                                                static_cast<uint32_t>(max_reach / 2 + 1)))
                                          : (5 + static_cast<int>(pixel_hash(bi, 61, seed) % 7));
            // Conifers: wider at bottom (triangular), narrow at top
            int branch_len = is_conifer ? static_cast<int>(static_cast<float>(base_len) *
                                                           (0.3f + 0.7f * (1.0f - height_frac)))
                                        : base_len;

            // Broadleaf: branches angle upward into former canopy area
            // Conifer: nearly horizontal with slight droop
            float rise = is_conifer    ? 0.0f
                         : is_broadleaf ? (-0.4f - height_frac * 0.3f)
                                        : (-0.3f * height_frac);
            float droop = is_conifer ? 0.15f : 0.0f;

            float bx = static_cast<float>(trunk_x);
            float by = static_cast<float>(branch_y);
            for (int j = 0; j < branch_len; ++j) {
                bx += static_cast<float>(dir);
                by += rise + droop + (j > branch_len / 2 ? 0.3f : 0.0f);

                int ix = static_cast<int>(bx);
                int iy = static_cast<int>(by);
                if (ix >= 0 && ix < s.w && iy >= 0 && iy < s.h) {
                    if (mat.get(ix, iy) == Mat::Empty) {
                        uint32_t col = (j == 0)                  ? branch_light
                                       : (j == branch_len - 1) ? branch_dark
                                                                  : branch_col;
                        set_px(s, mat, ix, iy, col, Mat::Wood);
                    }
                }
            }
        }
    }
}

void apply_health_tint(Sprite& s, MatBuf& mat, PlantHealth health, uint32_t seed,
                       PlantArchetype archetype) {
    if (health == PlantHealth::Healthy)
        return;

    // Leaf removal probability by health state
    float remove_chance = 0.0f;
    switch (health) {
        case PlantHealth::Stressed:
            if (archetype == PlantArchetype::Conifer)
                remove_chance = 0.15f;
            else if (archetype == PlantArchetype::Succulent)
                remove_chance = 0.10f;
            else if (archetype == PlantArchetype::Moss)
                remove_chance = 0.25f;
            else if (archetype == PlantArchetype::Grass)
                remove_chance = 0.08f;
            else if (archetype == PlantArchetype::Palm)
                remove_chance = 0.20f;  // fronds yellow at tips first
            else
                remove_chance = 0.35f;
            break;
        case PlantHealth::Brown:
            if (archetype == PlantArchetype::Conifer)
                remove_chance = 0.50f;
            else if (archetype == PlantArchetype::Succulent)
                remove_chance = 0.30f;
            else if (archetype == PlantArchetype::Moss)
                remove_chance = 0.55f;
            else if (archetype == PlantArchetype::Grass)
                remove_chance = 0.15f;
            else if (archetype == PlantArchetype::Palm)
                remove_chance = 0.55f;  // lower fronds drop
            else
                remove_chance = 0.70f;
            break;
        case PlantHealth::Dead:
            if (archetype == PlantArchetype::Conifer)
                remove_chance = 0.92f;
            else if (archetype == PlantArchetype::Succulent)
                remove_chance = 0.0f;
            else if (archetype == PlantArchetype::Moss)
                remove_chance = 0.50f;
            else if (archetype == PlantArchetype::Grass)
                remove_chance = 0.0f;
            else if (archetype == PlantArchetype::Palm)
                remove_chance = 0.85f;  // bare trunk with few drooping frond remnants
            else
                remove_chance = 1.0f;
            break;
        default:
            break;
    }

    int trunk_top_y = s.h;

    for (int y = 0; y < s.h; ++y) {
        for (int x = 0; x < s.w; ++x) {
            Mat m = mat.get(x, y);
            if (m == Mat::Empty || m == Mat::Shadow)
                continue;

            uint32_t px = get_pixel(s, x, y);
            auto r = static_cast<float>(px & 0xFF);
            auto g = static_cast<float>((px >> 8) & 0xFF);
            auto b = static_cast<float>((px >> 16) & 0xFF);

            if (m == Mat::Leaf) {
                // Probabilistic leaf removal
                float hash_val =
                    static_cast<float>(pixel_hash(x, y, seed + 0xDEAD) & 0xFFFF) / 65535.0f;
                if (hash_val < remove_chance) {
                    set_pixel(s, x, y, 0);
                    mat.set(x, y, Mat::Empty);
                    continue;
                }

                // Tint surviving leaves
                float lum = 0.3f * r + 0.59f * g + 0.11f * b;
                switch (health) {
                    case PlantHealth::Stressed: {
                        if (archetype == PlantArchetype::Conifer) {
                            r = std::clamp(lum * 0.55f + 35.0f, 0.0f, 255.0f);
                            g = std::clamp(lum * 0.50f + 15.0f, 0.0f, 255.0f);
                            b = std::clamp(lum * 0.20f, 0.0f, 255.0f);
                            r = r * 0.5f + static_cast<float>(px & 0xFF) * 0.5f;
                            g = g * 0.5f + static_cast<float>((px >> 8) & 0xFF) * 0.5f;
                            b = b * 0.5f + static_cast<float>((px >> 16) & 0xFF) * 0.5f;
                        } else if (archetype == PlantArchetype::Succulent) {
                            r = std::clamp(r * 0.9f + 15.0f, 0.0f, 255.0f);
                            g = std::clamp(g * 0.75f + 10.0f, 0.0f, 255.0f);
                            b = std::clamp(b * 0.6f, 0.0f, 255.0f);
                        } else if (archetype == PlantArchetype::Moss) {
                            r = std::clamp(lum * 0.6f + 25.0f, 0.0f, 255.0f);
                            g = std::clamp(lum * 0.55f + 15.0f, 0.0f, 255.0f);
                            b = std::clamp(lum * 0.15f, 0.0f, 255.0f);
                        } else if (archetype == PlantArchetype::Palm) {
                            // Fronds yellow from tips — warm yellow-green shift
                            r = std::clamp(lum * 0.70f + 40.0f, 0.0f, 255.0f);
                            g = std::clamp(lum * 0.65f + 25.0f, 0.0f, 255.0f);
                            b = std::clamp(lum * 0.10f, 0.0f, 255.0f);
                            r = r * 0.6f + static_cast<float>(px & 0xFF) * 0.4f;
                            g = g * 0.6f + static_cast<float>((px >> 8) & 0xFF) * 0.4f;
                            b = b * 0.6f + static_cast<float>((px >> 16) & 0xFF) * 0.4f;
                        } else {
                            // Broadleaf/Shrub/Grass: autumn yellow-orange
                            float ar = std::clamp(lum * 0.85f + 45.0f, 0.0f, 255.0f);
                            float ag = std::clamp(lum * 0.65f + 20.0f, 0.0f, 255.0f);
                            float ab = std::clamp(lum * 0.15f, 0.0f, 255.0f);
                            r = r * 0.4f + ar * 0.6f;
                            g = g * 0.4f + ag * 0.6f;
                            b = b * 0.4f + ab * 0.6f;
                        }
                        break;
                    }
                    case PlantHealth::Brown: {
                        if (archetype == PlantArchetype::Succulent) {
                            r = std::clamp(lum * 0.50f + 35.0f, 0.0f, 255.0f);
                            g = std::clamp(lum * 0.40f + 22.0f, 0.0f, 255.0f);
                            b = std::clamp(lum * 0.25f + 10.0f, 0.0f, 255.0f);
                        } else {
                            r = std::clamp(lum * 0.55f + 30.0f, 0.0f, 255.0f);
                            g = std::clamp(lum * 0.30f + 12.0f, 0.0f, 255.0f);
                            b = std::clamp(lum * 0.10f + 3.0f, 0.0f, 255.0f);
                        }
                        break;
                    }
                    case PlantHealth::Dead: {
                        if (archetype == PlantArchetype::Succulent) {
                            r = std::clamp(lum * 0.45f + 32.0f, 0.0f, 255.0f);
                            g = std::clamp(lum * 0.38f + 22.0f, 0.0f, 255.0f);
                            b = std::clamp(lum * 0.28f + 15.0f, 0.0f, 255.0f);
                        } else if (archetype == PlantArchetype::Moss) {
                            r = std::clamp(lum * 0.50f + 28.0f, 0.0f, 255.0f);
                            g = std::clamp(lum * 0.32f + 15.0f, 0.0f, 255.0f);
                            b = std::clamp(lum * 0.12f + 5.0f, 0.0f, 255.0f);
                        } else if (archetype == PlantArchetype::Grass) {
                            float vert = static_cast<float>(y) / static_cast<float>(s.h);
                            if (vert < 0.40f) {
                                set_pixel(s, x, y, 0);
                                mat.set(x, y, Mat::Empty);
                                continue;
                            }
                            r = std::clamp(lum * 0.60f + 40.0f, 0.0f, 255.0f);
                            g = std::clamp(lum * 0.40f + 25.0f, 0.0f, 255.0f);
                            b = std::clamp(lum * 0.15f + 5.0f, 0.0f, 255.0f);
                        } else {
                            // Broadleaf/Shrub/Conifer: grey-brown dead foliage
                            r = std::clamp(lum * 0.50f + 35.0f, 0.0f, 255.0f);
                            g = std::clamp(lum * 0.35f + 18.0f, 0.0f, 255.0f);
                            b = std::clamp(lum * 0.15f + 8.0f, 0.0f, 255.0f);
                        }
                        break;
                    }
                    default:
                        break;
                }

                set_pixel(s, x, y,
                          rgba(static_cast<uint8_t>(std::clamp(r, 0.0f, 255.0f)),
                               static_cast<uint8_t>(std::clamp(g, 0.0f, 255.0f)),
                               static_cast<uint8_t>(std::clamp(b, 0.0f, 255.0f))));

            } else if (m == Mat::Wood) {
                if (y < trunk_top_y)
                    trunk_top_y = y;

                // Item 5: Dead trees lose smaller branches first
                // Remove order>1 (twigs) when dead, partially remove order>1 when brown
                uint8_t order = mat.get_order(x, y);
                if (health == PlantHealth::Dead && order >= 2) {
                    set_pixel(s, x, y, 0);
                    mat.set(x, y, Mat::Empty);
                    continue;
                }
                if (health == PlantHealth::Brown && order >= 2) {
                    float hash_val = static_cast<float>(pixel_hash(x, y, seed + 0xB0B0) & 0xFFFF) /
                                     65535.0f;
                    if (hash_val < 0.5f) {
                        set_pixel(s, x, y, 0);
                        mat.set(x, y, Mat::Empty);
                        continue;
                    }
                }

                if (health == PlantHealth::Dead || health == PlantHealth::Brown) {
                    float lum = 0.3f * r + 0.59f * g + 0.11f * b;
                    float grey = (health == PlantHealth::Dead) ? 0.5f : 0.25f;
                    r = r * (1.0f - grey) + (lum * 0.65f + 35.0f) * grey;
                    g = g * (1.0f - grey) + (lum * 0.55f + 28.0f) * grey;
                    b = b * (1.0f - grey) + (lum * 0.42f + 20.0f) * grey;
                    set_pixel(s, x, y,
                              rgba(static_cast<uint8_t>(std::clamp(r, 0.0f, 255.0f)),
                                   static_cast<uint8_t>(std::clamp(g, 0.0f, 255.0f)),
                                   static_cast<uint8_t>(std::clamp(b, 0.0f, 255.0f))));
                }

            } else if (m == Mat::Accent) {
                if (health == PlantHealth::Dead || health == PlantHealth::Brown) {
                    set_pixel(s, x, y, 0);
                    mat.set(x, y, Mat::Empty);
                } else if (health == PlantHealth::Stressed) {
                    float lum = 0.3f * r + 0.59f * g + 0.11f * b;
                    r = r * 0.6f + lum * 0.4f;
                    g = g * 0.6f + lum * 0.4f;
                    b = b * 0.6f + lum * 0.4f;
                    set_pixel(s, x, y,
                              rgba(static_cast<uint8_t>(std::clamp(r, 0.0f, 255.0f)),
                                   static_cast<uint8_t>(std::clamp(g, 0.0f, 255.0f)),
                                   static_cast<uint8_t>(std::clamp(b, 0.0f, 255.0f))));
                }
            }
        }
    }

    // Shadow removal + dead branches for Brown/Dead
    if (health == PlantHealth::Brown || health == PlantHealth::Dead) {
        for (int y = 0; y < s.h; ++y) {
            for (int x = 0; x < s.w; ++x) {
                if (mat.get(x, y) == Mat::Shadow) {
                    set_pixel(s, x, y, 0);
                    mat.set(x, y, Mat::Empty);
                }
            }
        }
        if (health == PlantHealth::Dead) {
            if (archetype == PlantArchetype::BroadleafTree ||
                archetype == PlantArchetype::Shrub ||
                archetype == PlantArchetype::Conifer ||
                archetype == PlantArchetype::TropicalTree) {
                add_dead_branches(s, mat, seed, trunk_top_y, archetype);
            }
        }
    }
}

}  // namespace

// ── Public API ───────────────────────────────────────────────────────────────

Sprite generate_plant_sprite(const PlantTraits& traits, GrowthPhase phase, PlantHealth health) {
    Sprite s;
    get_canvas_size(traits.archetype, phase, s.w, s.h);
    s.pixels.assign(static_cast<size_t>(s.w * s.h), 0);

    MatBuf mat;
    mat.init(s.w, s.h);

    std::mt19937 rng_local(traits.sprite_seed + static_cast<uint32_t>(phase));
    VisualParams vp = derive_visual_params(traits);
    Palette pal = make_palette(traits, rng_local, vp.color_warmth);

    // Keep graph around so we can re-render wood after health effects
    PlantGraph graph;
    bool uses_trunk_on_top = false;

    switch (traits.archetype) {
        case PlantArchetype::Grass:
            generate_grass(s, mat, traits, pal, phase, rng_local);
            break;
        case PlantArchetype::Succulent:
            generate_succulent(s, mat, traits, pal, phase, rng_local);
            break;
        case PlantArchetype::Moss:
            generate_moss(s, mat, traits, pal, phase, rng_local);
            break;
        case PlantArchetype::BroadleafTree:
            graph = gen_broadleaf_graph(traits, phase, s.w, s.h, rng_local, vp);
            draw_ground_shadow(s, mat, s.w / 2, s.h - 1, s.w / 3, 2);
            rasterize_graph(s, mat, graph, pal, GraphRenderMode::TrunkOnTop);
            uses_trunk_on_top = true;
            break;
        case PlantArchetype::Conifer:
            graph = gen_conifer_graph(traits, phase, s.w, s.h, rng_local, vp);
            draw_ground_shadow(s, mat, s.w / 2, s.h - 1, s.w / 4, 2);
            rasterize_graph(s, mat, graph, pal, GraphRenderMode::TrunkOnTop);
            uses_trunk_on_top = true;
            break;
        case PlantArchetype::Shrub:
            graph = gen_shrub_graph(traits, phase, s.w, s.h, rng_local, vp);
            draw_ground_shadow(s, mat, s.w / 2, s.h - 1, s.w * 2 / 5, 2);
            rasterize_graph(s, mat, graph, pal);
            break;
        case PlantArchetype::Palm:
            graph = gen_palm_graph(traits, phase, s.w, s.h, rng_local, vp);
            draw_ground_shadow(s, mat, s.w / 2, s.h - 1, s.w / 4, 2);
            rasterize_graph(s, mat, graph, pal, GraphRenderMode::TrunkOnTop);
            uses_trunk_on_top = true;
            break;
        case PlantArchetype::TropicalTree:
            graph = gen_tropical_graph(traits, phase, s.w, s.h, rng_local, vp);
            draw_ground_shadow(s, mat, s.w / 2, s.h - 1, s.w / 3, 2);
            rasterize_graph(s, mat, graph, pal, GraphRenderMode::TrunkOnTop);
            uses_trunk_on_top = true;
            break;
    }

    // Build protected wood mask before cleanup (Item 3: never delete skeleton wood)
    bool has_graph = (traits.archetype == PlantArchetype::BroadleafTree ||
                      traits.archetype == PlantArchetype::Conifer ||
                      traits.archetype == PlantArchetype::Shrub ||
                      traits.archetype == PlantArchetype::Palm ||
                      traits.archetype == PlantArchetype::TropicalTree);
    ProtectedMask wood_mask;
    if (has_graph) {
        wood_mask = build_wood_mask(s, mat);
        // Item 6: Bridge wood gaps before connectivity check
        bridge_wood_gaps(s, mat, pal);
        // Rebuild mask after bridging
        wood_mask = build_wood_mask(s, mat);
    }

    // Cleanup passes (Item 3: respect protected mask)
    const ProtectedMask* protect_ptr = has_graph ? &wood_mask : nullptr;
    remove_orphans(s, mat, protect_ptr);
    fill_holes(s, mat);

    // Item 2: Enforce connectivity — delete anything not reachable from trunk base
    if (has_graph) {
        enforce_connectivity(s, mat);
    }

    // Health effects (before outline so outline traces the final silhouette)
    apply_health_tint(s, mat, health, traits.sprite_seed, traits.archetype);

    // TrunkOnTop mode hid branch wood under foliage. After health strips
    // foliage, re-render the wood skeleton so branches reappear.
    if (uses_trunk_on_top && health != PlantHealth::Healthy) {
        for (const auto& seg : graph.segs) {
            rasterize_segment(s, mat, seg, pal);
        }
    }

    // INVARIANT: enforce connectivity AGAIN after health effects.
    // Health stripping (leaf removal, order≥2 wood pruning) and add_dead_branches
    // can both create disconnected fragments. Re-enforce from trunk base.
    if (has_graph) {
        enforce_connectivity(s, mat);
    }

    // Second cleanup after health (leaf removal can create new orphans/tiny clusters)
    remove_orphans(s, mat);
    if (health == PlantHealth::Dead) {
        remove_small_clusters(s, mat, 6);
    } else if (health == PlantHealth::Stressed) {
        remove_small_clusters(s, mat, 3);
    }

    // Outline pass on final shape
    add_outline(s, mat);

    return s;
}

}  // namespace veggen
