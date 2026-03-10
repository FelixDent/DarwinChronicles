#include "telemetry.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>

#include "renderer.h"

namespace sandbox {

// ── Overlay names ───────────────────────────────────────────────────────────

const char* overlay_name(OverlayMode mode) {
    switch (mode) {
        case OverlayMode::None:
            return "None";
        case OverlayMode::Continental:
            return "Continental";
        case OverlayMode::Slope:
            return "Slope";
        case OverlayMode::ElevBand:
            return "ElevBand";
        case OverlayMode::DistOcean:
            return "DistOcean";
        case OverlayMode::Roughness:
            return "Roughness";
        case OverlayMode::Aspect:
            return "Aspect";
        case OverlayMode::Geology:
            return "Geology";
        case OverlayMode::SoilTexture:
            return "SoilTexture";
        default:
            return "Unknown";
    }
}

// ── Color mapping ───────────────────────────────────────────────────────────

SDL_Color continental_color(float h) {
    auto v = static_cast<uint8_t>(std::clamp(h, 0.0f, 1.0f) * 255.0f);
    return {v, v, v, 180};
}

SDL_Color slope_color(float s) {
    // Black (flat) → yellow (steep)
    float t = std::clamp(s, 0.0f, 1.0f);
    auto r = static_cast<uint8_t>(t * 255.0f);
    auto g = static_cast<uint8_t>(t * 230.0f);
    auto b = static_cast<uint8_t>(20.0f * (1.0f - t));
    return {r, g, b, 180};
}

SDL_Color elevband_color(ElevBand band) {
    switch (band) {
        case ElevBand::Water:
            return {30, 60, 180, 200};
        case ElevBand::Lowland:
            return {120, 180, 80, 200};
        case ElevBand::Hills:
            return {200, 170, 80, 200};
        case ElevBand::Mountains:
            return {180, 180, 190, 200};
        default:
            return {0, 0, 0, 180};
    }
}

SDL_Color dist_color(float dist, float max_dist) {
    // Green (close) → red (far)
    float t = (max_dist > 0.0f) ? std::clamp(dist / max_dist, 0.0f, 1.0f) : 0.0f;
    auto r = static_cast<uint8_t>(t * 220.0f + 20.0f);
    auto g = static_cast<uint8_t>((1.0f - t) * 220.0f + 20.0f);
    auto b = static_cast<uint8_t>(30.0f);
    return {r, g, b, 180};
}

SDL_Color erodibility_color(float erodibility) {
    // Green (low erodibility = resistant) -> red (high erodibility = easily eroded)
    float t = std::clamp(erodibility, 0.0f, 1.0f);
    auto r = static_cast<uint8_t>(40.0f * (1.0f - t) + 200.0f * t);
    auto g = static_cast<uint8_t>(180.0f * (1.0f - t) + 80.0f * t);
    auto b = static_cast<uint8_t>(60.0f * (1.0f - t) + 60.0f * t);
    return {r, g, b, 200};
}

SDL_Color soil_depth_color(float depth) {
    // Tan (shallow) -> dark brown (deep)
    float t = std::clamp(depth / 3.0f, 0.0f, 1.0f);  // soil_depth is [0..3] meters
    auto r = static_cast<uint8_t>(220.0f * (1.0f - t) + 80.0f * t);
    auto g = static_cast<uint8_t>(200.0f * (1.0f - t) + 60.0f * t);
    auto b = static_cast<uint8_t>(140.0f * (1.0f - t) + 30.0f * t);
    return {r, g, b, 200};
}

SDL_Color aspect_color(float aspect) {
    // Map angle [-pi, pi] to hue wheel
    float t = (aspect + 3.14159265f) / (2.0f * 3.14159265f);  // [0, 1]
    t = std::clamp(t, 0.0f, 1.0f);
    // HSV-like wheel: R→Y→G→C→B→M→R
    float h = t * 6.0f;
    if (h >= 6.0f)
        h -= 6.0f;  // wrap boundary (aspect = pi)
    int hi = static_cast<int>(h) % 6;
    float f = h - static_cast<float>(hi);
    uint8_t r = 0, g = 0, b = 0;
    switch (hi) {
        case 0:
            r = 255;
            g = static_cast<uint8_t>(f * 255.0f);
            break;
        case 1:
            r = static_cast<uint8_t>((1.0f - f) * 255.0f);
            g = 255;
            break;
        case 2:
            g = 255;
            b = static_cast<uint8_t>(f * 255.0f);
            break;
        case 3:
            g = static_cast<uint8_t>((1.0f - f) * 255.0f);
            b = 255;
            break;
        case 4:
            r = static_cast<uint8_t>(f * 255.0f);
            b = 255;
            break;
        case 5:
            r = 255;
            b = static_cast<uint8_t>((1.0f - f) * 255.0f);
            break;
    }
    return {r, g, b, 180};
}

// ── Statistics ──────────────────────────────────────────────────────────────

static void field_stats(const std::vector<float>& values, float& out_min, float& out_max,
                        float& out_mean, float& out_stddev) {
    if (values.empty())
        return;

    out_min = *std::min_element(values.begin(), values.end());
    out_max = *std::max_element(values.begin(), values.end());

    double sum = 0.0;
    for (float v : values)
        sum += static_cast<double>(v);
    out_mean = static_cast<float>(sum / static_cast<double>(values.size()));

    double sq_sum = 0.0;
    for (float v : values) {
        double d = static_cast<double>(v) - static_cast<double>(out_mean);
        sq_sum += d * d;
    }
    out_stddev = static_cast<float>(std::sqrt(sq_sum / static_cast<double>(values.size())));
}

static void field_stats_simple(const std::vector<float>& values, float& out_min, float& out_max,
                               float& out_mean) {
    if (values.empty())
        return;

    out_min = *std::min_element(values.begin(), values.end());
    out_max = *std::max_element(values.begin(), values.end());

    double sum = 0.0;
    for (float v : values)
        sum += static_cast<double>(v);
    out_mean = static_cast<float>(sum / static_cast<double>(values.size()));
}

TerrainStats compute_stats(const Terrain& terrain) {
    TerrainStats stats;
    stats.total_tiles = static_cast<uint32_t>(terrain.tiles.size());

    std::vector<float> heights, slopes, roughnesses;
    heights.reserve(terrain.tiles.size());
    slopes.reserve(terrain.tiles.size());

    double erodibility_sum = 0.0;
    double soil_depth_sum = 0.0;
    uint32_t land_count = 0;

    for (const auto& tile : terrain.tiles) {
        stats.band_counts[static_cast<size_t>(tile.band)]++;

        if (tile.is_ocean)
            ++stats.ocean_tiles;
        else
            ++stats.land_tiles;

        heights.push_back(tile.elev01);
        slopes.push_back(tile.slope01);

        if (tile.dist_ocean > stats.dist_ocean_max)
            stats.dist_ocean_max = tile.dist_ocean;

        if (!tile.is_ocean) {
            roughnesses.push_back(tile.roughness);
            erodibility_sum += static_cast<double>(tile.erodibility);
            soil_depth_sum += static_cast<double>(tile.soil_depth);
            ++land_count;
        }
    }

    if (land_count > 0) {
        stats.erodibility_mean =
            static_cast<float>(erodibility_sum / static_cast<double>(land_count));
        stats.soil_depth_mean =
            static_cast<float>(soil_depth_sum / static_cast<double>(land_count));
    }

    field_stats(heights, stats.height_min, stats.height_max, stats.height_mean,
                stats.height_stddev);
    field_stats(slopes, stats.slope_min, stats.slope_max, stats.slope_mean, stats.slope_stddev);
    field_stats_simple(roughnesses, stats.roughness_min, stats.roughness_max, stats.roughness_mean);

    return stats;
}

void print_stats(const TerrainStats& stats) {
    std::cout << "\n=== Terrain Statistics ===\n";
    std::cout << "Total tiles: " << stats.total_tiles << "\n";
    std::cout << "  Ocean: " << stats.ocean_tiles << " (" << std::fixed << std::setprecision(1)
              << 100.0f * static_cast<float>(stats.ocean_tiles) /
                     static_cast<float>(stats.total_tiles)
              << "%)\n";
    std::cout << "  Land:  " << stats.land_tiles << " ("
              << 100.0f * static_cast<float>(stats.land_tiles) /
                     static_cast<float>(stats.total_tiles)
              << "%)\n\n";

    // Band distribution
    const char* band_names[] = {"Water", "Lowland", "Hills", "Mountains"};
    std::cout << "Elevation Bands:\n";
    for (int i = 0; i < 4; ++i) {
        float pct = 100.0f * static_cast<float>(stats.band_counts[static_cast<size_t>(i)]) /
                    static_cast<float>(stats.total_tiles);
        std::cout << "  " << std::left << std::setw(12) << band_names[i] << std::right
                  << std::setw(6) << stats.band_counts[static_cast<size_t>(i)] << " (" << std::fixed
                  << std::setprecision(1) << std::setw(5) << pct << "%)\n";
    }

    auto print_field = [](const char* name, float mn, float mx, float mean) {
        std::cout << "  " << std::left << std::setw(16) << name << "min=" << std::fixed
                  << std::setprecision(2) << std::setw(8) << mn << " max=" << std::setw(8) << mx
                  << " mean=" << std::setw(8) << mean << "\n";
    };

    std::cout << "\nField Statistics:\n";
    print_field("Height", stats.height_min, stats.height_max, stats.height_mean);
    print_field("Slope", stats.slope_min, stats.slope_max, stats.slope_mean);
    print_field("Roughness", stats.roughness_min, stats.roughness_max, stats.roughness_mean);
    std::cout << "  Max dist_ocean: " << stats.dist_ocean_max << "\n";
    std::cout << "  Mean erodibility: " << stats.erodibility_mean << "\n";
    std::cout << "  Mean soil depth: " << stats.soil_depth_mean << "\n\n";
}

// ── FPS counter ─────────────────────────────────────────────────────────────

void FPSCounter::tick() {
    uint32_t now = SDL_GetTicks();
    frame_times_[static_cast<size_t>(index_)] = now;
    index_ = (index_ + 1) % WINDOW;
    if (count_ < WINDOW)
        ++count_;

    if (count_ > 1) {
        int oldest = (index_ - count_ + WINDOW) % WINDOW;
        uint32_t elapsed = now - frame_times_[static_cast<size_t>(oldest)];
        if (elapsed > 0) {
            fps_ = static_cast<float>(count_ - 1) * 1000.0f / static_cast<float>(elapsed);
        }
    }
}

// ── Overlay rendering ───────────────────────────────────────────────────────

void render_overlay(SDL_Renderer* renderer, const Terrain& terrain, const TerrainStats& stats,
                    const Camera& cam, int win_w, int win_h, OverlayMode mode, int tile_size) {
    if (mode == OverlayMode::None)
        return;

    // Determine visible tile range
    int min_tx, min_ty, max_tx, max_ty;
    cam.screen_to_tile(0, 0, win_w, win_h, tile_size, min_tx, min_ty);
    cam.screen_to_tile(win_w, win_h, win_w, win_h, tile_size, max_tx, max_ty);

    min_tx = std::max(min_tx - 1, 0);
    min_ty = std::max(min_ty - 1, 0);
    max_tx = std::min(max_tx + 1, static_cast<int>(terrain.width) - 1);
    max_ty = std::min(max_ty + 1, static_cast<int>(terrain.height) - 1);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    for (int ty = min_ty; ty <= max_ty; ++ty) {
        for (int tx = min_tx; tx <= max_tx; ++tx) {
            const TerrainTile& tile =
                terrain.tile_at(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty));
            SDL_Rect dst = cam.tile_to_screen(tx, ty, tile_size, win_w, win_h);

            SDL_Color c;
            switch (mode) {
                case OverlayMode::Continental:
                    c = continental_color(tile.elev01);
                    break;
                case OverlayMode::Slope:
                    c = slope_color(tile.slope01);
                    break;
                case OverlayMode::ElevBand:
                    c = elevband_color(tile.band);
                    break;
                case OverlayMode::DistOcean:
                    c = dist_color(tile.dist_ocean, stats.dist_ocean_max);
                    break;
                case OverlayMode::Roughness:
                    c = slope_color(tile.roughness);
                    break;
                case OverlayMode::Aspect:
                    c = aspect_color(tile.aspect);
                    break;
                case OverlayMode::Geology:
                    c = erodibility_color(tile.erodibility);
                    break;
                case OverlayMode::SoilTexture:
                    c = soil_depth_color(tile.soil_depth);
                    break;
                default:
                    continue;
            }

            SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
            SDL_RenderFillRect(renderer, &dst);
        }
    }
}

void render_grid(SDL_Renderer* renderer, const Terrain& terrain, const Camera& cam, int win_w,
                 int win_h, int tile_size) {
    int min_tx, min_ty, max_tx, max_ty;
    cam.screen_to_tile(0, 0, win_w, win_h, tile_size, min_tx, min_ty);
    cam.screen_to_tile(win_w, win_h, win_w, win_h, tile_size, max_tx, max_ty);

    min_tx = std::max(min_tx, 0);
    min_ty = std::max(min_ty, 0);
    max_tx = std::min(max_tx + 1, static_cast<int>(terrain.width));
    max_ty = std::min(max_ty + 1, static_cast<int>(terrain.height));

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 128, 128, 128, 60);

    // Vertical lines
    for (int tx = min_tx; tx <= max_tx; ++tx) {
        SDL_Rect top = cam.tile_to_screen(tx, min_ty, tile_size, win_w, win_h);
        SDL_Rect bot = cam.tile_to_screen(tx, max_ty, tile_size, win_w, win_h);
        SDL_RenderDrawLine(renderer, top.x, top.y, bot.x, bot.y + bot.h);
    }

    // Horizontal lines
    for (int ty = min_ty; ty <= max_ty; ++ty) {
        SDL_Rect left = cam.tile_to_screen(min_tx, ty, tile_size, win_w, win_h);
        SDL_Rect right = cam.tile_to_screen(max_tx, ty, tile_size, win_w, win_h);
        SDL_RenderDrawLine(renderer, left.x, left.y, right.x + right.w, right.y);
    }
}

// ── Minimal 5x7 bitmap font ─────────────────────────────────────────────────
// Each glyph is 5 columns x 7 rows, stored as 7 bytes (1 bit per column, MSB=left).

// clang-format off
static const uint8_t FONT_GLYPHS[][7] = {
    // space (32)
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    // A
    {0x04,0x0A,0x11,0x11,0x1F,0x11,0x11},
    // B
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
    // C
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
    // D
    {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},
    // E
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
    // F
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    // G
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0E},
    // H
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    // I
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
    // J
    {0x07,0x02,0x02,0x02,0x02,0x12,0x0C},
    // K
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11},
    // L
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    // M
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},
    // N
    {0x11,0x19,0x15,0x13,0x11,0x11,0x11},
    // O
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
    // P
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    // Q
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},
    // R
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    // S
    {0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E},
    // T
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    // U
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
    // V
    {0x11,0x11,0x11,0x11,0x0A,0x0A,0x04},
    // W
    {0x11,0x11,0x11,0x15,0x15,0x1B,0x11},
    // X
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    // Y
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},
    // Z
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
    // 0
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},
    // 1
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    // 2
    {0x0E,0x11,0x01,0x06,0x08,0x10,0x1F},
    // 3
    {0x0E,0x11,0x01,0x06,0x01,0x11,0x0E},
    // 4
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
    // 5
    {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
    // 6
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},
    // 7
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    // 8
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
    // 9
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
    // . (period)
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C},
    // % (percent)
    {0x18,0x19,0x02,0x04,0x08,0x13,0x03},
    // ( (open paren)
    {0x02,0x04,0x08,0x08,0x08,0x04,0x02},
    // ) (close paren)
    {0x08,0x04,0x02,0x02,0x02,0x04,0x08},
    // = (equals)
    {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00},
    // : (colon)
    {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00},
    // - (hyphen)
    {0x00,0x00,0x00,0x1F,0x00,0x00,0x00},
    // / (slash)
    {0x01,0x02,0x02,0x04,0x08,0x08,0x10},
    // > (greater than)
    {0x10,0x08,0x04,0x02,0x04,0x08,0x10},
};
// clang-format on

static int glyph_index(char ch) {
    if (ch == ' ')
        return 0;
    if (ch >= 'A' && ch <= 'Z')
        return ch - 'A' + 1;
    if (ch >= 'a' && ch <= 'z')
        return ch - 'a' + 1;  // Map lowercase to uppercase
    if (ch >= '0' && ch <= '9')
        return ch - '0' + 27;
    if (ch == '.')
        return 37;
    if (ch == '%')
        return 38;
    if (ch == '(')
        return 39;
    if (ch == ')')
        return 40;
    if (ch == '=')
        return 41;
    if (ch == ':')
        return 42;
    if (ch == '-')
        return 43;
    if (ch == '/')
        return 44;
    if (ch == '>')
        return 45;
    return 0;  // Default to space
}

void draw_text(SDL_Renderer* renderer, int x, int y, const char* text, int scale, uint8_t r,
               uint8_t g, uint8_t b) {
    SDL_SetRenderDrawColor(renderer, r, g, b, 255);
    int cursor_x = x;
    for (const char* p = text; *p; ++p) {
        int idx = glyph_index(*p);
        const uint8_t* glyph = FONT_GLYPHS[idx];
        for (int row = 0; row < 7; ++row) {
            uint8_t bits = glyph[row];
            for (int col = 0; col < 5; ++col) {
                if (bits & (0x10 >> col)) {
                    SDL_Rect px = {cursor_x + col * scale, y + row * scale, scale, scale};
                    SDL_RenderFillRect(renderer, &px);
                }
            }
        }
        cursor_x += 6 * scale;  // 5px glyph + 1px spacing
    }
}

// ── Legend rendering ────────────────────────────────────────────────────────

int text_pixel_width(const char* text, int scale) {
    int len = 0;
    for (const char* p = text; *p; ++p)
        ++len;
    return len > 0 ? len * 6 * scale - scale : 0;  // 5px glyph + 1px spacing, no trailing space
}

void render_legend(SDL_Renderer* renderer, const TerrainStats& stats, int /*win_w*/, int win_h) {
    constexpr int SCALE = 2;        // Pixel scale for font
    constexpr int SWATCH = 14;      // Color swatch size
    constexpr int ROW_H = 18;       // Row height
    constexpr int PAD = 8;          // Panel padding
    constexpr int TEXT_X_OFF = 22;  // Text offset after swatch
    constexpr int GAP = 12;         // Gap between name and percentage columns

    // ElevBand legend
    constexpr int NUM_BANDS = 4;
    const char* band_names[] = {"Water", "Lowland", "Hills", "Mountains"};
    ElevBand band_vals[] = {ElevBand::Water, ElevBand::Lowland, ElevBand::Hills,
                            ElevBand::Mountains};

    int max_name_w = 0;
    for (int i = 0; i < NUM_BANDS; ++i) {
        int w = text_pixel_width(band_names[i], SCALE);
        if (w > max_name_w)
            max_name_w = w;
    }

    // Percentage column: " 100.0%" = 7 chars max
    int pct_col_w = text_pixel_width("100.0%%", SCALE);
    int pct_x_off = TEXT_X_OFF + max_name_w + GAP;

    // Panel dimensions
    int panel_w = pct_x_off + pct_col_w + PAD;
    int panel_h = PAD * 2 + ROW_H * (NUM_BANDS + 1);  // +1 row for header
    int panel_x = PAD;
    int panel_y = win_h - panel_h - PAD;

    // Draw semi-transparent background
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);
    SDL_Rect bg = {panel_x, panel_y, panel_w, panel_h};
    SDL_RenderFillRect(renderer, &bg);

    // Border
    SDL_SetRenderDrawColor(renderer, 80, 80, 80, 200);
    SDL_RenderDrawRect(renderer, &bg);

    int y = panel_y + PAD;

    // Header
    draw_text(renderer, panel_x + PAD, y + 2, "TERRAIN", SCALE, 220, 220, 220);
    y += ROW_H;

    // Band entries
    for (int i = 0; i < NUM_BANDS; ++i) {
        auto c = elevband_color(band_vals[i]);

        // Color swatch
        SDL_Rect swatch = {panel_x + PAD, y + 2, SWATCH, SWATCH};
        SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, 255);
        SDL_RenderFillRect(renderer, &swatch);
        SDL_SetRenderDrawColor(renderer, 40, 40, 40, 255);
        SDL_RenderDrawRect(renderer, &swatch);

        // Band name
        draw_text(renderer, panel_x + TEXT_X_OFF, y + 3, band_names[i], SCALE, 200, 200, 200);

        // Percentage
        float pct = 100.0f * static_cast<float>(stats.band_counts[static_cast<size_t>(i)]) /
                    static_cast<float>(stats.total_tiles);
        char pct_buf[16];
        std::snprintf(pct_buf, sizeof(pct_buf), "%.1f%%", static_cast<double>(pct));
        int pct_w = text_pixel_width(pct_buf, SCALE);
        int pct_x = panel_x + pct_x_off + pct_col_w - pct_w;
        draw_text(renderer, pct_x, y + 3, pct_buf, SCALE, 160, 160, 160);

        y += ROW_H;
    }
}

// ── Overlay legend ──────────────────────────────────────────────────────

// Draw a horizontal gradient bar sampling the given color function
static void draw_gradient_bar(SDL_Renderer* renderer, int x, int y, int w, int h,
                              SDL_Color (*color_fn)(float), float lo, float hi) {
    constexpr int STEPS = 32;
    int step_w = std::max(w / STEPS, 1);
    for (int i = 0; i < STEPS; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(STEPS - 1);
        float v = lo + t * (hi - lo);
        SDL_Color c = color_fn(v);
        SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, 255);
        SDL_Rect r = {x + i * step_w, y, step_w + 1, h};
        SDL_RenderFillRect(renderer, &r);
    }
    // Border
    SDL_SetRenderDrawColor(renderer, 80, 80, 80, 200);
    SDL_Rect border = {x, y, STEPS * step_w, h};
    SDL_RenderDrawRect(renderer, &border);
}

// Distance overlay uses a two-arg color function; wrap it for gradient bar
static float s_dist_max = 1.0f;
static SDL_Color dist_color_wrap(float v) {
    return dist_color(v, s_dist_max);
}

void render_overlay_legend(SDL_Renderer* renderer, const TerrainStats& stats, OverlayMode mode,
                           int /*win_w*/, int win_h) {
    if (mode == OverlayMode::None)
        return;

    constexpr int SCALE = 2;
    constexpr int PAD = 8;
    constexpr int ROW_H = 18;
    constexpr int BAR_W = 192;
    constexpr int BAR_H = 12;
    constexpr int SWATCH = 14;
    constexpr int TEXT_X_OFF = 22;

    // Position: bottom-left, above the terrain legend
    // Terrain legend is ~5 rows tall; place this above it
    int terrain_legend_h = PAD * 2 + ROW_H * 5;

    // Determine content based on overlay mode
    struct SwatchEntry {
        SDL_Color color;
        const char* label;
    };

    // Helper lambda for categorical overlay legends
    auto render_categorical = [&](const char* title, const char* cat_info, SwatchEntry* entries,
                                  int num) {
        int cat_info_w = text_pixel_width(cat_info, 1);
        int max_label_w = 0;
        for (int i = 0; i < num; ++i) {
            int lw = text_pixel_width(entries[i].label, SCALE);
            if (lw > max_label_w)
                max_label_w = lw;
        }
        int swatch_col_w = TEXT_X_OFF + max_label_w + PAD;
        int panel_w = std::max(swatch_col_w, cat_info_w + PAD * 2);
        int panel_h = PAD * 2 + ROW_H * (num + 1) + ROW_H + 4;
        int panel_x = PAD;
        int panel_y = win_h - terrain_legend_h - panel_h - PAD;

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);
        SDL_Rect bg = {panel_x, panel_y, panel_w, panel_h};
        SDL_RenderFillRect(renderer, &bg);
        SDL_SetRenderDrawColor(renderer, 80, 80, 80, 200);
        SDL_RenderDrawRect(renderer, &bg);

        int y = panel_y + PAD;
        draw_text(renderer, panel_x + PAD, y + 2, title, SCALE, 220, 220, 220);
        y += ROW_H;

        for (int i = 0; i < num; ++i) {
            SDL_Rect sw = {panel_x + PAD, y + 2, SWATCH, SWATCH};
            SDL_SetRenderDrawColor(renderer, entries[i].color.r, entries[i].color.g,
                                   entries[i].color.b, 255);
            SDL_RenderFillRect(renderer, &sw);
            SDL_SetRenderDrawColor(renderer, 40, 40, 40, 255);
            SDL_RenderDrawRect(renderer, &sw);
            draw_text(renderer, panel_x + TEXT_X_OFF, y + 3, entries[i].label, SCALE, 200, 200,
                      200);
            y += ROW_H;
        }

        y += 4;
        draw_text(renderer, panel_x + PAD, y + 2, cat_info, 1, 140, 150, 160);
    };

    // Categorical overlays
    if (mode == OverlayMode::ElevBand) {
        SwatchEntry entries[] = {
            {elevband_color(ElevBand::Water), "Water"},
            {elevband_color(ElevBand::Lowland), "Lowland"},
            {elevband_color(ElevBand::Hills), "Hills"},
            {elevband_color(ElevBand::Mountains), "Mountains"},
        };
        render_categorical("ElevBand", "Elevation bands from unified height", entries, 4);
        return;
    }
    // Geology and SoilTexture are now gradient overlays (handled below)

    // Gradient overlays
    const char* lo_label = "0";
    const char* hi_label = "1";
    const char* desc = "";
    const char* info = "";
    SDL_Color (*color_fn)(float) = nullptr;

    switch (mode) {
        case OverlayMode::Continental:
            desc = "Height";
            info = "Unified terrain height (continent + ridges)";
            lo_label = "0 Low";
            hi_label = "1 High";
            color_fn = continental_color;
            break;
        case OverlayMode::Slope:
            desc = "Slope";
            info = "Gradient magnitude of unified height";
            lo_label = "Flat";
            hi_label = "Steep";
            color_fn = slope_color;
            break;
        case OverlayMode::DistOcean:
            desc = "Dist Ocean";
            info = "Euclidean distance to nearest ocean tile";
            lo_label = "Near";
            hi_label = "Far";
            s_dist_max = std::max(stats.dist_ocean_max, 1.0f);
            color_fn = dist_color_wrap;
            break;
        case OverlayMode::Roughness:
            desc = "Roughness";
            info = "Local height variance (3x3 neighborhood)";
            lo_label = "Smooth";
            hi_label = "Rough";
            color_fn = slope_color;  // Reuses slope color mapping
            break;
        case OverlayMode::Aspect:
            desc = "Aspect";
            info = "Slope direction (downhill facing)";
            lo_label = "W";
            hi_label = "E";
            color_fn = aspect_color;
            break;
        case OverlayMode::Geology:
            desc = "Erodibility";
            info = "Susceptibility to erosion (geology-based)";
            lo_label = "Resistant";
            hi_label = "Erodible";
            color_fn = erodibility_color;
            break;
        case OverlayMode::SoilTexture:
            desc = "Soil Depth";
            info = "Regolith depth in meters (0-3m)";
            lo_label = "Shallow";
            hi_label = "Deep";
            color_fn = soil_depth_color;
            break;
        default:
            return;
    }

    // Panel sizing
    int lo_w = text_pixel_width(lo_label, SCALE);
    int hi_w = text_pixel_width(hi_label, SCALE);
    int desc_w = text_pixel_width(desc, SCALE);
    int info_w = text_pixel_width(info, 1);  // info line at scale 1 (small)
    int bar_total = BAR_W;
    int content_w = std::max(bar_total, std::max(desc_w, lo_w + bar_total + hi_w));
    content_w = std::max(content_w, info_w);
    int panel_w = PAD * 2 + content_w;
    int panel_h = PAD * 2 + ROW_H + BAR_H + ROW_H + (info[0] ? ROW_H + 8 : 0);
    int panel_x = PAD;
    int panel_y = win_h - terrain_legend_h - panel_h - PAD;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);
    SDL_Rect bg = {panel_x, panel_y, panel_w, panel_h};
    SDL_RenderFillRect(renderer, &bg);
    SDL_SetRenderDrawColor(renderer, 80, 80, 80, 200);
    SDL_RenderDrawRect(renderer, &bg);

    int y = panel_y + PAD;

    // Title
    draw_text(renderer, panel_x + PAD, y + 2, desc, SCALE, 220, 220, 220);
    y += ROW_H;

    // Gradient bar
    float lo_val = 0.0f;
    float hi_val = 1.0f;
    if (mode == OverlayMode::DistOcean) {
        hi_val = stats.dist_ocean_max;
    } else if (mode == OverlayMode::Aspect) {
        lo_val = -3.14159265f;
        hi_val = 3.14159265f;
    }
    draw_gradient_bar(renderer, panel_x + PAD, y, BAR_W, BAR_H, color_fn, lo_val, hi_val);
    y += BAR_H + 4;

    // Min/max labels
    draw_text(renderer, panel_x + PAD, y, lo_label, SCALE, 160, 160, 160);
    int hi_x = panel_x + PAD + BAR_W - hi_w;
    draw_text(renderer, hi_x, y, hi_label, SCALE, 160, 160, 160);

    // Info description (small text, with gap above)
    if (info[0]) {
        y += ROW_H + 4;
        draw_text(renderer, panel_x + PAD, y, info, 1, 140, 150, 160);
    }
}

// ── UI Button ────────────────────────────────────────────────────────────────

ButtonRect render_button(SDL_Renderer* renderer, int x, int y, const char* label, bool hovered) {
    constexpr int SCALE = 2;
    constexpr int PAD_X = 12;
    constexpr int PAD_Y = 8;

    int text_w = text_pixel_width(label, SCALE);
    int btn_w = text_w + PAD_X * 2;
    int btn_h = 7 * SCALE + PAD_Y * 2;

    ButtonRect rect = {x, y, btn_w, btn_h};

    // Background
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    if (hovered) {
        SDL_SetRenderDrawColor(renderer, 60, 80, 120, 220);
    } else {
        SDL_SetRenderDrawColor(renderer, 40, 50, 70, 200);
    }
    SDL_Rect bg_rect = {x, y, btn_w, btn_h};
    SDL_RenderFillRect(renderer, &bg_rect);

    // Border
    SDL_SetRenderDrawColor(renderer, 100, 120, 160, 230);
    SDL_RenderDrawRect(renderer, &bg_rect);

    // Label
    draw_text(renderer, x + PAD_X, y + PAD_Y, label, SCALE, 220, 220, 240);

    return rect;
}

}  // namespace sandbox
