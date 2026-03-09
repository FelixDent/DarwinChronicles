# GPT-Assisted UI Review

## When to Use

Use this skill when you want expert feedback on a sandbox or module's **interactive UI** — layout, overlays, legends, controls, visual clarity, color choices, information density, and overall feel. This is for **UI/UX review**, not simulation behavior (use `gpt-behavior-tuning`) or procgen output quality (use `procgen-visual-review`).

**Trigger**: After building or modifying a sandbox's UI — new overlays, legend changes, panel layout, color schemes, button placement, camera behavior, or any user-facing visual change.

## The Loop

```
1. DESCRIBE  ──  What is this UI? What feel are we going for?
       ↓
2. CAPTURE   ──  Take screenshots in multiple states
       ↓
3. REVIEW    ──  Send to GPT with context
       ↓
4. IMPLEMENT ──  Apply GPT's suggestions
       ↓
5. RE-CAPTURE ── Verify improvements
       ↓
   Done? YES → Ship it. NO → Go to 3.
```

## Step 1: Describe the UI

Before taking screenshots, write a brief description covering:

- **What the module/sandbox does** (1-2 sentences)
- **Target audience** (developer tool? end-user game? scientific viz?)
- **UI feel we're going for** (e.g., "clean scientific visualization with dense but readable telemetry", "game-like with intuitive overlays", "developer debug tool — dense info is fine")
- **Key UI elements** (legend panel, overlay modes, controls, camera, buttons)
- **Known concerns** (anything you already suspect is wrong)

## Step 2: Capture Screenshots

Take screenshots that cover the UI's range of states:

1. **Default view** — what the user sees on launch
2. **Zoomed out** — full map/world view
3. **Zoomed in** — detail view
4. **Multiple overlays** — at least 3-4 different overlay modes showing different data
5. **Legend/panel** — close enough to read text
6. **Edge cases** — small window, extreme zoom, lots of data

For SDL2 sandboxes, use the built-in screenshot functionality or external tools. Save as PNG.

```bash
# Example: capture weather sandbox screenshots
./build/sandboxes/weather/sandbox_weather &
# Take screenshots manually or via SDL_SaveBMP + convert
```

For headless capture, render to BMP and convert:
```bash
convert screenshot.bmp screenshot.png
```

## Step 3: Send to GPT for Review

Use the helper script:

```bash
python3 .claude/skills/gpt-ui-review/gpt_ui_review.py \
  --images screenshot1.png screenshot2.png screenshot3.png \
  --description "Weather sandbox: atmospheric simulation overlaid on procedural terrain. Target feel: scientific visualization tool — dense telemetry is acceptable but must be readable. Legend panel shows live stats. 20 overlay modes cycled with keyboard. Camera supports pan+zoom." \
  --focus "layout,colors,readability"
```

### Focus areas (optional, comma-separated):
- `layout` — spatial arrangement of panels, legends, buttons
- `colors` — color schemes, contrast, accessibility
- `readability` — text size, font clarity, information density
- `overlays` — overlay visualization quality, color ramps, data mapping
- `controls` — discoverability, key binding display, button placement
- `consistency` — visual consistency across modes/states
- `feel` — overall aesthetic, does it match the target feel

If no `--focus` is given, GPT reviews all areas.

## Step 4: Implement Changes

**Implement ALL of GPT's suggestions unless there is a specific technical reason not to.** Do not cherry-pick or prioritize — GPT's recommendations are a complete package and partial implementation leaves inconsistencies. If a suggestion genuinely cannot be implemented (e.g., requires a font system that doesn't exist, would break an API contract, or conflicts with another suggestion), document WHY it was skipped and note it in the follow-up review.

Common categories:

- **Color ramp changes** — adjust overlay gradients for better perceptual uniformity
- **Legend repositioning** — move panels to reduce occlusion
- **Font/text sizing** — increase readability at various zoom levels
- **Information hierarchy** — emphasize important stats, de-emphasize noise
- **Contrast fixes** — ensure overlays are visible on all terrain types
- **Button/control discovery** — add key hints, tooltips, or on-screen help
- **Layout restructuring** — panel grouping, alignment, separators
- **Removing redundancy** — duplicate labels, unnecessary elements

## Step 5: Re-capture and Verify

Take new screenshots of the same states and re-send to GPT:

```bash
python3 .claude/skills/gpt-ui-review/gpt_ui_review.py \
  --images new1.png new2.png new3.png \
  --description "Same weather sandbox. Applied: moved legend to right, increased font, fixed overlay contrast." \
  --focus "layout,readability" \
  --previous-rating 5
```

Iterate until GPT rates 7+/10 on all focus areas.

## Script Usage

```bash
# Full review
python3 gpt_ui_review.py \
  --images img1.png img2.png img3.png \
  --description "Description of module and target feel..." \
  --focus "layout,colors,readability"

# Quick follow-up after changes
python3 gpt_ui_review.py \
  --images new1.png new2.png \
  --description "Applied changes: X, Y, Z" \
  --previous-rating 5

# JSON output for programmatic use
python3 gpt_ui_review.py \
  --images img1.png \
  --description "..." \
  --json

# Different model
python3 gpt_ui_review.py \
  --images img1.png \
  --description "..." \
  --model gpt-4.1
```

## What Makes Good Screenshots

| Bad | Good |
|-----|------|
| Single default view | 4-6 views covering zoom/overlay range |
| Tiny window | Full-size window showing all UI elements |
| Only one overlay | Multiple overlays showing color variety |
| Legend cropped | Legend fully visible and readable |
| Similar states | Contrasting states (zoomed in vs out, different data) |

## Mandatory: Consult GPT on All User-Facing Text

**Any text box, hint, label, or status message** shown in the UI must be reviewed by GPT before shipping. This includes:

- Keyboard shortcut hint text
- Overlay names and legend labels
- Status messages (paused, speed, etc.)
- Tooltip or description text

GPT catches issues that developers miss: unclear abbreviations, confusing key labels (e.g., "0-=" looks like a range), inaccurate descriptions (e.g., listing wrong keys), and text that doesn't match what users actually see on their keyboards.

**Process**: Include the text content in your `--description` when sending screenshots for review. Ask GPT specifically: "Is the shortcut/hint text clear to a first-time user?"

## Mandatory: Consult GPT on Layout Organization

**Before implementing any layout change**, describe the current layout to GPT and ask for recommendations. GPT provides expert-level spatial design feedback that prevents common mistakes:

- **Alignment**: All elements at the same vertical position must share exact pixel-aligned top edges. No "close enough" — misalignment reads as accidental.
- **Anchoring**: Every UI element should be visually anchored to another element or to a consistent margin/grid. Floating elements look messy.
- **Consistency**: Same-function elements (legends, hints, panels) must use the same styling, sizing, and positioning rules across all states.
- **Overlap**: Always check that independently-positioned elements don't collide at all supported window sizes and zoom levels.
- **Centering**: When the viewport can show empty space beyond the world boundary, the world must be centered — not offset.

**Process**: Include layout description in `--description`. Use `--focus "layout"` to get spatial arrangement feedback.

## Tips

- **Include the legend in every screenshot** — GPT can't evaluate readability if it can't see the text
- **Capture at native resolution** — don't resize screenshots before sending
- **Label your screenshots** — use descriptive filenames (e.g., `weather_temperature_overlay.png`)
- **Send terrain + overlay pairs** — if an overlay changes the look, show with and without
- **Implement everything** — apply ALL of GPT's suggestions per round, not just the top 3
- **Keep the description updated** — if you change the target feel, update the description

## Script Requirements

- Python 3.11+
- `openai` package (`pip install openai`)
- `OPENAI_API_KEY` in project `.env` file or environment
