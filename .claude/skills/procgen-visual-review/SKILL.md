# Procedural Generation Visual Review via GPT

## When to Use

Use this skill whenever you are **perfecting procedural generation logic whose outputs can be represented as images**. This includes:

- Terrain generation (heightmaps, biomes, rivers, coastlines)
- Vegetation/plant sprite generation
- Weather visualization (cloud patterns, precipitation maps)
- Any simulation output rendered as BMP/PNG

**Trigger**: After implementing or tuning procedural generation code, before declaring it "done".

## Workflow

### 1. Generate Sample Outputs

Run the procedural generator in headless mode across **at least 3-4 different seeds** and **2+ presets** (if applicable) to capture variety:

```bash
for seed in 42 777 12345 99999; do
  ./build-release/sandboxes/worldgen/sandbox_worldgen --seed $seed --headless
  convert terrain_earth_${seed}.bmp terrain_earth_${seed}.png
done
```

### 2. Write a Description of the Approach

Prepare a concise description (2-5 paragraphs) of the generation algorithm covering:
- Pipeline stages (what happens in what order)
- Key techniques (noise types, erosion, flood-fill, etc.)
- Recent changes being evaluated
- Known limitations or areas of concern

### 3. Send to GPT for Review

Use the helper script at `.claude/skills/procgen-visual-review/gpt_review.py`:

```bash
python3 .claude/skills/procgen-visual-review/gpt_review.py \
  --images terrain_earth_42.png terrain_earth_777.png terrain_earth_12345.png terrain_earth_99999.png \
  --description "12-stage tectonic terrain pipeline: plate generation, domain-warped Voronoi, continuous tectonic fields with continental spines, thermal erosion, hydraulic erosion, valley-floor smoothing, inland elevation bias, coastal plain enforcement, coastal roughness, basin flood-fill lakes, water speckle removal, coastal smoothing." \
  --model gpt-5.2
```

The script:
- Reads `OPENAI_API_KEY` from project `.env` file or environment
- Encodes images as base64 and sends them with the description to GPT's vision API
- Returns detailed visual feedback with a 1-10 quality rating and top 3 improvements

### 4. Analyze GPT's Feedback

Read GPT's response carefully. It will provide:
- Per-image analysis of visual issues
- Overall quality rating (1-10)
- Top 3 most impactful improvements with algorithmic suggestions

### 5. Implement Fixes — Including Deep Structural Changes

Address GPT's feedback starting with the highest-impact items. **Do not stop at parameter tuning** — if GPT recommends deep algorithmic changes (e.g., Priority-Flood depression filling, process-based erosion, tectonic splines), implement them. These are the changes that move the score from 5/10 to 7+/10.

When a suggestion is complex or ambiguous:
- **Send code snippets to GPT** for review: include relevant source code in the `--description` and ask GPT to evaluate your implementation approach or suggest specific parameter values.
- **Ask GPT for clarification**: if a suggestion mentions an algorithm you're unsure about (e.g., "Priority-Flood," "stream power erosion," "Strahler ordering"), ask GPT to explain the algorithm in detail with pseudocode and parameter ranges for your specific resolution/scale.
- **Send intermediate pipeline outputs**: share debug images (uplift field, flow accumulation, slope map, province mask) so GPT can pinpoint exactly which stage produces artifacts.

After each round of fixes:
1. Rebuild
2. Regenerate samples (same seeds for comparison)
3. Re-submit to GPT

### 6. Iterate Until Satisfied

Repeat steps 1-5 until GPT rates the output **7+/10** or reports no critical issues.

A session might look like:
```
Round 1: GPT says "4/10 — mountains hug coasts, no lakes, grainy texture"
  → Fix: inland bias, basin flood-fill, lower noise frequency
Round 2: GPT says "5/10 — better structure but uniform ridge texture everywhere"
  → Fix: province mask, along-strike segmentation, coastal plains
Round 3: GPT says "5/10 — needs Priority-Flood hydrology, process-based erosion"
  → Ask GPT for Priority-Flood pseudocode, implement it
Round 4: GPT says "6/10 — drainage hierarchy works, needs multi-scale erosion"
  → Implement stream-power erosion, send code for review
Round 5: GPT says "7/10 — good variety, convincing drainage. Minor: some shelf artifacts"
  → Accept or polish
```

### 7. Send Code for Algorithm Review

When implementing a complex algorithm GPT suggested, you can send the code back for review:

```bash
python3 .claude/skills/procgen-visual-review/gpt_review.py \
  --images terrain_earth_42.png \
  --description "I implemented Priority-Flood depression filling. Here's the core code:
\`\`\`cpp
// ... paste key function ...
\`\`\`
Does this look correct? What parameter values should I use for breach_threshold and min_lake_area at 256x256 resolution with 1km-equivalent tiles?" \
  --model gpt-5.2
```

### 8. Document the Final State

After the last iteration, save GPT's final review as a reference:
```bash
python3 .claude/skills/procgen-visual-review/gpt_review.py \
  --images terrain_earth_42.png terrain_earth_777.png \
  --description "..." --model gpt-5.2 --json > procgen_review_$(date +%Y%m%d).json
```

## Script Requirements

- Python 3.11+
- `openai` package (`pip install openai`)
- `OPENAI_API_KEY` in project `.env` file or environment
- ImageMagick `convert` for BMP→PNG conversion

## Tips

- **Use the same seeds across iterations** so you can compare before/after on identical inputs
- **Include at least one "difficult" seed** (e.g., one that produces very little land, or very much land)
- **Describe recent changes explicitly** in the description so GPT knows what to focus on
- **Don't over-optimize for one seed** — if GPT flags an issue on seed 777 but the fix breaks seed 42, that's a regression
- **Save intermediate PNGs** with descriptive names (e.g., `terrain_earth_42_v2.png`) for your own before/after comparison
- **Send code when stuck** — GPT can review algorithmic implementations and suggest fixes
- **Send debug layers** (heightmap, flow accumulation, province mask) as separate images to help GPT diagnose which pipeline stage causes artifacts
- **Don't avoid deep changes** — parameter tuning plateaus around 5/10. The jump to 7+ requires structural algorithm improvements (Priority-Flood, stream-power erosion, proper watershed extraction)
