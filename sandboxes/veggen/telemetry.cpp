#include "telemetry.h"

#include <algorithm>
#include <cmath>

namespace veggen {

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
// Each glyph is 5 columns x 7 rows, stored as 7 bytes (1 bit per column, MSB=left).

// clang-format off
static const uint8_t FONT_GLYPHS[][7] = {
    // space (0)
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    // A (1)
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
    // . (period) (37)
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C},
    // % (38)
    {0x18,0x19,0x02,0x04,0x08,0x13,0x03},
    // ( (39)
    {0x02,0x04,0x08,0x08,0x08,0x04,0x02},
    // ) (40)
    {0x08,0x04,0x02,0x02,0x02,0x04,0x08},
    // = (41)
    {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00},
    // : (42)
    {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00},
    // - (43)
    {0x00,0x00,0x00,0x1F,0x00,0x00,0x00},
    // + (44)
    {0x00,0x04,0x04,0x1F,0x04,0x04,0x00},
    // / (45)
    {0x01,0x02,0x02,0x04,0x08,0x08,0x10},
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
    if (ch == ':')
        return 42;
    if (ch == '-')
        return 43;
    if (ch == '+')
        return 44;
    if (ch == '/')
        return 45;
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

void draw_panel_bg(SDL_Renderer* renderer, int x, int y, int w, int h) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);
    SDL_Rect bg = {x, y, w, h};
    SDL_RenderFillRect(renderer, &bg);
    SDL_SetRenderDrawColor(renderer, 80, 80, 80, 200);
    SDL_RenderDrawRect(renderer, &bg);
}

// ── Vegetation-specific colors ──────────────────────────────────────────────

SDL_Color health_color(PlantHealth health) {
    switch (health) {
        case PlantHealth::Healthy:
            return {60, 200, 60, 255};
        case PlantHealth::Stressed:
            return {220, 200, 40, 255};
        case PlantHealth::Brown:
            return {160, 100, 40, 255};
        case PlantHealth::Dead:
            return {120, 120, 120, 255};
    }
    return {200, 200, 200, 255};
}

SDL_Color soil_moisture_color(float moisture) {
    float t = std::clamp(moisture, 0.0f, 1.0f);
    // Brown (dry) -> Blue (wet)
    auto r = static_cast<uint8_t>(160.0f * (1.0f - t) + 30.0f * t);
    auto g = static_cast<uint8_t>(110.0f * (1.0f - t) + 80.0f * t);
    auto b = static_cast<uint8_t>(60.0f * (1.0f - t) + 200.0f * t);
    return {r, g, b, 200};
}

SDL_Color temperature_color(float temp_c) {
    // Map -20..40 to blue..red
    float t = std::clamp((temp_c + 20.0f) / 60.0f, 0.0f, 1.0f);
    auto r = static_cast<uint8_t>(t * 220.0f + 20.0f);
    auto g = static_cast<uint8_t>((1.0f - std::abs(t - 0.5f) * 2.0f) * 180.0f + 20.0f);
    auto b = static_cast<uint8_t>((1.0f - t) * 220.0f + 20.0f);
    return {r, g, b, 200};
}

SDL_Color elevation_color(float elev01) {
    // Dark green (low) -> brown (mid) -> white (high)
    float t = std::clamp(elev01, 0.0f, 1.0f);
    uint8_t r, g, b;
    if (t < 0.5f) {
        float s = t * 2.0f;  // 0..1 within low half
        r = static_cast<uint8_t>(30.0f + s * 110.0f);
        g = static_cast<uint8_t>(100.0f + s * 20.0f);
        b = static_cast<uint8_t>(30.0f + s * 30.0f);
    } else {
        float s = (t - 0.5f) * 2.0f;  // 0..1 within high half
        r = static_cast<uint8_t>(140.0f + s * 100.0f);
        g = static_cast<uint8_t>(120.0f + s * 120.0f);
        b = static_cast<uint8_t>(60.0f + s * 180.0f);
    }
    return {r, g, b, 200};
}

SDL_Color surface_water_color(float surface_water) {
    // Tan (dry) -> cyan (puddles) -> deep blue (flooded)
    float t = std::clamp(surface_water, 0.0f, 1.0f);
    auto r = static_cast<uint8_t>(180.0f * (1.0f - t) + 20.0f * t);
    auto g = static_cast<uint8_t>(160.0f * (1.0f - t) + 120.0f * t);
    auto b = static_cast<uint8_t>(80.0f * (1.0f - t) + 220.0f * t);
    return {r, g, b, 200};
}

SDL_Color canopy_color(float canopy_cover) {
    // Pale (open) -> dark green (dense canopy)
    float t = std::clamp(canopy_cover, 0.0f, 1.0f);
    auto r = static_cast<uint8_t>(200.0f * (1.0f - t) + 20.0f * t);
    auto g = static_cast<uint8_t>(200.0f * (1.0f - t) + 140.0f * t);
    auto b = static_cast<uint8_t>(180.0f * (1.0f - t) + 30.0f * t);
    return {r, g, b, 200};
}

}  // namespace veggen
