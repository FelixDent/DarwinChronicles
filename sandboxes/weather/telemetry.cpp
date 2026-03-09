#include "telemetry.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "dynamics.h"
#include "renderer.h"
#include "terrain_gen.h"
#include "ui.h"

namespace sandbox {

// ── Color mapping ───────────────────────────────────────────────────────────

SDL_Color temperature_color(float temp_c) {
    // Perceptually uniform diverging: blue (#2C7BB6) → near-white (#F7F7F7) → red (#D7191C)
    float t = std::clamp((temp_c + 20.0f) / 60.0f, 0.0f, 1.0f);  // 0=cold, 1=hot

    uint8_t r, g, b;
    if (t < 0.5f) {
        // Blue (#2C7BB6) → near-white (#F7F7F7)
        float s = t * 2.0f;
        r = static_cast<uint8_t>(44.0f + s * 203.0f);   // 2C → F7
        g = static_cast<uint8_t>(123.0f + s * 124.0f);  // 7B → F7
        b = static_cast<uint8_t>(182.0f + s * 65.0f);   // B6 → F7
    } else {
        // Near-white (#F7F7F7) → red (#D7191C)
        float s = (t - 0.5f) * 2.0f;
        r = static_cast<uint8_t>(247.0f - s * 32.0f);   // F7 → D7
        g = static_cast<uint8_t>(247.0f - s * 222.0f);  // F7 → 19
        b = static_cast<uint8_t>(247.0f - s * 219.0f);  // F7 → 1C
    }
    return {r, g, b, 180};
}

SDL_Color precipitation_color(float precip) {
    // Blue ramp: light sky blue (low) → medium blue → dark navy (heavy rain)
    float t = std::clamp(precip, 0.0f, 1.0f);
    uint8_t r, g, b;
    if (t < 0.5f) {
        // Light sky blue (#6BAED6) → medium blue (#2171B5)
        float s = t * 2.0f;
        r = static_cast<uint8_t>(107.0f - s * 74.0f);   // 6B → 21
        g = static_cast<uint8_t>(174.0f - s * 61.0f);   // AE → 71
        b = static_cast<uint8_t>(214.0f - s * 33.0f);   // D6 → B5
    } else {
        // Medium blue (#2171B5) → dark navy (#08306B)
        float s = (t - 0.5f) * 2.0f;
        r = static_cast<uint8_t>(33.0f - s * 25.0f);    // 21 → 08
        g = static_cast<uint8_t>(113.0f - s * 65.0f);   // 71 → 30
        b = static_cast<uint8_t>(181.0f - s * 74.0f);   // B5 → 6B
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
    // Blue-tinted white coverage: transparent → soft blue-white → bright white
    float t = std::clamp(snow * 3.0f, 0.0f, 1.0f);  // saturates at snow=0.33
    if (t < 0.01f)
        return {0, 0, 0, 0};
    // Higher base alpha for better visibility; blue tint fades to pure white
    auto r = static_cast<uint8_t>(220.0f + t * 30.0f);   // 220 → 250
    auto g = static_cast<uint8_t>(230.0f + t * 18.0f);   // 230 → 248
    auto b = static_cast<uint8_t>(245.0f + t * 10.0f);   // 245 → 255
    auto a = static_cast<uint8_t>(60.0f + t * 180.0f);   // 60 → 240
    return {r, g, b, a};
}

SDL_Color precip_budget_color(float budget) {
    // Brown (depleted, 0) → green (full, 1) → white (excess, 2)
    float t = std::clamp(budget / 2.0f, 0.0f, 1.0f);  // normalize to [0,1]
    uint8_t r, g, b;
    if (t < 0.5f) {
        // Brown → green
        float s = t * 2.0f;
        r = static_cast<uint8_t>(140.0f * (1.0f - s) + 30.0f * s);
        g = static_cast<uint8_t>(100.0f * (1.0f - s) + 160.0f * s);
        b = static_cast<uint8_t>(40.0f * (1.0f - s) + 40.0f * s);
    } else {
        // Green → white
        float s = (t - 0.5f) * 2.0f;
        r = static_cast<uint8_t>(30.0f + s * 225.0f);
        g = static_cast<uint8_t>(160.0f + s * 95.0f);
        b = static_cast<uint8_t>(40.0f + s * 215.0f);
    }
    return {r, g, b, 180};
}

SDL_Color stability_color(float stab) {
    // Diverging: blue (#3B4CC0, unstable, -20) → near-white (#F7F7F7, neutral, 0)
    //          → red (#B40426, stable/inversion, +10)
    // Gamma 0.8 expands contrast in the common range
    float raw = std::clamp((stab + 20.0f) / 30.0f, 0.0f, 1.0f);  // 0=unstable, 1=inversion
    float t = std::pow(raw, 0.8f);  // gamma for better contrast
    uint8_t r, g, b;
    if (t < 0.5f) {
        // Blue (#3B4CC0) → near-white (#F7F7F7)
        float s = t * 2.0f;
        r = static_cast<uint8_t>(59.0f + s * 188.0f);   // 3B → F7
        g = static_cast<uint8_t>(76.0f + s * 171.0f);   // 4C → F7
        b = static_cast<uint8_t>(192.0f + s * 55.0f);   // C0 → F7
    } else {
        // Near-white (#F7F7F7) → red (#B40426)
        float s = (t - 0.5f) * 2.0f;
        r = static_cast<uint8_t>(247.0f - s * 67.0f);   // F7 → B4
        g = static_cast<uint8_t>(247.0f - s * 243.0f);  // F7 → 04
        b = static_cast<uint8_t>(247.0f - s * 209.0f);  // F7 → 26
    }
    return {r, g, b, 180};
}

SDL_Color aridity_color(float aridity) {
    // Colorblind-safe viridis-like: dark purple (desert) → blue → teal → yellow (humid)
    float t = std::clamp(aridity / 1.5f, 0.0f, 1.0f);  // 0=arid, 1=very humid
    uint8_t r, g, b;
    if (t < 0.25f) {
        // Dark purple (#2D1E4F) → blue (#2C7BB6)
        float s = t / 0.25f;
        r = static_cast<uint8_t>(45.0f + s * -1.0f);    // 2D → 2C
        g = static_cast<uint8_t>(30.0f + s * 93.0f);    // 1E → 7B
        b = static_cast<uint8_t>(79.0f + s * 103.0f);   // 4F → B6
    } else if (t < 0.5f) {
        // Blue (#2C7BB6) → teal (#2DB87B)
        float s = (t - 0.25f) / 0.25f;
        r = static_cast<uint8_t>(44.0f + s * 1.0f);     // 2C → 2D
        g = static_cast<uint8_t>(123.0f + s * 61.0f);   // 7B → B8
        b = static_cast<uint8_t>(182.0f - s * 59.0f);   // B6 → 7B
    } else if (t < 0.75f) {
        // Teal (#2DB87B) → light green (#7FC97F)
        float s = (t - 0.5f) / 0.25f;
        r = static_cast<uint8_t>(45.0f + s * 82.0f);    // 2D → 7F
        g = static_cast<uint8_t>(184.0f + s * 17.0f);   // B8 → C9
        b = static_cast<uint8_t>(123.0f + s * 4.0f);    // 7B → 7F
    } else {
        // Light green (#7FC97F) → warm yellow (#FEE08B)
        float s = (t - 0.75f) / 0.25f;
        r = static_cast<uint8_t>(127.0f + s * 127.0f);  // 7F → FE
        g = static_cast<uint8_t>(201.0f + s * 23.0f);   // C9 → E0
        b = static_cast<uint8_t>(127.0f + s * 12.0f);   // 7F → 8B
    }
    return {r, g, b, 180};
}

SDL_Color groundwater_color(float gw) {
    // Tan (dry) → cyan → dark blue (saturated)
    float t = std::clamp(gw / 1.0f, 0.0f, 1.0f);  // 0=dry, 1=1m depth
    uint8_t r, g, b;
    if (t < 0.3f) {
        float s = t / 0.3f;
        r = static_cast<uint8_t>(180.0f * (1.0f - s) + 40.0f * s);
        g = static_cast<uint8_t>(160.0f * (1.0f - s) + 180.0f * s);
        b = static_cast<uint8_t>(110.0f * (1.0f - s) + 200.0f * s);
    } else {
        float s = (t - 0.3f) / 0.7f;
        r = static_cast<uint8_t>(40.0f * (1.0f - s));
        g = static_cast<uint8_t>(180.0f * (1.0f - s * 0.6f));
        b = static_cast<uint8_t>(200.0f + s * 55.0f);
    }
    return {r, g, b, 180};
}

SDL_Color discharge_color(float discharge) {
    // Sqrt-scaled teal→white ramp: visually distinct from terrain/water
    if (discharge < 0.0001f)
        return {0, 0, 0, 0};  // transparent = no flow
    float t = std::clamp(std::sqrt(discharge / 0.05f), 0.0f, 1.0f);
    uint8_t r, g, b;
    if (t < 0.5f) {
        // Dark teal (#083D5B) → teal (#2AA198)
        float s = t * 2.0f;
        r = static_cast<uint8_t>(8.0f + s * 34.0f);     // 08 → 2A
        g = static_cast<uint8_t>(61.0f + s * 100.0f);   // 3D → A1
        b = static_cast<uint8_t>(91.0f + s * 61.0f);    // 5B → 98
    } else {
        // Teal (#2AA198) → pale (#F2F7FF)
        float s = (t - 0.5f) * 2.0f;
        r = static_cast<uint8_t>(42.0f + s * 200.0f);   // 2A → F2
        g = static_cast<uint8_t>(161.0f + s * 86.0f);   // A1 → F7
        b = static_cast<uint8_t>(152.0f + s * 103.0f);  // 98 → FF
    }
    return {r, g, b, static_cast<uint8_t>(160 + static_cast<int>(t * 95.0f))};
}

SDL_Color geology_color(RockType rock) {
    switch (rock) {
        case RockType::Granite:
            return {180, 130, 130, 200};  // pinkish-grey
        case RockType::Basalt:
            return {60, 60, 70, 200};  // dark grey
        case RockType::Limestone:
            return {210, 200, 170, 200};  // cream
        case RockType::Sandstone:
            return {210, 170, 100, 200};  // tan
        case RockType::Shale:
            return {100, 100, 110, 200};  // dark slate
        case RockType::Metamorphic:
            return {140, 120, 160, 200};  // purple-grey
        default:
            return {128, 128, 128, 200};
    }
}

SDL_Color soil_texture_color(SoilTexture soil) {
    switch (soil) {
        case SoilTexture::Sand:
            return {230, 210, 150, 200};  // sandy yellow
        case SoilTexture::Loam:
            return {140, 120, 80, 200};  // brown
        case SoilTexture::Silt:
            return {170, 160, 130, 200};  // light brown
        case SoilTexture::Clay:
            return {160, 100, 70, 200};  // reddish-brown
        case SoilTexture::Peat:
            return {60, 50, 30, 200};  // dark brown
        default:
            return {128, 128, 128, 200};
    }
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
    // , (comma)
    {0x00,0x00,0x00,0x00,0x00,0x04,0x08},
    // / (forward slash)
    {0x01,0x02,0x02,0x04,0x04,0x08,0x10},
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
    if (ch == ',')
        return 47;
    if (ch == '/')
        return 48;
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

static SDL_Color temp_gradient_sample(float t) {
    float temp_c = -20.0f + t * 60.0f;
    return temperature_color(temp_c);
}

static SDL_Color stability_gradient_sample(float t) {
    float stab = -20.0f + t * 30.0f;
    return stability_color(stab);
}

// ── Legend panel helper ─────────────────────────────────────────────────────

static void draw_panel_bg(SDL_Renderer* renderer, int x, int y, int w, int h) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 11, 16, 28, 255);  // fully opaque bg
    SDL_Rect bg = {x, y, w, h};
    SDL_RenderFillRect(renderer, &bg);
    SDL_SetRenderDrawColor(renderer, 42, 53, 66, 255);  // fully opaque border matching Theme
    SDL_RenderDrawRect(renderer, &bg);
}

// ── Overlay info descriptions ───────────────────────────────────────────────

struct OverlayInfo {
    const char* lines[8];
    int count;
};

static OverlayInfo get_overlay_info(OverlayMode overlay) {
    switch (overlay) {
        case OverlayMode::Temperature:
            return {{"Solar angle sets base by latitude",
                     "Lapse rate cools with altitude",
                     "QG eddies create weather fronts",
                     "Ocean moderates coastal temps",
                     "Drives: evaporation, snowmelt,",
                     "stability, aridity index"}, 6};
        case OverlayMode::Precipitation:
            return {{"Clouds condense when q > q_sat",
                     "Rain when cloud > thickness threshold",
                     "Orographic uplift on mountains",
                     "Wind carries moisture from ocean",
                     "Drives: surface water, soil,",
                     "snow depth, discharge"}, 6};
        case OverlayMode::Moisture:
            return {{"Ocean evaporation is main source",
                     "Land evap depends on soil water",
                     "Wind advects moisture inland",
                     "Condensation removes humidity",
                     "Drives: cloud formation, precip,",
                     "latent heat release"}, 6};
        case OverlayMode::WindDirection:
            return {{"QG streamfunction sets upper flow",
                     "Surface: pressure gradient + Coriolis",
                     "Trade / westerly / polar cells",
                     "Terrain roughness increases drag",
                     "Drives: moisture transport,",
                     "evaporation rate"}, 6};
        case OverlayMode::Evaporation:
            return {{"Ocean: temp-driven, cloud-limited",
                     "Land: quadratic soil dependence",
                     "Blocked below wilting point",
                     "Removes surface and soil water",
                     "Adds atmospheric humidity"}, 5};
        case OverlayMode::Storminess:
            return {{"Convergence x instability x humidity",
                     "High where weather fronts collide",
                     "Warm ocean enhances instability",
                     "Subsidence and high-P suppress",
                     "Drives: intense precipitation,",
                     "wind gusts"}, 6};
        case OverlayMode::MoistureBars:
            return {{"Bar height = column humidity",
                     "Taller bars = wetter air mass",
                     "Shows spatial moisture pattern",
                     "Quick visual for dry/wet zones"}, 4};
        case OverlayMode::RainShadow:
            return {{"Mountains force windward uplift",
                     "Leeward air descends and dries",
                     "Creates persistent arid zones",
                     "Depends on wind dir + mtn height",
                     "Interacts: precipitation, aridity"}, 5};
        case OverlayMode::SurfaceWater:
            return {{"Rain and snowmelt fill surface",
                     "Drains by infiltration + runoff",
                     "Evaporates from open surface",
                     "Pools in flat / low-slope terrain",
                     "Feeds: soil moisture, discharge,",
                     "basin lakes via spillway storage"}, 6};
        case OverlayMode::SoilMoisture:
            return {{"Infiltrated from surface water",
                     "Rate set by geology (ksat)",
                     "Capped at field capacity",
                     "Evaporation and plants pull water",
                     "Drives: land evaporation rate,",
                     "groundwater recharge"}, 6};
        case OverlayMode::SnowDepth:
            return {{"Precip falls as snow when T < 0 C",
                     "Melts above freezing -> water",
                     "Meltwater adds to surface water",
                     "Insulates ground temperature",
                     "Interacts: temperature,",
                     "surface water, albedo"}, 6};
        case OverlayMode::PrecipBudget:
            return {{"Air mass moisture capacity",
                     "Depleted by precipitation events",
                     "Recharged slowly over ocean",
                     "Low budget = exhausted air mass",
                     "Creates inland dry spells"}, 5};
        case OverlayMode::UpperWind:
            return {{"QG streamfunction gradient",
                     "Stronger at jet stream latitudes",
                     "Steers synoptic weather systems",
                     "Thermal wind from T gradient",
                     "Interacts: surface wind patterns"}, 5};
        case OverlayMode::Stability:
            return {{"T_upper - T_lower vs lapse rate",
                     "Negative = convective (storms)",
                     "Positive = inversion (stable)",
                     "Warm surface + cold aloft = mix",
                     "Drives: convection, cloud type,",
                     "precipitation intensity"}, 6};
        case OverlayMode::Aridity:
            return {{"Ratio: precipitation / PET",
                     "Low (<0.2) = desert climate",
                     "High (>0.65) = humid climate",
                     "EMA-smoothed over time",
                     "Driven by: precip, temp, soil"}, 5};
        case OverlayMode::Groundwater:
            return {{"Deep infiltration reservoir",
                     "Filled by drainage from soil",
                     "Capped at soil_depth x porosity",
                     "Discharges as river baseflow",
                     "Controlled by: geology, precip"}, 5};
        case OverlayMode::Discharge:
            return {{"D8 flow accumulation of runoff",
                     "Quick: storm pulse (tau=0.3 day)",
                     "Base: perennial flow (tau=10 day)",
                     "Sqrt/log scaled for visibility",
                     "Driven by: runoff, groundwater"}, 5};
        case OverlayMode::Geology:
            return {{"From tectonic context at gen time",
                     "Convergent boundaries -> metamorphic",
                     "Volcanic arcs -> basalt",
                     "Stable cratons -> granite/sandstone",
                     "Controls: erosion, infiltration,",
                     "soil type, bedrock hardness"}, 6};
        case OverlayMode::SoilTextureOverlay:
            return {{"Derived from geology + weathering",
                     "Sand: fast drain, low retention",
                     "Clay: slow drain, high retention",
                     "Peat: organic, high water hold",
                     "Controls: ksat, field_capacity,",
                     "wilting_point, erodibility"}, 6};
        default:
            return {{}, 0};
    }
}

// Fixed pixel width for overlay info panel (computed once across all overlays)
static const int INFO_PANEL_TEXT_W = [] {
    int w = 0;
    for (int i = 0; i < static_cast<int>(OverlayMode::COUNT); ++i) {
        auto info = get_overlay_info(static_cast<OverlayMode>(i));
        for (int j = 0; j < info.count; ++j)
            w = std::max(w, text_pixel_width(info.lines[j], 2));
    }
    return w;
}();

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
                           int win_w, int win_h, int mouse_x, int mouse_y) {
    constexpr int SCALE = 2;
    constexpr int ROW_H = 18;
    constexpr int PAD = 8;
    constexpr int BAR_H = 14;

    char buf[80];

    // ── Stats panel (bottom-left, always shown when weather is baked) ────────
    int stats_panel_w;
    SDL_Rect stats_bounds;
    int stats_content_y;
    {
        const auto& theme = ui::Theme::instance();
        constexpr int N = 8;
        char lines[N][48];
        std::snprintf(lines[0], 48, "WEATHER STATS");
        std::snprintf(lines[1], 48, "T: %.1f TO %.1f C",
                      static_cast<double>(stats.temp_min), static_cast<double>(stats.temp_max));
        std::snprintf(lines[2], 48, "P: %.2f TO %.2f",
                      static_cast<double>(stats.precip_min), static_cast<double>(stats.precip_max));
        std::snprintf(lines[3], 48, "M: %.2f TO %.2f",
                      static_cast<double>(stats.moisture_min),
                      static_cast<double>(stats.moisture_max));
        std::snprintf(lines[4], 48, "E: %.2f TO %.2f", static_cast<double>(stats.evap_min),
                      static_cast<double>(stats.evap_max));
        std::snprintf(lines[5], 48, "WIND MAX: %.2f",
                      static_cast<double>(stats.wind_speed_max));
        std::snprintf(lines[6], 48, "STORMS: %u", stats.storm_tiles);
        std::snprintf(lines[7], 48, "SHADOWS: %u", stats.rain_shadow_tiles);

        int max_text_w = 0;
        for (int i = 0; i < N; ++i)
            max_text_w = std::max(max_text_w, text_pixel_width(lines[i], SCALE));
        int panel_pad = ui::S3;
        stats_panel_w = max_text_w + panel_pad * 2;
        int rh = ui::row_height(SCALE);
        int panel_h = panel_pad * 2 + rh * N;

        ui::Panel panel;
        panel.bounds = {ui::SAFE_L, win_h - ui::SAFE_B - panel_h, stats_panel_w, panel_h};
        panel.padding = panel_pad;
        panel.draw_bg(renderer, theme);

        SDL_Rect prev_clip = panel.push_clip(renderer);
        auto cr = panel.content_rect();
        stats_content_y = cr.y;
        ui::ColumnLayout col{cr.x, cr.y, cr.w, 0};
        col.text(renderer, lines[0], SCALE, theme.accent);
        for (int i = 1; i < N; ++i)
            col.text(renderer, lines[i], SCALE, theme.text);
        ui::Panel::pop_clip(renderer, prev_clip);
        stats_bounds = panel.bounds;
    }

    // Tooltip for weather stats panel
    if (mouse_x >= stats_bounds.x && mouse_x < stats_bounds.x + stats_bounds.w &&
        mouse_y >= stats_bounds.y && mouse_y < stats_bounds.y + stats_bounds.h) {
        static const char* tips[] = {
            "Static weather bake summary",                                        // 0: WEATHER STATS
            "Temperature range from baked climate|Based on latitude and lapse rate", // 1: T
            "Precipitation range from baked climate|From zonal+meridional moisture sweeps", // 2: P
            "Moisture range (0-1)|From evaporation and precipitation balance",    // 3: M
            "Evaporation demand range (0-1)|Higher at warm, low-humidity locations", // 4: E
            "Maximum wind speed from 3-cell model|Trade, westerly, and polar winds", // 5: WIND
            "Tiles with storminess > threshold|From convergence + instability",   // 6: STORMS
            "Tiles in rain shadow of mountains|Leeward of orographic barriers",   // 7: SHADOWS
        };
        int rh = ui::row_height(SCALE);
        int rel_y = mouse_y - stats_content_y;
        int hovered = rel_y / rh;
        if (hovered >= 0 && hovered < 8) {
            ui::render_tooltip(renderer, mouse_x, mouse_y, tips[hovered], win_w, win_h);
        }
    }

    // ── Overlay color key (right of stats, shown when overlay is active) ────
    if (overlay == OverlayMode::None) {
        // No overlay active — top button already shows "OVERLAY: NONE"
        return;
    }

    int key_x = PAD + stats_panel_w + PAD;
    SDL_Rect legend_rect = {0, 0, 0, 0};  // Track legend bounds for info panel

    // Helper: draw a gradient key panel (title, gradient bar, min/max labels)
    auto draw_gradient_key = [&](const char* title, ColorFn color_fn, const char* lo_label,
                                 const char* hi_label, uint8_t lo_r, uint8_t lo_g, uint8_t lo_b,
                                 uint8_t hi_r, uint8_t hi_g, uint8_t hi_b,
                                 const char* mid_label = nullptr) {
        // Bar must be wide enough for title and for lo+hi labels side by side
        int title_w = text_pixel_width(title, SCALE);
        int labels_w = text_pixel_width(lo_label, SCALE) + text_pixel_width(hi_label, SCALE) + 24;
        int bar_w = std::max(180, std::max(title_w, labels_w));
        int key_w = bar_w + PAD * 2;
        int key_rows = 3;
        int key_h = PAD * 2 + ROW_H * key_rows + BAR_H;
        int key_y = win_h - key_h - PAD;

        legend_rect = {key_x, key_y, key_w, key_h};
        draw_panel_bg(renderer, key_x, key_y, key_w, key_h);
        int y = key_y + PAD;
        draw_text(renderer, key_x + PAD, y + 2, title, SCALE, 220, 220, 220);
        y += ROW_H;

        render_gradient_bar(renderer, key_x + PAD, y, bar_w, BAR_H, color_fn);
        y += BAR_H + 8;  // larger gap between ramp and tick labels

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
            draw_gradient_key("PRECIPITATION (MM/DAY)", precipitation_color, "0", "1.0", 107, 174,
                              214, 8, 48, 107, "0.5");
            break;

        case OverlayMode::Moisture:
            draw_gradient_key("HUMIDITY (0-1)", moisture_color, "0", "1.0", 150, 100, 50, 20, 180,
                              40, "0.5");
            break;

        case OverlayMode::WindDirection: {
            const char* lines[] = {"PREVAILING WINDS", "-->  = wind direction",
                                   "0-30: Trade easterlies", "30-60: Westerlies",
                                   "60-90: Polar easterlies"};
            int key_w = panel_width_for({lines[0], lines[1], lines[2], lines[4]}, SCALE, PAD * 2);
            int key_rows = 7;
            int key_h = PAD * 2 + ROW_H * key_rows;
            int key_y = win_h - key_h - PAD;

            legend_rect = {key_x, key_y, key_w, key_h};
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
            draw_gradient_key("EVAPORATION (0-1)", evaporation_color, "0", "1.0", 220, 200, 30,
                              255, 60, 0, "0.5");
            break;

        case OverlayMode::Storminess:
            draw_gradient_key("STORMINESS", storminess_color, "Calm", "Stormy", 120, 120, 130, 160,
                              40, 200);
            break;

        case OverlayMode::MoistureBars: {
            const char* l1 = "MOISTURE BARS";
            const char* l2 = "Taller = wetter";
            int key_w = panel_width_for({l1, l2}, SCALE, PAD * 2);
            int key_rows = 3;
            int key_h = PAD * 2 + ROW_H * key_rows;
            int key_y = win_h - key_h - PAD;

            legend_rect = {key_x, key_y, key_w, key_h};
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

        case OverlayMode::PrecipBudget: {
            auto budget_grad = [](float t) -> SDL_Color { return precip_budget_color(t * 2.0f); };
            draw_gradient_key("PRECIP BUDGET", budget_grad, "Depleted", "Full", 140, 100, 40, 255,
                              255, 255, "1.0");
            break;
        }

        case OverlayMode::RainShadow: {
            const char* l1 = "RAIN SHADOW";
            std::snprintf(buf, sizeof(buf), "%u leeward tiles", stats.rain_shadow_tiles);
            int key_w = panel_width_for({l1, buf, "Leeward of mountains"}, SCALE, PAD * 2);
            int key_rows = 4;
            int key_h = PAD * 2 + ROW_H * key_rows;
            int key_y = win_h - key_h - PAD;

            legend_rect = {key_x, key_y, key_w, key_h};
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

        case OverlayMode::Aridity: {
            // Extended legend with direction labels
            int bar_w = std::max(180, panel_width_for({"ARIDITY INDEX (P/PET)"}, SCALE, PAD) - PAD);
            int key_w = bar_w + PAD * 2;
            int key_rows = 4;  // title + bar + ticks + direction labels
            int key_h = PAD * 2 + ROW_H * key_rows + BAR_H;
            int key_y_pos = win_h - key_h - PAD;

            legend_rect = {key_x, key_y_pos, key_w, key_h};
            draw_panel_bg(renderer, key_x, key_y_pos, key_w, key_h);
            int y = key_y_pos + PAD;
            draw_text(renderer, key_x + PAD, y + 2, "ARIDITY INDEX (P/PET)", SCALE, 220, 220, 220);
            y += ROW_H;

            auto aridity_grad = [](float t) -> SDL_Color { return aridity_color(t * 1.5f); };
            render_gradient_bar(renderer, key_x + PAD, y, bar_w, BAR_H, aridity_grad);
            y += BAR_H + 8;

            // Tick labels
            draw_text(renderer, key_x + PAD, y, "0", SCALE, 180, 60, 30);
            const char* mid = "0.65";
            int mw = text_pixel_width(mid, SCALE);
            draw_text(renderer, key_x + PAD + bar_w / 2 - mw / 2, y, mid, SCALE, 220, 220, 220);
            const char* hi = "1.5+";
            int rw = text_pixel_width(hi, SCALE);
            draw_text(renderer, key_x + PAD + bar_w - rw, y, hi, SCALE, 40, 100, 220);
            y += ROW_H;

            // Direction labels
            draw_text(renderer, key_x + PAD, y, "ARID", SCALE, 140, 100, 80);
            const char* humid = "HUMID";
            int hw = text_pixel_width(humid, SCALE);
            draw_text(renderer, key_x + PAD + bar_w - hw, y, humid, SCALE, 80, 160, 120);
            break;
        }

        case OverlayMode::Groundwater: {
            draw_gradient_key("GROUNDWATER (M)", groundwater_color, "0", "1.0", 180, 160, 110,
                              40, 120, 255, "0.3");
            break;
        }

        case OverlayMode::Discharge: {
            auto discharge_grad = [](float t) -> SDL_Color { return discharge_color(t * 0.05f); };
            draw_gradient_key("DISCHARGE - SQRT SCALE", discharge_grad, "0", "0.05+", 120, 120,
                              120, 242, 247, 255, "0.01");
            break;
        }

        case OverlayMode::SurfaceWater: {
            draw_gradient_key("SURFACE WATER (M)", surface_water_color, "0", "1.0", 180, 160, 80,
                              20, 120, 220, "0.5");
            break;
        }

        case OverlayMode::SoilMoisture: {
            draw_gradient_key("SOIL MOISTURE (0-1)", soil_moisture_color, "0", "1.0", 160, 120, 60,
                              30, 140, 40, "0.5");
            break;
        }

        case OverlayMode::SnowDepth: {
            auto snow_grad = [](float t) -> SDL_Color { return snow_depth_color(t * 0.33f); };
            draw_gradient_key("SNOW DEPTH (M)", snow_grad, "0", "0.3+", 160, 160, 170, 250, 248,
                              255, "0.1");
            break;
        }

        case OverlayMode::UpperWind: {
            auto upper_wind_grad = [](float t) -> SDL_Color {
                auto r = static_cast<uint8_t>(40.0f + t * 215.0f);
                auto g = static_cast<uint8_t>(40.0f + t * 160.0f);
                auto b = static_cast<uint8_t>(80.0f + t * 175.0f);
                return {r, g, b, 160};
            };
            draw_gradient_key("UPPER WIND (CELLS/DAY)", upper_wind_grad, "0", "4.0", 80, 80, 120,
                              255, 200, 255, "2.0");
            break;
        }

        case OverlayMode::Stability: {
            draw_gradient_key("STABILITY (K)", stability_gradient_sample, "-20", "+10", 59, 76,
                              192, 180, 4, 38, "0");
            break;
        }

        case OverlayMode::Geology: {
            // Categorical legend — render colored swatches with labels
            struct CatEntry {
                SDL_Color c;
                const char* label;
            };
            CatEntry entries[] = {
                {geology_color(RockType::Granite), "Granite"},
                {geology_color(RockType::Basalt), "Basalt"},
                {geology_color(RockType::Limestone), "Limestone"},
                {geology_color(RockType::Sandstone), "Sandstone"},
                {geology_color(RockType::Shale), "Shale"},
                {geology_color(RockType::Metamorphic), "Metamorphic"},
            };
            int key_w = panel_width_for({"GEOLOGY", "Metamorphic"}, SCALE, PAD * 2 + 20);
            int n_entries = 6;
            int key_h = PAD * 2 + ROW_H * (n_entries + 1);
            int key_y = win_h - key_h - PAD;
            legend_rect = {key_x, key_y, key_w, key_h};
            draw_panel_bg(renderer, key_x, key_y, key_w, key_h);
            int y = key_y + PAD;
            draw_text(renderer, key_x + PAD, y + 2, "GEOLOGY", SCALE, 220, 220, 220);
            y += ROW_H;
            for (int i = 0; i < n_entries; ++i) {
                SDL_SetRenderDrawColor(renderer, entries[i].c.r, entries[i].c.g, entries[i].c.b,
                                       220);
                SDL_Rect swatch = {key_x + PAD, y + 2, 12, 12};
                SDL_RenderFillRect(renderer, &swatch);
                draw_text(renderer, key_x + PAD + 18, y + 2, entries[i].label, SCALE, 200, 200,
                          200);
                y += ROW_H;
            }
            break;
        }

        case OverlayMode::SoilTextureOverlay: {
            struct CatEntry {
                SDL_Color c;
                const char* label;
            };
            CatEntry entries[] = {
                {soil_texture_color(SoilTexture::Sand), "Sand"},
                {soil_texture_color(SoilTexture::Loam), "Loam"},
                {soil_texture_color(SoilTexture::Silt), "Silt"},
                {soil_texture_color(SoilTexture::Clay), "Clay"},
                {soil_texture_color(SoilTexture::Peat), "Peat"},
            };
            int key_w = panel_width_for({"SOIL TEXTURE", "Sand"}, SCALE, PAD * 2 + 20);
            int n_entries = 5;
            int key_h = PAD * 2 + ROW_H * (n_entries + 1);
            int key_y = win_h - key_h - PAD;
            legend_rect = {key_x, key_y, key_w, key_h};
            draw_panel_bg(renderer, key_x, key_y, key_w, key_h);
            int y = key_y + PAD;
            draw_text(renderer, key_x + PAD, y + 2, "SOIL TEXTURE", SCALE, 220, 220, 220);
            y += ROW_H;
            for (int i = 0; i < n_entries; ++i) {
                SDL_SetRenderDrawColor(renderer, entries[i].c.r, entries[i].c.g, entries[i].c.b,
                                       220);
                SDL_Rect swatch = {key_x + PAD, y + 2, 12, 12};
                SDL_RenderFillRect(renderer, &swatch);
                draw_text(renderer, key_x + PAD + 18, y + 2, entries[i].label, SCALE, 200, 200,
                          200);
                y += ROW_H;
            }
            break;
        }

        default:
            break;
    }

    // ── Overlay info panel (right of legend, bottom-aligned) ─────────────────
    auto info = get_overlay_info(overlay);
    if (legend_rect.w > 0 && info.count > 0) {
        const auto& theme = ui::Theme::instance();
        int info_pad = ui::S3;
        int info_rh = ui::row_height(SCALE);
        int info_content_h = info_rh * info.count;
        int info_w = INFO_PANEL_TEXT_W + info_pad * 2;
        int info_h = std::max(legend_rect.h, info_content_h + info_pad * 2);
        int info_x = legend_rect.x + legend_rect.w + PAD;
        int info_y = std::max(ui::SAFE_T, legend_rect.y + legend_rect.h - info_h);

        // Don't render if panel would extend past right edge
        if (info_x + info_w > win_w - ui::SAFE_R) return;

        ui::Panel info_panel;
        info_panel.bounds = {info_x, info_y, info_w, info_h};
        info_panel.padding = info_pad;
        info_panel.draw_bg(renderer, theme);

        SDL_Rect prev_clip = info_panel.push_clip(renderer);
        auto icr = info_panel.content_rect();
        ui::ColumnLayout col{icr.x, icr.y, icr.w, 0};

        for (int i = 0; i < info.count; ++i)
            col.text(renderer, info.lines[i], SCALE, theme.text_muted);

        ui::Panel::pop_clip(renderer, prev_clip);
    }
}

// ── Dynamic simulation legend ───────────────────────────────────────────────

void render_dynamic_legend(SDL_Renderer* renderer, const DynamicStats& stats,
                           const AtmosphereStats& atmo_stats, float elapsed_days, float time_scale,
                           bool paused, OverlayMode /*overlay*/, int win_w, int win_h,
                           float time_of_day, float day_of_year, int mouse_x, int mouse_y) {
    constexpr int SCALE = 2;
    const auto& theme = ui::Theme::instance();

    // ── Pre-format all lines to determine panel width ─────────────────────
    // Like CSS width:auto — panel shrink-wraps to widest content line.
    char lines[22][80];
    int line_count = 0;

    // Section 1: Dynamics
    std::snprintf(lines[line_count++], 80, "DYNAMICS");
    int day = static_cast<int>(elapsed_days);
    if (paused)
        std::snprintf(lines[line_count++], 80, "DAY %d  PAUSED", day);
    else
        std::snprintf(lines[line_count++], 80, "DAY %d  %.1fx", day, static_cast<double>(time_scale));
    const char* seasons[] = {"SPRING", "SUMMER", "AUTUMN", "WINTER"};
    int season_idx = static_cast<int>((day_of_year + 10.0f) / 91.25f) % 4;
    int hour = static_cast<int>(time_of_day);
    int minute = static_cast<int>((time_of_day - static_cast<float>(hour)) * 60.0f);
    std::snprintf(lines[line_count++], 80, "%s %02d:%02d", seasons[season_idx], hour, minute);

    // Section 2: Ground state
    std::snprintf(lines[line_count++], 80, "SW: %.4f  MAX: %.4f",
                  static_cast<double>(stats.surface_water_mean),
                  static_cast<double>(stats.surface_water_max));
    std::snprintf(lines[line_count++], 80, "SOIL: %.3f",
                  static_cast<double>(stats.soil_moisture_mean));
    std::snprintf(lines[line_count++], 80, "SNOW: %u  FLOODED: %u", stats.snow_tiles,
                  stats.flooded_tiles);
    std::snprintf(lines[line_count++], 80, "ARIDITY: %.2f  A:%u H:%u",
                  static_cast<double>(stats.aridity_mean), stats.arid_tiles, stats.humid_tiles);
    std::snprintf(lines[line_count++], 80, "GW: %.4f  DISCH: %.3f",
                  static_cast<double>(stats.groundwater_mean),
                  static_cast<double>(stats.discharge_max));

    // Section 3: Atmosphere
    std::snprintf(lines[line_count++], 80, "ATMOSPHERE");
    std::snprintf(lines[line_count++], 80, "T: %.0f / %.0f / %.0f",
                  static_cast<double>(atmo_stats.T_min), static_cast<double>(atmo_stats.T_mean),
                  static_cast<double>(atmo_stats.T_max));
    std::snprintf(lines[line_count++], 80, "Q: %.3f  CLOUD: %.3f",
                  static_cast<double>(atmo_stats.q_mean),
                  static_cast<double>(atmo_stats.cloud_mean));
    std::snprintf(lines[line_count++], 80, "PRECIP: %.3f MAX %.3f",
                  static_cast<double>(atmo_stats.precip_mean),
                  static_cast<double>(atmo_stats.precip_max));
    std::snprintf(lines[line_count++], 80, "RAIN %u  CLOUDY %u", atmo_stats.raining_cells,
                  atmo_stats.cloudy_cells);
    std::snprintf(lines[line_count++], 80, "WIND: %.2f MAX %.2f",
                  static_cast<double>(atmo_stats.wind_mean),
                  static_cast<double>(atmo_stats.wind_max));
    std::snprintf(lines[line_count++], 80, "UPPER: T=%.0f W=%.1f S=%.0f",
                  static_cast<double>(atmo_stats.T_upper_mean),
                  static_cast<double>(atmo_stats.wind_upper_mean),
                  static_cast<double>(atmo_stats.stability_mean));

    // Section 4: Invariants
    std::snprintf(lines[line_count++], 80, "INVARIANTS");
    std::snprintf(lines[line_count++], 80, "H2O: %.1f CFL: %.2f",
                  static_cast<double>(atmo_stats.total_atmo_water),
                  static_cast<double>(atmo_stats.max_courant));
    std::snprintf(lines[line_count++], 80, "BUDGET: %.2f-%.2f (%.2f)",
                  static_cast<double>(atmo_stats.budget_min),
                  static_cast<double>(atmo_stats.budget_max),
                  static_cast<double>(atmo_stats.budget_mean));
    std::snprintf(lines[line_count++], 80, "ENERGY DRIFT: %.2f%%",
                  static_cast<double>(atmo_stats.energy_drift_pct));

    // ── Fixed panel width from worst-case content (CSS min-width) ──────────
    // Use template strings with maximum-length values so the panel never resizes.
    static const int fixed_text_w = [] {
        const char* templates[] = {
            "ARIDITY: 0.00  A:99999 H:99999",
            "SW: 0.0000  MAX: 00.0000",
            "SNOW: 99999  FLOODED: 9999",
            "ENERGY DRIFT: -000.00%",
            "BUDGET: 0.00-0.00 (0.00)",
            "PRECIP: 0.000 MAX 0.000",
            "UPPER: T=-00 W=00.0 S=-00",
            "H2O: 0000.0 CFL: 00.00",
            "RAIN 9999  CLOUDY 9999",
        };
        int w = 0;
        for (auto* t : templates)
            w = std::max(w, text_pixel_width(t, 2));
        return w;
    }();

    int panel_pad = ui::S3;  // 12px inner padding
    int panel_w = fixed_text_w + panel_pad * 2;
    int row_h = ui::row_height(SCALE);
    int panel_h = panel_pad * 2 + row_h * line_count + ui::separator_height() * 3;
    int panel_x = win_w - panel_w - ui::SAFE_R;
    int panel_y = 10;  // align top with button row (BTN_MARGIN)

    // ── Draw panel using UI system ────────────────────────────────────────
    ui::Panel panel;
    panel.bounds = {panel_x, panel_y, panel_w, panel_h};
    panel.padding = panel_pad;
    panel.draw_bg(renderer, theme);

    // Push clip rect (CSS overflow:hidden)
    SDL_Rect prev_clip = panel.push_clip(renderer);

    // ── Layout content using ColumnLayout ─────────────────────────────────
    auto cr = panel.content_rect();
    ui::ColumnLayout col{cr.x, cr.y, cr.w, 0};

    // Color shortcuts
    SDL_Color hdr = theme.accent;
    SDL_Color val = theme.text;
    SDL_Color lbl = theme.text_muted;
    SDL_Color hi = theme.info;

    // Section 1: Dynamics
    col.text(renderer, lines[0], SCALE, hdr);
    col.text(renderer, lines[1], SCALE, val);
    col.text(renderer, lines[2], SCALE, lbl);
    col.separator(renderer, ui::S2);

    // Section 2: Ground state
    col.text(renderer, lines[3], SCALE, hi);
    col.text(renderer, lines[4], SCALE, val);
    col.text(renderer, lines[5], SCALE, val);
    col.text(renderer, lines[6], SCALE, lbl);
    col.text(renderer, lines[7], SCALE, hi);
    col.separator(renderer, ui::S2);

    // Section 3: Atmosphere
    col.text(renderer, lines[8], SCALE, hdr);
    col.text(renderer, lines[9], SCALE, val);
    col.text(renderer, lines[10], SCALE, val);
    col.text(renderer, lines[11], SCALE, hi);
    col.text(renderer, lines[12], SCALE, lbl);
    col.text(renderer, lines[13], SCALE, val);
    col.text(renderer, lines[14], SCALE, lbl);
    col.separator(renderer, ui::S2);

    // Section 4: Invariants
    col.text(renderer, lines[15], SCALE, hdr);

    // CFL warning color
    SDL_Color cfl_c = atmo_stats.max_courant > 1.0f ? theme.danger : val;
    col.text(renderer, lines[16], SCALE, cfl_c);
    col.text(renderer, lines[17], SCALE, lbl);

    SDL_Color energy_c = std::abs(atmo_stats.energy_drift_pct) > 5.0f ? theme.danger : val;
    col.text(renderer, lines[18], SCALE, energy_c);

    // Pop clip rect
    ui::Panel::pop_clip(renderer, prev_clip);

    // ── Tooltip on hover ──────────────────────────────────────────────────
    if (mouse_x >= panel.bounds.x && mouse_x < panel.bounds.x + panel.bounds.w &&
        mouse_y >= panel.bounds.y && mouse_y < panel.bounds.y + panel.bounds.h) {
        // Tooltip descriptions per line index
        static const char* tips[] = {
            "Dynamic simulation status",                                          // 0: DYNAMICS
            "Simulation day and speed multiplier",                                // 1: DAY N  Nx
            "Current season and time of day",                                     // 2: SEASON HH:MM
            "Mean and max surface water depth (m)|Standing water on terrain",     // 3: SW
            "Mean soil moisture (0-1 of field capacity)|Water held in soil",      // 4: SOIL
            "Tiles with snow cover / standing water",                             // 5: SNOW FLOODED
            "Aridity index (precip/evap)|A=arid tiles  H=humid tiles",           // 6: ARIDITY
            "Mean groundwater depth / max discharge|Subsurface water + river flow", // 7: GW DISCH
            "Atmosphere simulation state",                                        // 8: ATMOSPHERE
            "Temperature min / mean / max (Celsius)",                             // 9: T
            "Mean specific humidity and cloud cover",                             // 10: Q CLOUD
            "Mean and max precipitation rate (mm/day)",                           // 11: PRECIP
            "Count of raining and cloudy atmo cells",                             // 12: RAIN CLOUDY
            "Mean and max wind speed (cells/day)",                                // 13: WIND
            "Upper troposphere: temp, wind, stability|Negative stability = convective",  // 14: UPPER
            "Simulation health diagnostics",                                      // 15: INVARIANTS
            "Total atmospheric water / Courant number|CFL > 1.0 = advection instability", // 16: H2O CFL
            "Precipitation budget range and mean|Tracks air mass moisture capacity",      // 17: BUDGET
            "Energy drift from initial state|Seasonal oscillation is normal",     // 18: ENERGY
        };

        int content_y = cr.y;
        int rel_y = mouse_y - content_y;

        // Account for separators: after row 2 (idx 3), after row 7 (idx 8), after row 14 (idx 15)
        // Build a Y-offset table
        int y_offset = 0;
        int hovered_line = -1;
        for (int i = 0; i < line_count; ++i) {
            if (i == 3 || i == 8 || i == 15) y_offset += ui::separator_height();
            int row_top = i * row_h + y_offset;
            if (rel_y >= row_top && rel_y < row_top + row_h) {
                hovered_line = i;
                break;
            }
        }

        if (hovered_line >= 0 && hovered_line < static_cast<int>(sizeof(tips) / sizeof(tips[0]))) {
            ui::render_tooltip(renderer, mouse_x, mouse_y, tips[hovered_line], win_w, win_h);
        }
    }
}

// ── Tile inspector ──────────────────────────────────────────────────────────

void render_tile_inspector(SDL_Renderer* renderer, int tile_x, int tile_y, int mouse_x,
                           int mouse_y, const Terrain& terrain, const DynamicState& dynamics,
                           const AtmosphereState& atmosphere, int win_w, int win_h) {
    if (tile_x < 0 || tile_y < 0 || tile_x >= static_cast<int>(terrain.width) ||
        tile_y >= static_cast<int>(terrain.height))
        return;

    constexpr int SCALE = 1;
    constexpr int MAX_LINES = 28;
    const auto& theme = ui::Theme::instance();

    auto ux = static_cast<uint32_t>(tile_x);
    auto uy = static_cast<uint32_t>(tile_y);
    const auto& tt = terrain.tile_at(ux, uy);

    char lines[MAX_LINES][52];
    SDL_Color colors[MAX_LINES];
    int n = 0;

    auto hdr = [&](const char* text) {
        std::snprintf(lines[n], 52, "%s", text);
        colors[n] = theme.accent;
        ++n;
    };
    // Helper macro — avoids -Wformat-nonliteral on variadic lambda
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
    auto val = [&](const char* fmt, auto... args) {
        std::snprintf(lines[n], 52, fmt, args...);
        colors[n] = theme.text;
        ++n;
    };
#pragma GCC diagnostic pop
    auto sep = [&]() {
        lines[n][0] = '\0';
        colors[n] = {0, 0, 0, 0};  // sentinel for separator
        ++n;
    };

    // Header
    std::snprintf(lines[n], 52, "TILE (%d, %d)", tile_x, tile_y);
    colors[n] = theme.accent;
    ++n;

    // Terrain
    val("Elev: %.3f  %s", static_cast<double>(tt.elev01), elevband_name(tt.band));
    val("Slope: %.3f  Aspect: %.0f", static_cast<double>(tt.slope01),
        static_cast<double>(tt.aspect * 57.2958f));
    val("Ocean: %s  Dist: %.1f", tt.is_ocean ? "Y" : "N", static_cast<double>(tt.dist_ocean));
    val("Roughness: %.3f", static_cast<double>(tt.roughness));

    sep();
    hdr("GEOLOGY");
    val("%s  Hard: %.2f", rock_type_name(tt.rock), static_cast<double>(tt.bedrock_hardness));
    val("%s  Depth: %.2f", soil_texture_name(tt.soil), static_cast<double>(tt.soil_depth));
    val("Perm: %.2f  Ksat: %.3f", static_cast<double>(tt.bedrock_permeability),
        static_cast<double>(tt.ksat));
    val("FC: %.2f  WP: %.2f", static_cast<double>(tt.field_capacity),
        static_cast<double>(tt.wilting_point));

    // Dynamic state (if available)
    bool has_dyn = !dynamics.tiles.empty() && ux < terrain.width && uy < terrain.height;
    if (has_dyn) {
        const auto& dt = dynamics.tile_at(ux, uy);
        sep();
        hdr("DYNAMICS");
        val("SW: %.4f  Soil: %.3f", static_cast<double>(dt.surface_water),
            static_cast<double>(dt.soil_moisture));
        val("Snow: %.3f  GW: %.4f", static_cast<double>(dt.snow_depth),
            static_cast<double>(dt.groundwater));
        val("Disch: %.4f  Arid: %.2f", static_cast<double>(dt.discharge),
            static_cast<double>(dt.aridity));
        val("Precip: %.3f  Evap: %.3f", static_cast<double>(dt.effective_precip),
            static_cast<double>(dt.effective_evap));
        val("PET: %.3f  Storm: %.3f", static_cast<double>(dt.pet),
            static_cast<double>(dt.effective_storm));
    }

    // Atmosphere (sampled to tile coords)
    if (!atmosphere.cells.empty()) {
        float T = atmosphere.sample(ux, uy, &AtmosphereCell::T);
        float q = atmosphere.sample(ux, uy, &AtmosphereCell::q);
        float u = atmosphere.sample(ux, uy, &AtmosphereCell::u);
        float v = atmosphere.sample(ux, uy, &AtmosphereCell::v);
        float cloud = atmosphere.sample(ux, uy, &AtmosphereCell::cloud);
        float precip = atmosphere.sample_nearest(ux, uy, &AtmosphereCell::precip_rate);
        float p = atmosphere.sample(ux, uy, &AtmosphereCell::p);
        float stab = atmosphere.sample(ux, uy, &AtmosphereCell::stability);

        sep();
        hdr("ATMOSPHERE");
        val("T: %.1f C  P: %.3f", static_cast<double>(T), static_cast<double>(p));
        val("Q: %.4f  Cloud: %.3f", static_cast<double>(q), static_cast<double>(cloud));
        val("Precip: %.3f", static_cast<double>(precip));
        float wind_spd = std::sqrt(u * u + v * v);
        val("Wind: %.2f %s", static_cast<double>(wind_spd), wind_direction_name(u, v));
        val("Stability: %.1f", static_cast<double>(stab));
    }

    // ── Compute panel size ─────────────────────────────────────────────────
    int rh = ui::row_height(SCALE);
    int sep_h = ui::separator_height();
    int panel_pad = ui::S2;

    int content_h = 0;
    int max_text_w = 0;
    for (int i = 0; i < n; ++i) {
        if (lines[i][0] == '\0' && colors[i].a == 0) {
            content_h += sep_h;
        } else {
            content_h += rh;
            max_text_w = std::max(max_text_w, text_pixel_width(lines[i], SCALE));
        }
    }

    int panel_w = max_text_w + panel_pad * 2;
    int panel_h = content_h + panel_pad * 2;

    // ── Position: near cursor, avoid obscuring other panels ────────────
    // Place to the right and below cursor by default; flip if near edges
    constexpr int CURSOR_OFFSET = 16;
    int px = mouse_x + CURSOR_OFFSET;
    int py = mouse_y + CURSOR_OFFSET;

    if (px + panel_w > win_w - ui::SAFE_R) px = mouse_x - panel_w - CURSOR_OFFSET;
    if (py + panel_h > win_h - ui::SAFE_B) py = mouse_y - panel_h - CURSOR_OFFSET;
    if (px < ui::SAFE_L) px = ui::SAFE_L;
    if (py < ui::SAFE_T) py = ui::SAFE_T;

    // ── Draw ───────────────────────────────────────────────────────────
    ui::Panel panel;
    panel.bounds = {px, py, panel_w, panel_h};
    panel.padding = panel_pad;
    panel.draw_bg(renderer, theme);

    SDL_Rect prev_clip = panel.push_clip(renderer);
    auto cr = panel.content_rect();
    ui::ColumnLayout col{cr.x, cr.y, cr.w, 0};

    for (int i = 0; i < n; ++i) {
        if (lines[i][0] == '\0' && colors[i].a == 0) {
            col.separator(renderer, ui::S1);
        } else {
            col.text(renderer, lines[i], SCALE, colors[i]);
        }
    }

    ui::Panel::pop_clip(renderer, prev_clip);
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
