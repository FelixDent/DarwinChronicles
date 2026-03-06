# Sandbox UI Skill

Guidelines for making UI changes to SDL2-based sandbox applications (worldgen, weather, veggen, etc.). This skill captures learned patterns and mistakes to avoid.

## Rendering Architecture

All sandboxes use SDL2 software rendering with a custom 5x7 bitmap font (`draw_text`). No TTF fonts. Text scale parameter controls size (1 = tiny, 2 = normal, 3 = large).

### Font System

- Glyphs: 5px wide × 7px tall, rendered at `scale` multiplier
- Character spacing: 6px × scale (5px glyph + 1px gap)
- `text_pixel_width(text, scale)` returns pixel width of a string
- Only uppercase A-Z, digits 0-9, and a few symbols (`.%()=:-/>`) are available
- Lowercase maps to uppercase automatically

### Color Conventions

- Panel backgrounds: `(0, 0, 0, 180)` — semi-transparent black
- Panel borders: `(80, 80, 80, 200)`
- Title text: `(220, 220, 220)` at scale 2
- Body text: `(200, 200, 200)` at scale 2
- Secondary/dim text: `(160, 160, 160)` at scale 2
- Info/description text: `(140, 150, 160)` at scale 1

## Layout Rules

### Spacing Constants

```
PAD = 8          // Panel inner padding (all sides)
ROW_H = 18       // Row height for scale-2 text lines
SWATCH = 14      // Color swatch square size
TEXT_X_OFF = 22   // Text indent after a swatch
GAP = 12         // Horizontal gap between columns
BAR_W = 192      // Gradient bar width
BAR_H = 12       // Gradient bar height
```

### Vertical Spacing

- Between a title and first content row: `ROW_H` (18px)
- Between content rows: `ROW_H` (18px)
- Between a gradient bar and its min/max labels: `BAR_H + 4` (16px)
- **Between content and a description/info line: `ROW_H + 4` (22px)** — must have clear visual separation; do NOT place info text immediately after other content
- Panel height must account for all content including info lines and gaps

### Common Mistake: Cramped Info Lines

When adding secondary description text below primary content:
- **Wrong**: `y += ROW_H - 4` or `y += 14` — too tight, text overlaps or looks cluttered
- **Right**: `y += ROW_H + 4` — gives breathing room between the content above and the smaller info text below
- Panel height must include the full gap: add `ROW_H + 8` (not just `14`) for each info line

### Panel Positioning

- Legend panels stack from bottom-left, upward
- Terrain legend is always at the bottom: `win_h - panel_h - PAD`
- Overlay legend sits above terrain legend: `win_h - terrain_legend_h - panel_h - PAD`
- Buttons position from top-right with `BTN_MARGIN = 10`

### Panel Width Calculation

Always compute panel width from the widest content element:
```cpp
int panel_w = PAD * 2 + std::max({content_a_w, content_b_w, info_w});
```

## Overlay Panels

Each overlay should have:
1. **Title** — overlay name in scale-2 bright text
2. **Visualization** — gradient bar (continuous) or color swatches (categorical)
3. **Range labels** — lo/hi labels below gradient bar, or labels next to swatches
4. **Description** — 1-line info text at scale 1, explaining what the overlay shows, with a gap above

### Gradient Overlay Template

```
┌─────────────────────────┐
│ OVERLAY NAME            │  ← scale 2, bright
│ ████████████████████████│  ← gradient bar
│ Low              High   │  ← scale 2, dim
│                         │  ← gap
│ What this field means   │  ← scale 1, info color
└─────────────────────────┘
```

### Categorical Overlay Template

```
┌─────────────────────────┐
│ OVERLAY NAME            │  ← scale 2, bright
│ ██ Category A           │  ← swatch + label
│ ██ Category B           │
│ ██ Category C           │
│                         │  ← gap
│ What this shows         │  ← scale 1, info color
└─────────────────────────┘
```

## Performance

### Tile Rendering

- Every visible tile = 1 `SDL_RenderFillRect` call (base) + 1 blended rect (overlay)
- At min zoom, all tiles visible → 65K+ draw calls for a 256×256 world
- **Decorations** (triangles for hills/mountains) must skip when tiles are small: threshold `dst.w >= 8`, not `dst.w >= 4`
- `fill_triangle` uses per-scanline `SDL_RenderDrawLine` — expensive on tiny tiles, invisible to the user

### Zoom Levels

- Default `MIN_ZOOM = 0.25`, `MAX_ZOOM = 4.0`
- At zoom 0.25, a 16px tile becomes 4px on screen — no decoration is visible
- At zoom 0.5, tiles are 8px — minimal decoration threshold

## Camera

- `center_on_world()` — centers camera without changing zoom
- `fit_world()` — centers camera AND sets zoom to show entire world (with 5% margin)
- Use `fit_world()` at startup so the user sees the full map immediately

## Lessons Learned

This section is updated when mistakes are corrected during development.

1. **Info text spacing** (2026-03-06): Description lines placed with `y += ROW_H - 4` looked cluttered and nearly overlapping. Fix: use `y += ROW_H + 4` and size the panel to match.
2. **Decoration threshold** (2026-03-06): Drawing hill/mountain triangles at `dst.w >= 4` caused jerkiness when zoomed out — thousands of invisible 4px triangles. Fix: raise to `dst.w >= 8`.
3. **Initial zoom** (2026-03-06): Starting at zoom 1.0 shows a zoomed-in portion of the map, confusing users. Fix: use `fit_world()` to start fully zoomed out.
