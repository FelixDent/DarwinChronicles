# Sandbox UI Skill

Guidelines for making UI changes to SDL2-based sandbox applications (worldgen, weather, veggen, etc.). This skill captures learned patterns and mistakes to avoid.

## CSS-Thinking Principles

Think of SDL2 UI the same way you'd think about CSS in a web/mobile app. These principles are mandatory:

1. **Box model**: Every UI element is a box with padding, border, content area. Use `ui::Panel` which enforces this.
2. **overflow:hidden**: Text MUST NOT extend beyond its panel bounds. Use `Panel::push_clip()` / `pop_clip()` to enforce. This is non-negotiable.
3. **width:auto**: Panel width MUST be computed from the widest content line, not from a hardcoded reference string. Use `ui::auto_width()` or iterate over pre-formatted lines. Never assume which line is widest.
4. **Anchor positioning**: Panels anchor to screen corners using `ui::Anchor` + safe-area insets, not absolute pixel coordinates. Think `position: absolute; top: 8px; right: 8px;`.
5. **Spacing tokens**: Use `ui::S0` through `ui::S5` (multiples of 4px) instead of ad-hoc pixel values. Like CSS spacing utilities.
6. **Centralized theme**: All colors come from `ui::Theme::instance()`. Never hardcode RGB values in panel/text rendering code.
7. **Column layout**: Use `ui::ColumnLayout` for vertical stacking with consistent row heights, like CSS flexbox column.
8. **Independent positioning**: Panels must NOT position relative to other panels' hardcoded sizes. Each panel computes its own position from screen anchors. If panels need to avoid overlap, use safe zones (top-left vs top-right vs bottom-left), not "left edge of panel X minus 12px".

### Component Library: `ui.h`

All panels must use the `ui.h` component library (`sandboxes/weather/ui.h`):
- `ui::Panel` — background + border + shadow + clip rect (CSS box)
- `ui::ColumnLayout` — vertical stack with text(), separator(), spacer() (CSS flexbox column)
- `ui::Theme` — centralized color palette (CSS variables)
- `ui::anchor_rect()` — position relative to screen corners (CSS position:absolute)
- `ui::auto_width()` — shrink-wrap panel to content (CSS width:auto)
- `ui::row_height()` — consistent line height per font scale

## Rendering Architecture

All sandboxes use SDL2 software rendering with a custom 5x7 bitmap font (`draw_text`). No TTF fonts. Text scale parameter controls size (1 = tiny, 2 = normal, 3 = large).

### Font System

- Glyphs: 5px wide × 7px tall, rendered at `scale` multiplier
- Character spacing: 6px × scale (5px glyph + 1px gap)
- `text_pixel_width(text, scale)` returns pixel width of a string
- Only uppercase A-Z, digits 0-9, and a few symbols (`.%()=:-/>`) are available
- Lowercase maps to uppercase automatically

### Color Conventions

Use `ui::Theme::instance()` for all colors. The centralized palette:

- `theme.panel_bg` — `(11, 16, 28, 220)` dark blue-black
- `theme.panel_border` — `(42, 53, 66, 200)` subtle blue-gray
- `theme.panel_shadow` — `(0, 0, 0, 80)` drop shadow
- `theme.text` — `(215, 220, 227)` primary content
- `theme.text_muted` — `(154, 166, 178)` secondary/dim
- `theme.text_bright` — `(242, 245, 248)` emphasis
- `theme.accent` — `(231, 184, 76)` section headers (gold)
- `theme.info` — `(110, 195, 240)` highlights (blue)
- `theme.danger` — `(255, 80, 80)` warnings (red)
- `theme.ok` — `(80, 220, 130)` positive (green)

Legacy color constants (for overlay legends not yet migrated to ui.h):
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
4. **Hint/shortcut text must match actual key bindings** (2026-03-08): Displayed "-=:SPEED" but `-` and `=` are overlay keys, not speed controls (speed uses numpad +/- only). Fix: always cross-reference hint text against actual SDL_KEYDOWN handlers before shipping. Never assume key function from label.
5. **Shortcut text formatting** (2026-03-08): "0-=" looks like a range (0 through =) but means three separate keys. Fix: use spaces between individual keys: "0 - = `" to show they are discrete.
6. **UI element overlap** (2026-03-08): Shortcuts hint box and telemetry panel both positioned at top-right, overlapping. Fix: compute the left edge of the telemetry panel and position hints to its left. Always check that independently-positioned UI elements don't collide at the target resolution.
7. **Color ramp visibility at low values** (2026-03-08): Precipitation ramp starting color (#BFD7EA, very light blue) appeared near-white at alpha 180, making the entire overlay look uniform white. Fix: test color ramps at their lightest end — if the lightest swatch is hard to distinguish from white/transparent, darken the starting color. Use mid-range blues (#6BAED6+) for precipitation.
8. **Panel height formula must match content** (2026-03-08): Telemetry panel height formula had extra row/separator padding (ROW_H*24 + SEP_GAP*6) when actual content only needed ROW_H*22 + SEP_GAP*3, leaving visible empty space at bottom. Fix: count actual rows and separators, don't pad "just in case."
9. **Overlay stats must update in dynamic mode** (2026-03-08): SHADOWS stat from static weather bake showed 0 when dynamic atmosphere was running. Fix: if a stat comes from a static bake but the system is running dynamically, either recompute it from dynamic state or hide it.
10. **Top row must share a single baseline** (2026-03-08): Hint box and telemetry panel at different Y positions looks accidental and messy. Fix: define a single TOP_MARGIN constant (e.g., 40px) and position ALL top-row elements at the same Y.
11. **Camera must fit world on startup and clamp to bounds** (2026-03-08): Starting at zoom 1.0 shows partial map; panning can reveal empty background. Fix: use `fit_world()` at startup to show entire world, and `clamp_to_world()` every frame to prevent camera from exposing void.
12. **Speed controls must work without numpad** (2026-03-08): Many laptops lack numpads. Use universally-available keys (`,` and `.` for slower/faster) instead of numpad-only keys.
13. **Consult GPT on all user-facing text** (2026-03-08): Shortcut hints, labels, and status messages must be reviewed by GPT before shipping. Developers miss unclear abbreviations, wrong key names, and confusing formatting. Added as mandatory step to gpt-ui-review skill.
14. **Consult GPT on layout organization** (2026-03-08): Before implementing layout changes, describe the current layout to GPT for alignment/anchoring/consistency review. Added as mandatory step to gpt-ui-review skill.
15. **No duplicate messaging** (2026-03-08): "OVERLAY: NONE" shown in both top button and bottom hint chip. Show overlay status in exactly one place — the top button is sufficient.
16. **Text overflow from hardcoded panel width** (2026-03-08): Panel width was hardcoded from one reference string ("ENERGY DRIFT: -00.00%") but actual content like "ARIDITY: 0.82  A:149 H:4947" is wider, causing text to overflow the panel edge. Fix: always compute panel width from ALL content lines (CSS width:auto). Created `ui.h` component library with Panel (clip rect for overflow:hidden), auto_width(), and ColumnLayout.
17. **Panel positioning coupled to other panels** (2026-03-08): Hint box computed its X position from "telemetry panel X minus hint width minus 12px" — a hardcoded dependency on another panel's size. When the telemetry panel changed size, the hint box overlapped. Fix: position each panel independently from screen anchors (top-left, top-right, bottom-left). Use separate screen zones, not relative offsets between panels.
18. **Think CSS, not pixel math** (2026-03-08): Every ad-hoc `int y = panel_y + PAD + 2` is a layout bug waiting to happen. Use ColumnLayout for vertical stacking, Panel for box model + clip, Theme for colors. Components enforce consistency; pixel math doesn't.
19. **Panel alpha must be near-opaque** (2026-03-08): Semi-transparent panel backgrounds (alpha 210-220) let terrain glyphs bleed through, creating "duplicate letter" artifacts. Fix: use alpha 245+ for all panel backgrounds. Map content should never be visible through UI panels.
20. **Gradient key must account for all labels** (2026-03-08): `bar_w = max(180, title_width)` didn't account for lo+hi tick labels at full width. "1.0" was clipped at the right edge of the precipitation legend. Fix: `bar_w = max(180, max(title_w, lo_w + hi_w + gap))`.
21. **Don't show implementation details in overlay legends** (2026-03-08): Storminess overlay had a "MULTIPLICATIVE MODEL: humidity * instability^3..." formula panel — useless to the user, visual clutter. Overlay legends should show what the colors mean, not how they're computed.
22. **Align panels with the button row** (2026-03-08): Dynamics panel at Y=40 while buttons at Y=10 left a 30px gap that looked accidental. Fix: panel Y = BTN_MARGIN (10) to share the same top edge as buttons.
