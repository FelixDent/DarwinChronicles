# skill.md — Visual Feedback Loop with Invariants for Procedural Pixel-Art Sprite Generation

This skill instructs Claude Code how to implement a **visual iteration loop** for procedural pixel-art sprites that is **driven by explicit invariants** (hard constraints that must always hold). The loop is:

**code → generate many seeds → validate invariants → render debug overlays → inspect → classify failures by invariant → repair/enforce → repeat**

The key upgrade vs a generic loop: *every visual defect must be mapped to a violated invariant, and invariants must be enforced by code (repair/prune/resample), not by hoping the generator “usually works.”*

---

## 0) Non-negotiables

1) **Determinism**
- Every sprite must be reproducible from:
  - `generator_version_id`
  - `seed`
  - `template_id`
  - `direction`
  - `frame`
  - `stage` (growth/age)
  - `palette_id`
- Use a stable PRNG; never call time-based randomness.
- **No randomness during rasterization**: random only in structure generation.

2) **Fixed seed sets**
- Maintain:
  - `SMOKE_SEEDS` (8–16 quick)
  - `REGRESSION_SEEDS` (64–256)
  - `TORTURE_SEEDS` (edge cases; extreme traits, rare templates)

3) **Always produce visual artifacts**
- Every run writes:
  - `sheet.png` contact sheet (labeled)
  - `gallery.html` (sortable/filterable)
  - `meta.json` (run config + seed list + version)
  - `report.json` (invariant metrics + failures)
  - `debug/` overlays for failing cases

4) **Hard gating**
- Runs must summarize invariant pass rates.
- Regression run is considered failed if any hard invariant fails beyond allowable exceptions.

---

## 1) Core concept: Invariants-first generation

Procedural generation must be treated like a compiler:
- Generate candidate → validate invariants → if fail, repair/prune/resample → only then output.

**Rule:** Any visual defect observed during review must be classified as *an invariant violation* or *a softness/tuning issue with a measurable metric.*

---

## 2) Required invariants (minimum set)

Implement invariants as code checks returning:
- boolean `pass`
- metrics
- debug info (e.g., offending pixels, components, anchor points)

### A) Structural / graph invariants

**INV-S1: Root connectivity (wood)**
- All wood pixels must belong to the single connected component containing the trunk/root base.
- No floating wood components.

**INV-S2: Attachment anchoring (foliage/accessories)**
- Every foliage cluster must have an anchor on wood or a valid canopy anchor region.
- Foliage must overlap anchor or be connected by a stem pixel.

**INV-S3: Topology bounds (template-defined)**
- Branch order ≤ `max_order`
- Segment lengths within `[min,max]`
- Attachment spacing ≥ `min_spacing`
- No self-intersections beyond tolerance (optional but useful)

**INV-S4: Stage continuity (growth/age)**
- Growth stages must derive from the same base structure:
  - seedling ⟶ young ⟶ mature: size/foliage non-decreasing
  - old/dead: foliage decreases; wood may remain or increase slightly

### B) Pixel-art style invariants

**INV-P1: Palette closure**
- All pixels must be in the active palette ramps.
- No “new” colors.

**INV-P2: No orphan noise**
- Orphan pixels count below threshold per layer (wood/foliage/outline).
- No isolated single pixels unless explicitly tagged (sparkle, fruit speckle).

**INV-P3: Outline rule consistency**
- Either:
  - silhouette-only outline, or
  - no outline
- Never mixed rules within a sprite.

**INV-P4: Bounding box sanity**
- Sprite pixels must lie inside expected bounds (no stray far-away pixels).

### C) Biological/ecological invariants (template + environment)

**INV-B1: Silhouette envelope compliance**
- Tree canopy must match template envelope:
  - conifer: triangular/conical
  - broadleaf: oval/round
  - shrub: low/wide
- Measured as a numeric “envelope error” metric.

**INV-B2: Proportion constraints**
- Template-specific ratios must hold, e.g.:
  - deciduous trunk height ratio 0.20–0.35
  - conifer trunk visible ratio 0.10–0.20
  - canopy radius vs height within bounds

**INV-B3: Environment coherence**
- Trait choices must be consistent with environment inputs:
  - dry → lower leaf density/canopy radius
  - cold → evergreen/conifer bias, reduced broadleaf size
  - high wind → reduced height and long horizontal branches
- These are enforced as trait clamps and template selection constraints.

---

## 3) Enforcement mechanisms (how to guarantee invariants)

For each invariant, implement at least one *enforcement* step:

### For INV-S1 (root connectivity)
- After rasterizing wood:
  1) compute connected components (8-neighbor)
  2) find component containing trunk base pixel
  3) delete all other wood pixels
- Optional: “bridge gaps” pass if the main trunk component is fragmented by rounding.

### For INV-S2 (attachment anchoring)
- For each attachment:
  - compute anchor pixel on parent segment
  - stamp foliage so at least one stamp pixel overlaps anchor
  - if not possible, draw a 1–2 px stem connector
- After foliage render:
  - delete foliage components not connected to any valid anchor region (optional, depends on style)

### For INV-P1 (palette closure)
- Quantize all pixels to nearest palette entry (or forbid out-of-palette writes).
- Fail hard if any pixel remains out-of-palette after quantization.

### For INV-P2 (no orphan noise)
- Cleanup pass that:
  - removes isolated pixels
  - fills pinholes
  - merges 1px gaps inside blobs
- Protect “structural pixels” via a `protected_mask` (e.g., trunk skeleton, anchor pixels).

### For INV-B1/B2 (silhouette & proportions)
- Prune or resample:
  - If envelope error too high: prune foliage outside envelope *softly* (allow small overshoot margin).
  - If proportions invalid: clamp traits and regenerate structure.

**Rule:** Prefer repair → prune → resample in that order.

---

## 4) Harness architecture (what Claude must implement)

### A) Generator API
- No file I/O in generator.

Input:
- `SpriteRequest { seed, template_id, env_stats, stage, direction, frame, resolution, palette_id }`

Output:
- `SpriteLayers { wood_rgba, foliage_rgba, outline_rgba, final_rgba }`
- `SpriteMeta { traits, anchors, envelope_params, trunk_base_px, ... }`

### B) Validation API
Implement:

- `ValidationResult validate(const SpriteLayers&, const SpriteMeta&, const Ruleset&)`

Return:
- `pass_fail` per invariant
- metrics (counts, errors)
- debug payload (component labels, offending pixels)

### C) Enforcement pipeline
The generator run must follow:

1) `structure = generate_structure(seed, template, env)`
2) `layers = rasterize(structure)`
3) `layers = enforce_invariants(layers, meta)`  // connectivity, anchoring, palette, cleanup
4) `result = validate(layers, meta)`
5) If fails:
   - attempt repair/prune up to N tries
   - else mark sprite FAIL but still output with overlays
6) Output artifacts

---

## 5) Debug overlays (required for fast diagnosis)

When any invariant fails, output debug images:

- `components_wood.png` (component colored labels)
- `anchors.png` (anchor points marked)
- `envelope.png` (silhouette envelope drawn)
- `orphan_heat.png` (orphan pixels highlighted)
- `palette_violations.png` (out-of-palette pixels)

These overlays must be linked from `gallery.html` for each failing sprite.

---

## 6) Visual review protocol (how Claude should “inspect”)

Claude should not rely on subjective statements. It must:

1) Open the contact sheet/gallery.
2) Look at *failed cases first* (sorted by invariant failures).
3) For each visible defect:
   - Identify which invariant(s) it violates.
   - Identify which pipeline stage caused the violation:
     - structure gen
     - stamping
     - clipping/envelope
     - shading
     - cleanup
4) Implement the smallest change that fixes that invariant.
5) Re-run SMOKE seeds, then diff vs prior version.

**Rule:** Every change must cite:
- which invariant(s) it targets
- how it enforces them (repair/prune/resample)
- expected metric movement (e.g., orphan count down, envelope error down)

---

## 7) A/B diffing (must support)

Implement:

`--diff <versionA> <versionB>`

Output:
- `diff_gallery.html`
- side-by-side A/B for each sprite key
- optional pixel diff heatmap
- invariant metric diff table per sprite

Sprite key:
`(seed, template_id, stage, direction, frame, palette_id)`

---

## 8) Automated report (must support)

Write:
- `report.json` including:
  - pass rate per invariant
  - worst offenders (top N by each metric)
  - summary of repairs/prunes/resamples attempted

Also print a concise console summary:

- total sprites generated
- invariant pass counts
- worst seeds and why (which invariant)

---

## 9) Iteration cadence (how Claude should proceed)

1) Build one template (e.g., broadleaf) end-to-end.
2) Add invariants + enforcement.
3) Expand to conifer, shrub, etc.
4) Add stages (growth) only after base is stable.
5) Add wind frames/directions only after invariants pass.

Use:
- SMOKE: every code change
- REGRESSION: every milestone
- TORTURE: after major algorithm changes

---

## 10) Quick mapping: symptoms → likely invariant violation

- Floating branches → INV-S1 (root connectivity) broken by clipping/cleanup/rasterization
- Speckled leaves → INV-P2 (orphan noise) / too many single pixels
- Colors drifting → INV-P1 (palette closure)
- Weird triangles / over-symmetry → INV-B1 (envelope too strict) + template variance too low
- Stage mismatch → INV-S4 (stage continuity)
- Animation popping → frame determinism broken (random during render) + INV-S4

---

## Summary

Claude must implement a procedural art loop where:
- **invariants are explicit**
- **validation is automatic**
- **enforcement guarantees constraints**
- **visual review is guided by failed invariants**
- **every iteration is reproducible and diffable**

This makes the generator steadily improve without regressions and prevents recurring defects like disconnected branches or noisy foliage.