#include "telemetry.h"

#include "renderer.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>

namespace sandbox {

// ── Overlay names ───────────────────────────────────────────────────────────

const char* overlay_name(OverlayMode mode) {
    switch (mode) {
        case OverlayMode::None: return "None";
        case OverlayMode::Height: return "Height";
        case OverlayMode::Temperature: return "Temperature";
        case OverlayMode::Moisture: return "Moisture";
        case OverlayMode::Toxicity: return "Toxicity";
        case OverlayMode::BiomeColor: return "BiomeColor";
        case OverlayMode::EffectiveMoisture: return "EffMoisture";
        default: return "Unknown";
    }
}

// ── Color mapping ───────────────────────────────────────────────────────────

SDL_Color height_color(float h) {
    auto v = static_cast<uint8_t>(std::clamp(h, 0.0f, 1.0f) * 255.0f);
    return {v, v, v, 180};
}

SDL_Color temperature_color(float temp_K) {
    // Map 200K-350K to blue→white→red
    float t = std::clamp((temp_K - 200.0f) / 150.0f, 0.0f, 1.0f);
    uint8_t r, g, b;
    if (t < 0.5f) {
        float s = t * 2.0f;
        r = static_cast<uint8_t>(s * 255.0f);
        g = static_cast<uint8_t>(s * 255.0f);
        b = 255;
    } else {
        float s = (t - 0.5f) * 2.0f;
        r = 255;
        g = static_cast<uint8_t>((1.0f - s) * 255.0f);
        b = static_cast<uint8_t>((1.0f - s) * 255.0f);
    }
    return {r, g, b, 180};
}

SDL_Color moisture_color(float m) {
    // Brown (dry) to blue (wet)
    float t = std::clamp(m, 0.0f, 1.0f);
    auto r = static_cast<uint8_t>(140.0f * (1.0f - t) + 30.0f * t);
    auto g = static_cast<uint8_t>(100.0f * (1.0f - t) + 80.0f * t);
    auto b = static_cast<uint8_t>(60.0f * (1.0f - t) + 220.0f * t);
    return {r, g, b, 180};
}

SDL_Color toxicity_color(float tox) {
    // Green (low) to purple (high)
    float t = std::clamp(tox, 0.0f, 1.0f);
    auto r = static_cast<uint8_t>(20.0f + t * 140.0f);
    auto g = static_cast<uint8_t>(160.0f * (1.0f - t));
    auto b = static_cast<uint8_t>(20.0f + t * 180.0f);
    return {r, g, b, 180};
}

SDL_Color effective_moisture_color(float em) {
    // Red (dry, low effective moisture) to green (wet, high effective moisture)
    float t = std::clamp(em, 0.0f, 1.0f);
    auto r = static_cast<uint8_t>(200.0f * (1.0f - t) + 20.0f * t);
    auto g = static_cast<uint8_t>(60.0f * (1.0f - t) + 200.0f * t);
    auto b = static_cast<uint8_t>(30.0f * (1.0f - t) + 60.0f * t);
    return {r, g, b, 180};
}

// ── Statistics ──────────────────────────────────────────────────────────────

static void field_stats(const std::vector<float>& values, float& out_min, float& out_max,
                        float& out_mean, float& out_stddev) {
    if (values.empty()) return;

    out_min = *std::min_element(values.begin(), values.end());
    out_max = *std::max_element(values.begin(), values.end());

    double sum = 0.0;
    for (float v : values) sum += static_cast<double>(v);
    out_mean = static_cast<float>(sum / static_cast<double>(values.size()));

    double sq_sum = 0.0;
    for (float v : values) {
        double d = static_cast<double>(v) - static_cast<double>(out_mean);
        sq_sum += d * d;
    }
    out_stddev = static_cast<float>(std::sqrt(sq_sum / static_cast<double>(values.size())));
}

WorldStats compute_stats(const World& world) {
    WorldStats stats;
    stats.total_tiles = static_cast<uint32_t>(world.tiles.size());

    std::vector<float> heights, temps, moistures, toxicities;
    heights.reserve(world.tiles.size());
    temps.reserve(world.tiles.size());
    moistures.reserve(world.tiles.size());
    toxicities.reserve(world.tiles.size());

    for (const auto& tile : world.tiles) {
        stats.biome_counts[static_cast<size_t>(tile.biome)]++;
        heights.push_back(tile.height);
        temps.push_back(tile.temperature_K);
        moistures.push_back(tile.moisture);
        toxicities.push_back(tile.toxins);
    }

    field_stats(heights, stats.height_min, stats.height_max, stats.height_mean,
                stats.height_stddev);
    field_stats(temps, stats.temp_min, stats.temp_max, stats.temp_mean, stats.temp_stddev);
    field_stats(moistures, stats.moisture_min, stats.moisture_max, stats.moisture_mean,
                stats.moisture_stddev);
    field_stats(toxicities, stats.toxicity_min, stats.toxicity_max, stats.toxicity_mean,
                stats.toxicity_stddev);

    return stats;
}

void print_stats(const WorldStats& stats) {
    std::cout << "\n=== World Statistics ===\n";
    std::cout << "Total tiles: " << stats.total_tiles << "\n\n";

    // Biome distribution with bar chart
    std::cout << "Biome Distribution:\n";
    uint32_t max_count = *std::max_element(stats.biome_counts.begin(), stats.biome_counts.end());

    for (size_t i = 0; i < static_cast<size_t>(Biome::COUNT); ++i) {
        uint32_t count = stats.biome_counts[i];
        if (count == 0) continue;

        float pct = 100.0f * static_cast<float>(count) / static_cast<float>(stats.total_tiles);
        int bar_len = (max_count > 0) ? static_cast<int>(30.0f * static_cast<float>(count) /
                                                          static_cast<float>(max_count))
                                      : 0;

        std::cout << "  " << std::left << std::setw(18) << biome_name(static_cast<Biome>(i))
                  << std::right << std::setw(6) << count << " (" << std::fixed
                  << std::setprecision(1) << std::setw(5) << pct << "%) "
                  << std::string(static_cast<size_t>(bar_len), '#') << "\n";
    }

    auto print_field = [](const char* name, float mn, float mx, float mean, float sd) {
        std::cout << "  " << std::left << std::setw(14) << name << "min=" << std::fixed
                  << std::setprecision(2) << std::setw(8) << mn << " max=" << std::setw(8) << mx
                  << " mean=" << std::setw(8) << mean << " stddev=" << std::setw(8) << sd << "\n";
    };

    std::cout << "\nField Statistics:\n";
    print_field("Height", stats.height_min, stats.height_max, stats.height_mean,
                stats.height_stddev);
    print_field("Temp (K)", stats.temp_min, stats.temp_max, stats.temp_mean, stats.temp_stddev);
    print_field("Moisture", stats.moisture_min, stats.moisture_max, stats.moisture_mean,
                stats.moisture_stddev);
    print_field("Toxicity", stats.toxicity_min, stats.toxicity_max, stats.toxicity_mean,
                stats.toxicity_stddev);
    std::cout << "\n";
}

// ── FPS counter ─────────────────────────────────────────────────────────────

void FPSCounter::tick() {
    uint32_t now = SDL_GetTicks();
    frame_times_[static_cast<size_t>(index_)] = now;
    index_ = (index_ + 1) % WINDOW;
    if (count_ < WINDOW) ++count_;

    if (count_ > 1) {
        int oldest = (index_ - count_ + WINDOW) % WINDOW;
        uint32_t elapsed = now - frame_times_[static_cast<size_t>(oldest)];
        if (elapsed > 0) {
            fps_ = static_cast<float>(count_ - 1) * 1000.0f / static_cast<float>(elapsed);
        }
    }
}

// ── Overlay rendering ───────────────────────────────────────────────────────

void render_overlay(SDL_Renderer* renderer, const World& world, const Camera& cam, int win_w,
                    int win_h, OverlayMode mode, int tile_size) {
    if (mode == OverlayMode::None) return;

    // Determine visible tile range
    int min_tx, min_ty, max_tx, max_ty;
    cam.screen_to_tile(0, 0, win_w, win_h, tile_size, min_tx, min_ty);
    cam.screen_to_tile(win_w, win_h, win_w, win_h, tile_size, max_tx, max_ty);

    min_tx = std::max(min_tx - 1, 0);
    min_ty = std::max(min_ty - 1, 0);
    max_tx = std::min(max_tx + 1, static_cast<int>(world.width) - 1);
    max_ty = std::min(max_ty + 1, static_cast<int>(world.height) - 1);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    for (int ty = min_ty; ty <= max_ty; ++ty) {
        for (int tx = min_tx; tx <= max_tx; ++tx) {
            const Tile& tile =
                world.tile_at(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty));
            SDL_Rect dst = cam.tile_to_screen(tx, ty, tile_size, win_w, win_h);

            SDL_Color c;
            switch (mode) {
                case OverlayMode::Height: c = height_color(tile.height); break;
                case OverlayMode::Temperature: c = temperature_color(tile.temperature_K); break;
                case OverlayMode::Moisture: c = moisture_color(tile.moisture); break;
                case OverlayMode::Toxicity: c = toxicity_color(tile.toxins); break;
                case OverlayMode::BiomeColor: {
                    auto bc = biome_color(tile.biome);
                    c = {bc[0], bc[1], bc[2], 200};
                    break;
                }
                case OverlayMode::EffectiveMoisture:
                    c = effective_moisture_color(tile.effective_moisture);
                    break;
                default: continue;
            }

            SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
            SDL_RenderFillRect(renderer, &dst);
        }
    }
}

void render_grid(SDL_Renderer* renderer, const World& world, const Camera& cam, int win_w,
                 int win_h, int tile_size) {
    int min_tx, min_ty, max_tx, max_ty;
    cam.screen_to_tile(0, 0, win_w, win_h, tile_size, min_tx, min_ty);
    cam.screen_to_tile(win_w, win_h, win_w, win_h, tile_size, max_tx, max_ty);

    min_tx = std::max(min_tx, 0);
    min_ty = std::max(min_ty, 0);
    max_tx = std::min(max_tx + 1, static_cast<int>(world.width));
    max_ty = std::min(max_ty + 1, static_cast<int>(world.height));

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
};
// clang-format on

static int glyph_index(char ch) {
    if (ch == ' ') return 0;
    if (ch >= 'A' && ch <= 'Z') return ch - 'A' + 1;
    if (ch >= 'a' && ch <= 'z') return ch - 'a' + 1;  // Map lowercase to uppercase
    if (ch >= '0' && ch <= '9') return ch - '0' + 27;
    if (ch == '.') return 37;
    if (ch == '%') return 38;
    if (ch == '(') return 39;
    if (ch == ')') return 40;
    return 0;  // Default to space
}

static void draw_text(SDL_Renderer* renderer, int x, int y, const char* text, int scale,
                      uint8_t r, uint8_t g, uint8_t b) {
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

static int text_pixel_width(const char* text, int scale) {
    int len = 0;
    for (const char* p = text; *p; ++p) ++len;
    return len > 0 ? len * 6 * scale - scale : 0;  // 5px glyph + 1px spacing, no trailing space
}

void render_legend(SDL_Renderer* renderer, const WorldStats& stats, int /*win_w*/, int win_h) {
    constexpr int SCALE = 2;         // Pixel scale for font
    constexpr int SWATCH = 14;       // Color swatch size
    constexpr int ROW_H = 18;        // Row height
    constexpr int PAD = 8;           // Panel padding
    constexpr int TEXT_X_OFF = 22;   // Text offset after swatch
    constexpr int GAP = 12;          // Gap between name and percentage columns

    // Count visible biomes and find longest name
    int visible = 0;
    int max_name_w = 0;
    for (size_t i = 0; i < static_cast<size_t>(Biome::COUNT); ++i) {
        if (stats.biome_counts[i] > 0) {
            ++visible;
            int w = text_pixel_width(biome_name(static_cast<Biome>(i)), SCALE);
            if (w > max_name_w) max_name_w = w;
        }
    }
    if (visible == 0) return;

    // Percentage column: " 100.0%" = 7 chars max
    int pct_col_w = text_pixel_width("100.0%%", SCALE);
    int pct_x_off = TEXT_X_OFF + max_name_w + GAP;

    // Panel dimensions
    int panel_w = pct_x_off + pct_col_w + PAD;
    int panel_h = PAD * 2 + ROW_H * visible + ROW_H;  // +1 row for header
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
    draw_text(renderer, panel_x + PAD, y + 2, "BIOMES", SCALE, 220, 220, 220);
    y += ROW_H;

    // Biome entries
    for (size_t i = 0; i < static_cast<size_t>(Biome::COUNT); ++i) {
        if (stats.biome_counts[i] == 0) continue;

        auto biome = static_cast<Biome>(i);
        auto color = biome_color(biome);

        // Color swatch
        SDL_Rect swatch = {panel_x + PAD, y + 2, SWATCH, SWATCH};
        SDL_SetRenderDrawColor(renderer, color[0], color[1], color[2], 255);
        SDL_RenderFillRect(renderer, &swatch);
        SDL_SetRenderDrawColor(renderer, 40, 40, 40, 255);
        SDL_RenderDrawRect(renderer, &swatch);

        // Biome name
        draw_text(renderer, panel_x + TEXT_X_OFF, y + 3, biome_name(biome), SCALE, 200, 200, 200);

        // Percentage — right-aligned in its column
        float pct = 100.0f * static_cast<float>(stats.biome_counts[i]) /
                    static_cast<float>(stats.total_tiles);
        char pct_buf[16];
        std::snprintf(pct_buf, sizeof(pct_buf), "%.1f%%", static_cast<double>(pct));
        int pct_w = text_pixel_width(pct_buf, SCALE);
        int pct_x = panel_x + pct_x_off + pct_col_w - pct_w;
        draw_text(renderer, pct_x, y + 3, pct_buf, SCALE, 160, 160, 160);

        y += ROW_H;
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
    SDL_Rect bg = {x, y, btn_w, btn_h};
    SDL_RenderFillRect(renderer, &bg);

    // Border
    SDL_SetRenderDrawColor(renderer, 100, 120, 160, 230);
    SDL_RenderDrawRect(renderer, &bg);

    // Label
    draw_text(renderer, x + PAD_X, y + PAD_Y, label, SCALE, 220, 220, 240);

    return rect;
}

}  // namespace sandbox
