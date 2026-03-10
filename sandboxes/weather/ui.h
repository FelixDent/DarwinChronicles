#pragma once

#include <SDL.h>

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <string>

namespace sandbox {

// Forward-declare from telemetry.h (sandbox namespace)
void draw_text(SDL_Renderer* renderer, int x, int y, const char* text, int scale, uint8_t r,
               uint8_t g, uint8_t b);
int text_pixel_width(const char* text, int scale);

namespace ui {

// ── Spacing scale (multiples of 4) ─────────────────────────────────────────
// Use these instead of ad-hoc pixel values. Like CSS spacing tokens.

constexpr int S0 = 0;
constexpr int S1 = 4;
constexpr int S2 = 8;
constexpr int S3 = 12;
constexpr int S4 = 16;
constexpr int S5 = 24;

// ── Safe area ──────────────────────────────────────────────────────────────
// Minimum distance from screen edges for any UI element (like CSS safe-area-inset).

constexpr int SAFE_L = 8;
constexpr int SAFE_T = 8;
constexpr int SAFE_R = 8;
constexpr int SAFE_B = 8;

// ── Theme colors ───────────────────────────────────────────────────────────
// Centralized so every panel/text uses the same palette.

struct Theme {
    SDL_Color panel_bg = {11, 16, 28, 255};
    SDL_Color panel_border = {42, 53, 66, 255};
    SDL_Color panel_shadow = {0, 0, 0, 0};  // disabled — opaque panels don't need shadow

    SDL_Color text = {215, 220, 227, 255};
    SDL_Color text_muted = {154, 166, 178, 255};
    SDL_Color text_bright = {242, 245, 248, 255};

    SDL_Color accent = {231, 184, 76, 255};  // section headers (gold)
    SDL_Color info = {110, 195, 240, 255};   // water/highlight (blue)
    SDL_Color danger = {255, 80, 80, 255};   // warnings (red)
    SDL_Color ok = {80, 220, 130, 255};      // positive (green)

    static const Theme& instance() {
        static Theme t;
        return t;
    }
};

// ── Anchoring ──────────────────────────────────────────────────────────────
// Position a rectangle relative to a parent area (like CSS position: absolute + insets).

enum class Anchor { TopLeft, TopRight, BottomLeft, BottomRight };

struct Rect {
    int x, y, w, h;
};

// Compute position for a box of size (w,h) anchored within parent area.
// offset_x/offset_y are from the anchor corner, inward.
inline Rect anchor_rect(int parent_w, int parent_h, Anchor anchor, int w, int h, int offset_x = 0,
                        int offset_y = 0) {
    switch (anchor) {
        case Anchor::TopLeft:
            return {SAFE_L + offset_x, SAFE_T + offset_y, w, h};
        case Anchor::TopRight:
            return {parent_w - SAFE_R - w - offset_x, SAFE_T + offset_y, w, h};
        case Anchor::BottomLeft:
            return {SAFE_L + offset_x, parent_h - SAFE_B - h - offset_y, w, h};
        case Anchor::BottomRight:
            return {parent_w - SAFE_R - w - offset_x, parent_h - SAFE_B - h - offset_y, w, h};
    }
    return {0, 0, w, h};
}

// ── Panel drawing ──────────────────────────────────────────────────────────
// Consistent panel appearance: bg + border + shadow + clip rect.
// Like a CSS box with background, border, box-shadow, overflow:hidden.

struct Panel {
    SDL_Rect bounds;   // outer bounds (including border)
    int padding = S2;  // inner padding (like CSS padding)

    // Content area (bounds inset by padding+border) — for positioning children
    SDL_Rect content_rect() const {
        return {bounds.x + padding, bounds.y + padding, bounds.w - padding * 2,
                bounds.h - padding * 2};
    }

    // Max text width that fits inside content area
    int max_text_width() const { return content_rect().w; }

    // Draw background + border + shadow. Call before drawing content.
    void draw_bg(SDL_Renderer* r, const Theme& t = Theme::instance()) const {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        // Shadow (skipped when alpha is 0)
        if (t.panel_shadow.a > 0) {
            SDL_SetRenderDrawColor(r, t.panel_shadow.r, t.panel_shadow.g, t.panel_shadow.b,
                                   t.panel_shadow.a);
            SDL_Rect shadow = {bounds.x + 2, bounds.y + 2, bounds.w, bounds.h};
            SDL_RenderFillRect(r, &shadow);
        }
        // Background
        SDL_SetRenderDrawColor(r, t.panel_bg.r, t.panel_bg.g, t.panel_bg.b, t.panel_bg.a);
        SDL_Rect bg = bounds;
        SDL_RenderFillRect(r, &bg);
        // Border
        SDL_SetRenderDrawColor(r, t.panel_border.r, t.panel_border.g, t.panel_border.b,
                               t.panel_border.a);
        SDL_RenderDrawRect(r, &bg);
    }

    // Push SDL clip rect to content area. Returns previous clip rect.
    // Like CSS overflow:hidden — nothing draws outside content area.
    SDL_Rect push_clip(SDL_Renderer* r) const {
        SDL_Rect prev;
        SDL_RenderGetClipRect(r, &prev);
        SDL_Rect clip = content_rect();
        SDL_RenderSetClipRect(r, &clip);
        return prev;
    }

    // Pop clip rect (restore previous).
    // SDL2: SDL_RenderGetClipRect returns {0,0,0,0} when no clip is set.
    static void pop_clip(SDL_Renderer* r, const SDL_Rect& prev) {
        if (prev.w == 0 && prev.h == 0)
            SDL_RenderSetClipRect(r, nullptr);  // no previous clip
        else
            SDL_RenderSetClipRect(r, &prev);
    }
};

// ── Text measurement ───────────────────────────────────────────────────────
// Forward-declared from telemetry.h; these are the primitives.

// Declared in telemetry.h:
//   void draw_text(SDL_Renderer*, int x, int y, const char* text, int scale, uint8_t r, g, b);
//   int text_pixel_width(const char* text, int scale);

// Compute panel width that fits all provided lines plus padding.
// Like CSS width:auto — shrink-wraps to content.
inline int auto_width(std::initializer_list<const char*> lines, int font_scale, int padding) {
    int max_w = 0;
    for (auto* line : lines)
        max_w = std::max(max_w, sandbox::text_pixel_width(line, font_scale));
    return max_w + padding * 2;
}

// Row height for a given font scale (glyph height + vertical gap).
inline int row_height(int font_scale) {
    return font_scale * 7 + S1;
}

// ── Layout helpers ─────────────────────────────────────────────────────────

// Stack layout cursor: tracks Y position while adding rows to a column.
// Like CSS flexbox column with gap.
struct ColumnLayout {
    int x;      // content left edge
    int y;      // current Y cursor
    int max_w;  // content max width (for right-alignment, separators)
    int gap;    // gap between items (like CSS gap)

    // Draw text at current position, advance cursor.
    // color is an SDL_Color for convenience.
    void text(SDL_Renderer* r, const char* str, int scale, SDL_Color c) {
        sandbox::draw_text(r, x, y, str, scale, c.r, c.g, c.b);
        y += row_height(scale);
    }

    // Draw a separator line (like CSS border-bottom on a section).
    void separator(SDL_Renderer* r, int sep_gap = S2) {
        y += sep_gap / 2;
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 40, 48, 58, 255);
        SDL_RenderDrawLine(r, x, y, x + max_w, y);
        y += sep_gap / 2 + 2;
    }

    // Skip vertical space (like CSS margin-top).
    void spacer(int px) { y += px; }
};

// Height consumed by a separator (sep_gap + 2px line).
inline int separator_height(int sep_gap = S2) {
    return sep_gap + 2;
}

// ── Tooltip ───────────────────────────────────────────────────────────────
// Renders a small info box near the mouse cursor. Like CSS title/tooltip.

inline void render_tooltip(SDL_Renderer* r, int mouse_x, int mouse_y, const char* text, int win_w,
                           int win_h) {
    if (!text || !text[0])
        return;
    constexpr int TT_SCALE = 1;
    constexpr int TT_PAD = 6;
    constexpr int TT_OFFSET = 16;  // distance from cursor

    // Wrap long text into lines (split on '|' delimiter)
    const char* lines[8];
    int line_count = 0;
    // Copy text so we can split on '|'
    char buf[256];
    int len = 0;
    for (const char* p = text; *p && len < 255; ++p)
        buf[len++] = *p;
    buf[len] = '\0';

    lines[line_count++] = buf;
    for (int i = 0; i < len && line_count < 8; ++i) {
        if (buf[i] == '|') {
            buf[i] = '\0';
            lines[line_count++] = &buf[i + 1];
        }
    }

    int max_w = 0;
    for (int i = 0; i < line_count; ++i)
        max_w = std::max(max_w, sandbox::text_pixel_width(lines[i], TT_SCALE));

    int tt_w = max_w + TT_PAD * 2;
    int tt_h = TT_PAD * 2 + row_height(TT_SCALE) * line_count;

    // Position: prefer right+below cursor, flip if near edge
    int tx = mouse_x + TT_OFFSET;
    int ty = mouse_y + TT_OFFSET;
    if (tx + tt_w > win_w - SAFE_R)
        tx = mouse_x - tt_w - 4;
    if (ty + tt_h > win_h - SAFE_B)
        ty = mouse_y - tt_h - 4;
    if (tx < SAFE_L)
        tx = SAFE_L;
    if (ty < SAFE_T)
        ty = SAFE_T;

    // Draw bg + border
    const auto& t = Theme::instance();
    SDL_SetRenderDrawColor(r, 22, 28, 42, 255);
    SDL_Rect bg = {tx, ty, tt_w, tt_h};
    SDL_RenderFillRect(r, &bg);
    SDL_SetRenderDrawColor(r, t.accent.r, t.accent.g, t.accent.b, 180);
    SDL_RenderDrawRect(r, &bg);

    // Draw text
    int text_y = ty + TT_PAD;
    for (int i = 0; i < line_count; ++i) {
        sandbox::draw_text(r, tx + TT_PAD, text_y, lines[i], TT_SCALE, 230, 235, 240);
        text_y += row_height(TT_SCALE);
    }
}

}  // namespace ui
}  // namespace sandbox
