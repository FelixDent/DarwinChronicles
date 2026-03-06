#include "telemetry.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "dynamics.h"
#include "renderer.h"

namespace sandbox {

// ── Color mapping ───────────────────────────────────────────────────────────

SDL_Color temperature_color(float temp_c) {
    // Blue (-20C) → white (10C) → red (40C)
    float t = std::clamp((temp_c + 20.0f) / 60.0f, 0.0f, 1.0f);  // 0=cold, 1=hot

    uint8_t r, g, b;
    if (t < 0.5f) {
        // Blue → white
        float s = t * 2.0f;
        r = static_cast<uint8_t>(30.0f + s * 225.0f);
        g = static_cast<uint8_t>(60.0f + s * 195.0f);
        b = static_cast<uint8_t>(200.0f + s * 55.0f);
    } else {
        // White → red
        float s = (t - 0.5f) * 2.0f;
        r = static_cast<uint8_t>(255.0f);
        g = static_cast<uint8_t>(255.0f * (1.0f - s));
        b = static_cast<uint8_t>(255.0f * (1.0f - s));
    }
    return {r, g, b, 180};
}

SDL_Color precipitation_color(float precip) {
    // Brown (dry) → green (moderate) → blue (wet)
    float t = std::clamp(precip, 0.0f, 1.0f);

    uint8_t r, g, b;
    if (t < 0.5f) {
        float s = t * 2.0f;
        r = static_cast<uint8_t>(160.0f * (1.0f - s) + 30.0f * s);
        g = static_cast<uint8_t>(110.0f * (1.0f - s) + 160.0f * s);
        b = static_cast<uint8_t>(50.0f * (1.0f - s) + 40.0f * s);
    } else {
        float s = (t - 0.5f) * 2.0f;
        r = static_cast<uint8_t>(30.0f * (1.0f - s) + 20.0f * s);
        g = static_cast<uint8_t>(160.0f * (1.0f - s) + 80.0f * s);
        b = static_cast<uint8_t>(40.0f * (1.0f - s) + 220.0f * s);
    }
    return {r, g, b, 180};
}

SDL_Color moisture_color(float moisture) {
    // Brown (dry) → green (wet)
    float t = std::clamp(moisture, 0.0f, 1.0f);
    auto r = static_cast<uint8_t>(150.0f * (1.0f - t) + 20.0f * t);
    auto g = static_cast<uint8_t>(100.0f * (1.0f - t) + 180.0f * t);
    auto b = static_cast<uint8_t>(50.0f * (1.0f - t) + 40.0f * t);
    return {r, g, b, 180};
}

SDL_Color evaporation_color(float evap) {
    // Yellow → red
    float t = std::clamp(evap, 0.0f, 1.0f);
    auto r = static_cast<uint8_t>(220.0f + t * 35.0f);
    auto g = static_cast<uint8_t>(200.0f * (1.0f - t * 0.7f));
    auto b = static_cast<uint8_t>(30.0f * (1.0f - t));
    return {r, g, b, 180};
}

SDL_Color storminess_color(float storm) {
    // Gray → purple
    float t = std::clamp(storm, 0.0f, 1.0f);
    auto r = static_cast<uint8_t>(120.0f * (1.0f - t) + 160.0f * t);
    auto g = static_cast<uint8_t>(120.0f * (1.0f - t) + 40.0f * t);
    auto b = static_cast<uint8_t>(130.0f * (1.0f - t) + 200.0f * t);
    return {r, g, b, 180};
}

SDL_Color surface_water_color(float sw) {
    // Tan (dry) → cyan (puddles) → deep blue (flooded)
    float t = std::clamp(sw, 0.0f, 1.0f);
    auto r = static_cast<uint8_t>(180.0f * (1.0f - t) + 20.0f * t);
    auto g = static_cast<uint8_t>(160.0f * (1.0f - t) + 120.0f * t);
    auto b = static_cast<uint8_t>(80.0f * (1.0f - t) + 220.0f * t);
    return {r, g, b, 180};
}

SDL_Color soil_moisture_color(float sm) {
    // Brown (dry) → dark green (saturated)
    float t = std::clamp(sm, 0.0f, 1.0f);
    auto r = static_cast<uint8_t>(160.0f * (1.0f - t) + 30.0f * t);
    auto g = static_cast<uint8_t>(120.0f * (1.0f - t) + 140.0f * t);
    auto b = static_cast<uint8_t>(60.0f * (1.0f - t) + 40.0f * t);
    return {r, g, b, 180};
}

SDL_Color snow_depth_color(float snow) {
    // Transparent → bright white
    float t = std::clamp(snow * 3.0f, 0.0f, 1.0f);  // saturates at snow=0.33
    auto a = static_cast<uint8_t>(t * 220.0f);
    return {240, 240, 255, a};
}

// ── Wind direction name ─────────────────────────────────────────────────────

const char* wind_direction_name(float u, float v) {
    float speed = std::sqrt(u * u + v * v);
    if (speed < 0.05f)
        return "calm";

    // atan2(v, u): 0=E, PI/2=S, -PI/2=N (v=south-positive, u=east-positive)
    float angle = std::atan2(v, u);
    constexpr float PI = 3.14159265358979f;
    constexpr float EIGHTH = PI / 8.0f;

    // 8-way compass: angle bins centered on each direction
    if (angle >= -EIGHTH && angle < EIGHTH)
        return "E";
    if (angle >= EIGHTH && angle < 3.0f * EIGHTH)
        return "SE";
    if (angle >= 3.0f * EIGHTH && angle < 5.0f * EIGHTH)
        return "S";
    if (angle >= 5.0f * EIGHTH && angle < 7.0f * EIGHTH)
        return "SW";
    if (angle >= 7.0f * EIGHTH || angle < -7.0f * EIGHTH)
        return "W";
    if (angle >= -7.0f * EIGHTH && angle < -5.0f * EIGHTH)
        return "NW";
    if (angle >= -5.0f * EIGHTH && angle < -3.0f * EIGHTH)
        return "N";
    return "NE";  // -3*EIGHTH to -EIGHTH
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

// ── Minimal 5x7 bitmap font ─────────────────────────────────────────────────

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
    // - (minus/dash)
    {0x00,0x00,0x00,0x1F,0x00,0x00,0x00},
    // : (colon)
    {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00},
    // < (less than)
    {0x02,0x04,0x08,0x10,0x08,0x04,0x02},
    // > (greater than)
    {0x08,0x04,0x02,0x01,0x02,0x04,0x08},
    // @ (at sign - used for wind display)
    {0x0E,0x11,0x17,0x15,0x17,0x10,0x0E},
};
// clang-format on

static int glyph_index(char ch) {
    if (ch == ' ')
        return 0;
    if (ch >= 'A' && ch <= 'Z')
        return ch - 'A' + 1;
    if (ch >= 'a' && ch <= 'z')
        return ch - 'a' + 1;
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
    if (ch == '-')
        return 42;
    if (ch == ':')
        return 43;
    if (ch == '<')
        return 44;
    if (ch == '>')
        return 45;
    if (ch == '@')
        return 46;
    return 0;
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
        cursor_x += 6 * scale;
    }
}

int text_pixel_width(const char* text, int scale) {
    int len = 0;
    for (const char* p = text; *p; ++p)
        ++len;
    return len > 0 ? len * 6 * scale - scale : 0;
}

// ── Gradient bar helper ──────────────────────────────────────────────────────

using ColorFn = SDL_Color (*)(float);

static void render_gradient_bar(SDL_Renderer* renderer, int x, int y, int bar_w, int bar_h,
                                ColorFn color_fn) {
    for (int i = 0; i < bar_w; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(bar_w - 1);
        SDL_Color c = color_fn(t);
        SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, 255);
        SDL_Rect col = {x + i, y, 1, bar_h};
        SDL_RenderFillRect(renderer, &col);
    }
    // Border
    SDL_SetRenderDrawColor(renderer, 80, 80, 80, 200);
    SDL_Rect border = {x, y, bar_w, bar_h};
    SDL_RenderDrawRect(renderer, &border);
}

// Temperature needs special handling: maps -20..40 to the color fn
static SDL_Color temp_gradient_sample(float t) {
    float temp_c = -20.0f + t * 60.0f;
    return temperature_color(temp_c);
}

// ── Legend panel helper ─────────────────────────────────────────────────────

static void draw_panel_bg(SDL_Renderer* renderer, int x, int y, int w, int h) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);
    SDL_Rect bg = {x, y, w, h};
    SDL_RenderFillRect(renderer, &bg);
    SDL_SetRenderDrawColor(renderer, 80, 80, 80, 200);
    SDL_RenderDrawRect(renderer, &bg);
}

// ── Legend rendering ────────────────────────────────────────────────────────

// Measure the widest string in a list and return panel width with padding
static int panel_width_for(std::initializer_list<const char*> lines, int scale, int pad) {
    int max_w = 0;
    for (const char* line : lines) {
        int w = text_pixel_width(line, scale);
        if (w > max_w)
            max_w = w;
    }
    return max_w + pad * 2;
}

void render_weather_legend(SDL_Renderer* renderer, const WeatherStats& stats, OverlayMode overlay,
                           int /*win_w*/, int win_h) {
    constexpr int SCALE = 2;
    constexpr int ROW_H = 18;
    constexpr int PAD = 8;
    constexpr int BAR_H = 14;

    char buf[80];

    // ── Stats panel (bottom-left, always shown when weather is baked) ────────
    int stats_panel_w;
    {
        // Pre-format lines to measure widths
        char t_buf[48], p_buf[48], m_buf[48], e_buf[48], w_buf[32], s_buf[32], r_buf[32];
        std::snprintf(t_buf, sizeof(t_buf), "T: %.1f to %.1f C",
                      static_cast<double>(stats.temp_min), static_cast<double>(stats.temp_max));
        std::snprintf(p_buf, sizeof(p_buf), "P: %.2f to %.2f",
                      static_cast<double>(stats.precip_min), static_cast<double>(stats.precip_max));
        std::snprintf(m_buf, sizeof(m_buf), "M: %.2f to %.2f",
                      static_cast<double>(stats.moisture_min),
                      static_cast<double>(stats.moisture_max));
        std::snprintf(e_buf, sizeof(e_buf), "E: %.2f to %.2f", static_cast<double>(stats.evap_min),
                      static_cast<double>(stats.evap_max));
        std::snprintf(w_buf, sizeof(w_buf), "Wind max: %.2f",
                      static_cast<double>(stats.wind_speed_max));
        std::snprintf(s_buf, sizeof(s_buf), "Storms: %u", stats.storm_tiles);
        std::snprintf(r_buf, sizeof(r_buf), "Shadows: %u", stats.rain_shadow_tiles);

        stats_panel_w = panel_width_for(
            {t_buf, p_buf, m_buf, e_buf, w_buf, s_buf, r_buf, "WEATHER STATS"}, SCALE, PAD * 2);

        constexpr int STATS_ROWS = 8;
        int panel_h = PAD * 2 + ROW_H * (STATS_ROWS + 1);
        int panel_x = PAD;
        int panel_y = win_h - panel_h - PAD;

        draw_panel_bg(renderer, panel_x, panel_y, stats_panel_w, panel_h);

        int y = panel_y + PAD;
        draw_text(renderer, panel_x + PAD, y + 2, "WEATHER STATS", SCALE, 220, 220, 220);
        y += ROW_H;

        draw_text(renderer, panel_x + PAD, y + 2, t_buf, SCALE, 200, 200, 200);
        y += ROW_H;
        draw_text(renderer, panel_x + PAD, y + 2, p_buf, SCALE, 200, 200, 200);
        y += ROW_H;
        draw_text(renderer, panel_x + PAD, y + 2, m_buf, SCALE, 200, 200, 200);
        y += ROW_H;
        draw_text(renderer, panel_x + PAD, y + 2, e_buf, SCALE, 200, 200, 200);
        y += ROW_H;
        draw_text(renderer, panel_x + PAD, y + 2, w_buf, SCALE, 200, 200, 200);
        y += ROW_H;
        draw_text(renderer, panel_x + PAD, y + 2, s_buf, SCALE, 200, 200, 200);
        y += ROW_H;
        draw_text(renderer, panel_x + PAD, y + 2, r_buf, SCALE, 200, 200, 200);
    }

    // ── Overlay color key (right of stats, shown when overlay is active) ────
    if (overlay == OverlayMode::None)
        return;

    int key_x = PAD + stats_panel_w + PAD;

    // Helper: draw a gradient key panel (title, gradient bar, min/max labels)
    auto draw_gradient_key = [&](const char* title, ColorFn color_fn, const char* lo_label,
                                 const char* hi_label, uint8_t lo_r, uint8_t lo_g, uint8_t lo_b,
                                 uint8_t hi_r, uint8_t hi_g, uint8_t hi_b,
                                 const char* mid_label = nullptr) {
        int bar_w = std::max(180, panel_width_for({title, lo_label}, SCALE, PAD) - PAD);
        int key_w = bar_w + PAD * 2;
        int key_rows = 3;
        int key_h = PAD * 2 + ROW_H * key_rows + BAR_H;
        int key_y = win_h - key_h - PAD;

        draw_panel_bg(renderer, key_x, key_y, key_w, key_h);
        int y = key_y + PAD;
        draw_text(renderer, key_x + PAD, y + 2, title, SCALE, 220, 220, 220);
        y += ROW_H;

        render_gradient_bar(renderer, key_x + PAD, y, bar_w, BAR_H, color_fn);
        y += BAR_H + 4;

        draw_text(renderer, key_x + PAD, y, lo_label, SCALE, lo_r, lo_g, lo_b);
        if (mid_label) {
            int mw = text_pixel_width(mid_label, SCALE);
            draw_text(renderer, key_x + PAD + bar_w / 2 - mw / 2, y, mid_label, SCALE, 220, 220,
                      220);
        }
        int rw = text_pixel_width(hi_label, SCALE);
        draw_text(renderer, key_x + PAD + bar_w - rw, y, hi_label, SCALE, hi_r, hi_g, hi_b);
    };

    switch (overlay) {
        case OverlayMode::Temperature:
            draw_gradient_key("TEMPERATURE", temp_gradient_sample, "-20C", "40C", 120, 160, 255,
                              255, 100, 100, "10C");
            break;

        case OverlayMode::Precipitation:
            draw_gradient_key("PRECIPITATION", precipitation_color, "Dry", "Wet", 160, 110, 50, 20,
                              80, 220);
            break;

        case OverlayMode::Moisture:
            draw_gradient_key("MOISTURE", moisture_color, "Arid", "Wet", 150, 100, 50, 20, 180, 40);
            break;

        case OverlayMode::WindDirection: {
            const char* lines[] = {"PREVAILING WINDS", "-->  = wind direction",
                                   "0-30: Trade easterlies", "30-60: Westerlies",
                                   "60-90: Polar easterlies"};
            int key_w = panel_width_for({lines[0], lines[1], lines[2], lines[4]}, SCALE, PAD * 2);
            int key_rows = 7;
            int key_h = PAD * 2 + ROW_H * key_rows;
            int key_y = win_h - key_h - PAD;

            draw_panel_bg(renderer, key_x, key_y, key_w, key_h);
            int y = key_y + PAD;
            draw_text(renderer, key_x + PAD, y + 2, lines[0], SCALE, 220, 220, 220);
            y += ROW_H;
            draw_text(renderer, key_x + PAD, y + 2, lines[1], SCALE, 255, 240, 100);
            y += ROW_H;
            draw_text(renderer, key_x + PAD, y + 2, lines[2], SCALE, 200, 200, 200);
            y += ROW_H;
            draw_text(renderer, key_x + PAD, y + 2, lines[3], SCALE, 200, 200, 200);
            y += ROW_H;
            draw_text(renderer, key_x + PAD, y + 2, lines[4], SCALE, 200, 200, 200);
            y += ROW_H;
            draw_text(renderer, key_x + PAD, y + 2, "Bright=fast Dim=slow", SCALE, 160, 160, 160);
            y += ROW_H;
            draw_text(renderer, key_x + PAD, y + 2, "V: toggle arrows", SCALE, 160, 160, 160);
            break;
        }

        case OverlayMode::Evaporation:
            draw_gradient_key("EVAPORATION", evaporation_color, "Low", "High", 220, 200, 30, 255,
                              60, 0);
            break;

        case OverlayMode::Storminess: {
            draw_gradient_key("STORMINESS", storminess_color, "Calm", "Stormy", 120, 120, 130, 160,
                              40, 200);

            // Factor legend panel to the right of the gradient key
            const char* f_lines[] = {"5-FACTOR MODEL",    "30% Moisture convergence",
                                     "20% Wind shear",    "20% Thermal instability",
                                     "15% Frontal zones", "15% Cyclogenesis band"};
            int f_key_w =
                panel_width_for({f_lines[0], f_lines[1], f_lines[2], f_lines[5]}, SCALE, PAD * 2);
            int f_key_h = PAD * 2 + ROW_H * 6;
            int f_key_x =
                key_x + std::max(180, panel_width_for({"STORMINESS"}, SCALE, PAD)) + PAD * 2 + PAD;
            int f_key_y = win_h - f_key_h - PAD;

            draw_panel_bg(renderer, f_key_x, f_key_y, f_key_w, f_key_h);
            int fy = f_key_y + PAD;
            draw_text(renderer, f_key_x + PAD, fy + 2, f_lines[0], SCALE, 220, 220, 220);
            fy += ROW_H;
            for (int fi = 1; fi <= 5; ++fi) {
                draw_text(renderer, f_key_x + PAD, fy + 2, f_lines[fi], SCALE, 180, 180, 180);
                fy += ROW_H;
            }
            break;
        }

        case OverlayMode::MoistureBars: {
            const char* l1 = "MOISTURE BARS";
            const char* l2 = "Taller = wetter";
            int key_w = panel_width_for({l1, l2}, SCALE, PAD * 2);
            int key_rows = 3;
            int key_h = PAD * 2 + ROW_H * key_rows;
            int key_y = win_h - key_h - PAD;

            draw_panel_bg(renderer, key_x, key_y, key_w, key_h);
            int y = key_y + PAD;
            draw_text(renderer, key_x + PAD, y + 2, l1, SCALE, 220, 220, 220);
            y += ROW_H;

            SDL_SetRenderDrawColor(renderer, 40, 80, 220, 180);
            SDL_Rect sample = {key_x + PAD, y + 2, 8, 12};
            SDL_RenderFillRect(renderer, &sample);
            draw_text(renderer, key_x + PAD + 14, y + 2, "= moisture", SCALE, 200, 200, 200);
            y += ROW_H;
            draw_text(renderer, key_x + PAD, y + 2, l2, SCALE, 160, 160, 160);
            break;
        }

        case OverlayMode::RainShadow: {
            const char* l1 = "RAIN SHADOW";
            std::snprintf(buf, sizeof(buf), "%u leeward tiles", stats.rain_shadow_tiles);
            int key_w = panel_width_for({l1, buf, "Leeward of mountains"}, SCALE, PAD * 2);
            int key_rows = 4;
            int key_h = PAD * 2 + ROW_H * key_rows;
            int key_y = win_h - key_h - PAD;

            draw_panel_bg(renderer, key_x, key_y, key_w, key_h);
            int y = key_y + PAD;
            draw_text(renderer, key_x + PAD, y + 2, l1, SCALE, 220, 220, 220);
            y += ROW_H;

            // Gradient swatch: light orange → deep orange
            for (int i = 0; i < 60; ++i) {
                float t = static_cast<float>(i) / 59.0f;
                auto r = static_cast<uint8_t>(255);
                auto g = static_cast<uint8_t>(160.0f * (1.0f - t * 0.6f));
                auto b = static_cast<uint8_t>(40.0f * (1.0f - t));
                SDL_SetRenderDrawColor(renderer, r, g, b, 220);
                SDL_Rect px = {key_x + PAD + i, y + 2, 1, 12};
                SDL_RenderFillRect(renderer, &px);
            }
            draw_text(renderer, key_x + PAD + 66, y + 2, "= shadow", SCALE, 200, 200, 200);
            y += ROW_H;

            draw_text(renderer, key_x + PAD, y + 2, "Leeward of mountains", SCALE, 160, 160, 160);
            y += ROW_H;
            draw_text(renderer, key_x + PAD, y + 2, buf, SCALE, 160, 160, 160);
            break;
        }

        default:
            break;
    }
}

// ── Dynamic simulation legend ───────────────────────────────────────────────

void render_dynamic_legend(SDL_Renderer* renderer, const DynamicStats& stats,
                           const AtmosphereStats& atmo_stats, float elapsed_days, float time_scale,
                           bool paused, OverlayMode overlay, int win_w, int /*win_h*/,
                           float time_of_day, float day_of_year) {
    constexpr int SCALE = 2;
    constexpr int ROW_H = 18;
    constexpr int PAD = 8;

    int panel_w = text_pixel_width("CLOUD: 0.000 MAX 0.000", SCALE) + PAD * 4;
    int panel_h = PAD * 2 + ROW_H * 19;
    int panel_x = win_w - panel_w - PAD;
    int panel_y = 40;

    draw_panel_bg(renderer, panel_x, panel_y, panel_w, panel_h);

    int y = panel_y + PAD;
    char buf[80];

    draw_text(renderer, panel_x + PAD, y + 2, "DYNAMICS", SCALE, 255, 220, 100);
    y += ROW_H;

    int day = static_cast<int>(elapsed_days);
    if (paused)
        std::snprintf(buf, sizeof(buf), "DAY %d  PAUSED", day);
    else
        std::snprintf(buf, sizeof(buf), "DAY %d  %.1fx", day, static_cast<double>(time_scale));
    draw_text(renderer, panel_x + PAD, y + 2, buf, SCALE, 200, 200, 200);
    y += ROW_H;

    // Season + time of day
    const char* seasons[] = {"SPRING", "SUMMER", "AUTUMN", "WINTER"};
    int season_idx = static_cast<int>((day_of_year + 10.0f) / 91.25f) % 4;
    int hour = static_cast<int>(time_of_day);
    int minute = static_cast<int>((time_of_day - static_cast<float>(hour)) * 60.0f);
    std::snprintf(buf, sizeof(buf), "%s %02d:%02d", seasons[season_idx], hour, minute);
    draw_text(renderer, panel_x + PAD, y + 2, buf, SCALE, 220, 200, 160);
    y += ROW_H;

    // ── Ground state ─────────────────
    std::snprintf(buf, sizeof(buf), "SURFACE WATER: %.4f",
                  static_cast<double>(stats.surface_water_mean));
    draw_text(renderer, panel_x + PAD, y + 2, buf, SCALE, 100, 180, 220);
    y += ROW_H;

    std::snprintf(buf, sizeof(buf), "  MAX: %.4f", static_cast<double>(stats.surface_water_max));
    draw_text(renderer, panel_x + PAD, y + 2, buf, SCALE, 80, 150, 200);
    y += ROW_H;

    std::snprintf(buf, sizeof(buf), "SOIL MOISTURE: %.3f",
                  static_cast<double>(stats.soil_moisture_mean));
    draw_text(renderer, panel_x + PAD, y + 2, buf, SCALE, 120, 180, 80);
    y += ROW_H;

    std::snprintf(buf, sizeof(buf), "SNOW: %u  FLOODED: %u", stats.snow_tiles,
                  stats.flooded_tiles);
    draw_text(renderer, panel_x + PAD, y + 2, buf, SCALE, 200, 200, 240);
    y += ROW_H;

    // ── Atmosphere state ─────────────
    draw_text(renderer, panel_x + PAD, y + 2, "ATMOSPHERE", SCALE, 255, 200, 100);
    y += ROW_H;

    std::snprintf(buf, sizeof(buf), "T: %.0f / %.0f / %.0f", static_cast<double>(atmo_stats.T_min),
                  static_cast<double>(atmo_stats.T_mean), static_cast<double>(atmo_stats.T_max));
    draw_text(renderer, panel_x + PAD, y + 2, buf, SCALE, 200, 180, 180);
    y += ROW_H;

    std::snprintf(buf, sizeof(buf), "Q: %.3f AVG %.3f",
                  static_cast<double>(atmo_stats.q_max), static_cast<double>(atmo_stats.q_mean));
    draw_text(renderer, panel_x + PAD, y + 2, buf, SCALE, 180, 200, 180);
    y += ROW_H;

    std::snprintf(buf, sizeof(buf), "CLOUD: %.3f MAX %.3f",
                  static_cast<double>(atmo_stats.cloud_mean),
                  static_cast<double>(atmo_stats.cloud_max));
    draw_text(renderer, panel_x + PAD, y + 2, buf, SCALE, 180, 180, 200);
    y += ROW_H;

    std::snprintf(buf, sizeof(buf), "PRECIP: %.3f MAX %.3f",
                  static_cast<double>(atmo_stats.precip_mean),
                  static_cast<double>(atmo_stats.precip_max));
    draw_text(renderer, panel_x + PAD, y + 2, buf, SCALE, 100, 180, 220);
    y += ROW_H;

    std::snprintf(buf, sizeof(buf), "RAIN %u  CLOUDY %u", atmo_stats.raining_cells,
                  atmo_stats.cloudy_cells);
    draw_text(renderer, panel_x + PAD, y + 2, buf, SCALE, 160, 160, 200);
    y += ROW_H;

    std::snprintf(buf, sizeof(buf), "WIND: %.2f MAX %.2f",
                  static_cast<double>(atmo_stats.wind_mean),
                  static_cast<double>(atmo_stats.wind_max));
    draw_text(renderer, panel_x + PAD, y + 2, buf, SCALE, 200, 200, 160);
    y += ROW_H;

    // ── Invariant monitoring ─────────
    draw_text(renderer, panel_x + PAD, y + 2, "INVARIANTS", SCALE, 255, 180, 100);
    y += ROW_H;

    std::snprintf(buf, sizeof(buf), "H2O: %.1f CFL: %.2f",
                  static_cast<double>(atmo_stats.total_atmo_water),
                  static_cast<double>(atmo_stats.max_courant));
    // Color CFL red if > 1.0 (violation), green if safe
    uint8_t cfl_r = atmo_stats.max_courant > 1.0f ? 255 : 120;
    uint8_t cfl_g = atmo_stats.max_courant > 1.0f ? 100 : 200;
    draw_text(renderer, panel_x + PAD, y + 2, buf, SCALE, cfl_r, cfl_g, 160);
    y += ROW_H;

    if (std::abs(atmo_stats.water_correction) > 0.001f) {
        std::snprintf(buf, sizeof(buf), "CORR: %.4f",
                      static_cast<double>(atmo_stats.water_correction));
        draw_text(renderer, panel_x + PAD, y + 2, buf, SCALE, 255, 180, 100);
    }
    y += ROW_H;

    if (overlay != OverlayMode::None) {
        const char* ov_name = overlay_name(overlay);
        std::snprintf(buf, sizeof(buf), "OVERLAY: %s", ov_name);
        draw_text(renderer, panel_x + PAD, y + 2, buf, SCALE, 200, 220, 255);
    }
}

// ── Grid rendering ──────────────────────────────────────────────────────────

void render_grid(SDL_Renderer* renderer, uint32_t world_w, uint32_t world_h, const Camera& cam,
                 int win_w, int win_h, int tile_size) {
    int min_tx, min_ty, max_tx, max_ty;
    cam.screen_to_tile(0, 0, win_w, win_h, tile_size, min_tx, min_ty);
    cam.screen_to_tile(win_w, win_h, win_w, win_h, tile_size, max_tx, max_ty);

    min_tx = std::max(min_tx, 0);
    min_ty = std::max(min_ty, 0);
    max_tx = std::min(max_tx + 1, static_cast<int>(world_w));
    max_ty = std::min(max_ty + 1, static_cast<int>(world_h));

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 128, 128, 128, 60);

    for (int tx = min_tx; tx <= max_tx; ++tx) {
        SDL_Rect top = cam.tile_to_screen(tx, min_ty, tile_size, win_w, win_h);
        SDL_Rect bot = cam.tile_to_screen(tx, max_ty, tile_size, win_w, win_h);
        SDL_RenderDrawLine(renderer, top.x, top.y, bot.x, bot.y + bot.h);
    }

    for (int ty = min_ty; ty <= max_ty; ++ty) {
        SDL_Rect left = cam.tile_to_screen(min_tx, ty, tile_size, win_w, win_h);
        SDL_Rect right = cam.tile_to_screen(max_tx, ty, tile_size, win_w, win_h);
        SDL_RenderDrawLine(renderer, left.x, left.y, right.x + right.w, right.y);
    }
}

}  // namespace sandbox
