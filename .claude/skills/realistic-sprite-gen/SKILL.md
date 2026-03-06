# skill.md — Reliable Procedural Generation of Realistic Living Forms (Pixel Art + Simulation-Driven)

This skill describes how to generate **believable plants and animals procedurally** with **high reliability** (low “random weirdness”), by enforcing constraints, generating structure first, and tying traits to the environment. It is written as implementation-oriented guidance for Claude Code.

---

## Goals

- Produce outputs that look **consistent, intentional, and biologically plausible**.
- Ensure variation is **structured**, not arbitrary noise.
- Make morphology and materials **fit the environment** (climate/terrain/ecology).
- Use a **multi-stage pipeline**:
  1) **Base structure**
  2) **Secondary structure**
  3) **Surface & styling**
  4) **Animation/behavior cues**
- Provide built-in **validations and pruning** to prevent nonsense.

---

## Core Principles

### 1) Constrain generation with templates (don’t freeform)
Always pick a **Growth Form / Body Plan Template** first, then mutate parameters within safe bounds.

**Plants — Growth forms**
- Conifer, Broadleaf tree, Shrub, Grass tuft, Palm, Succulent/Cactus, Moss/Lichen, Aquatic.

**Animals — Body plans**
- Quadruped, Biped, Insectoid (6 legs), Arachnid (8 legs), Serpentine, Avian (wings + legs), Aquatic (fins), Amorphous (rare).

Templates define:
- allowed limb/branch counts
- angle ranges
- symmetry rules
- silhouette envelope targets
- attachment rules (leaves at tips vs along stems, etc.)

**Rule:** No template = no generation.

---

### 2) Structure first, then layers
Generate a clean “skeleton/graph” first, then render/style on top.

- **Plants:** trunk/stems/branches graph → foliage placement → pixel rasterization → shading/outline → cleanup
- **Animals:** body plan graph (segments + joints) → part shapes → skin/pattern overlays → pose/animation

**Rule:** Never decide pixels directly before having a structure.

---

### 3) Environment drives traits (ecological coherence)
Morphology must match environment-derived constraints:
- temperature regime
- moisture / effective precipitation
- seasonality
- wind/storm frequency
- soil fertility / water holding
- elevation

Traits should be sampled from distributions conditioned on those inputs.

**Example mappings**
- Cold + seasonal → conifer-like plant form, tougher leaves, lower leaf-area density.
- Hot + dry → sparse canopy, small leaves/needles, thicker stems, more defensive traits.
- Wet + warm → high canopy height, dense foliage, broader leaves.
- High wind → shorter plants, stronger taper, fewer long unsupported branches.
- Poor soil → slow growth traits, lower density, smaller canopy.

---

### 4) “Realism” comes from constraints + feedback, not detail
Believable results come from:
- silhouette consistency
- coherent branching/limb placement
- sensible size ratios
- limited palettes + stable shading rules
- spatial correlation (neighbors similar)

Avoid:
- per-pixel randomness
- too many independent random knobs
- unconstrained mutation of topology each generation

---

## Implementation Pattern

### A) Use a trait vector (genome) + deterministic PRNG
Represent each organism/plant as a small trait vector. Use a seed for reproducibility.

**Plant trait vector (minimal)**
- height
- canopy_radius
- taper
- branch_density
- branch_angle_bias
- leaf_cluster_size
- leaf_density
- droop (gravity effect)
- palette_id (biome palette)
- deciduous (bool) / evergreen

**Animal trait vector (minimal)**
- body_scale
- segment_count (within template bounds)
- limb_count (fixed by template or bounded)
- limb_length_ratio
- head_size_ratio
- tail_length_ratio
- gait_params (stride length, frequency)
- pattern_id, palette_id
- insulation / metabolism (ties to climate)

**Rule:** All generated geometry/sprites must be deterministic from `(species_id, age, direction, season_state)`.

---

### B) Multi-stage pipeline (recommended)

#### Stage 1 — Select template based on environment
Input: env stats (temp, moisture, seasonality, wind, productivity)

Output: template ID + bounded trait distribution

**Example**
- If `temp_mean < 5°C`: prefer Conifer / Shrub / Moss.
- If `moisture < 0.2`: prefer Shrub / Succulent / Sparse Grass.
- If `seasonality high`: prefer Deciduous broadleaf or grasses (depending on moisture).

#### Stage 2 — Generate base structure (graph)
Plants: directed graph of segments (trunk → branches → twigs)
Animals: graph of segments/joints (torso → limbs → head/tail)

**Constraints to enforce**
- connectivity (no floating limbs/branches)
- monotonic trunk growth (plants)
- bilateral symmetry (animals) unless template allows asymmetry
- angle bounds (no backward-growing trunk; no legs attached to head unless allowed)
- branch/limb count within template bounds
- minimum spacing between attachments (avoid spaghetti)

#### Stage 3 — Add secondary structure (attachments)
Plants: leaf clusters, flowers, fruit, needles
Animals: ears, horns, fins, spikes, patterns

**Constraints**
- foliage mostly on twig tips for broadleaf; along branches for conifers
- fruit only if plant productivity supports it
- animal features must attach to valid anchor points (head/torso/tail) only

#### Stage 4 — Rasterize/render to pixel art
Do not “paint” random pixels. Use controlled primitives + stamps.

Plants:
- rasterize wood segments as capsules
- stamp foliage from a small library of leaf-cluster stamps
- apply fixed shading ramp and optional silhouette outline
- run cleanup pass (remove orphans, fill pinholes)

Animals:
- assemble from procedural parts (ellipses/capsules) into layers
- apply shading ramp
- apply pattern overlay (stripes/spots) with controlled noise
- cleanup pass

#### Stage 5 — Animation + direction variants
Avoid arbitrary sprite rotation (crunchy pixels). Prefer:
- generate 4 or 8 direction variants by re-projecting structure
- generate 3–5 wind frames for plants by bending skeleton slightly
- animate animals via part transforms (legs swing, body bob) rather than redrawing every frame

---

## Realism Constraints: Examples (non-negotiable checks)

### Plants
**Structural constraints**
- Trunk must remain connected and generally upward.
- Branch thickness must taper with order (`order+1` thinner than parent).
- Max branch order: usually 2–4.
- Branch density decreases with height for many forms (except conifer).
- No branches longer than a fraction of trunk height (template-defined).

**Silhouette constraints**
- Conifer: triangular / conical envelope.
- Broadleaf: oval/round envelope, canopy concentrated near top third.
- Shrub: low, wide envelope.

**Ecology constraints**
- In dry climates: reduce leaf density and canopy radius.
- In cold: reduce broad leaves; increase evergreen/needle likelihood.
- In windy: prune high-extent branches.

### Animals
**Topology constraints**
- Bilateral symmetry by default.
- Limbs attach to torso segments only.
- Limb length ratios constrained (no “legs longer than body * 3” unless template permits).
- Head size ratio constrained by template.

**Movement constraints**
- If body is heavy/short-legged: low max speed, low jump.
- If long-legged: higher stride, higher speed but higher energy cost.
- If serpentine: no “jump” (or extremely limited).
- Gait must match limb count (quadruped walk cycle vs insect gait).

**Ecology constraints**
- Cold climates increase insulation trait and reduce extremity length.
- Open plains favor speed + low cover reliance; forests favor maneuverability/climbing traits.
- Low productivity favors smaller body sizes and efficient metabolism.

---

## Preventing “Random Weirdness”: Key Tactics

### 1) Limit topology mutations
Allow continuous trait mutation often (size, density, ratios), but topology changes rarely:
- e.g., limb_count or new branch layer occurs once per many generations.

### 2) Use correlated randomness
Nearby instances of the same species should share:
- palette
- general silhouette
- proportions
with small per-instance variation.

### 3) Enforce “style passes”
After rasterization:
- remove isolated pixels
- fill single-pixel holes
- enforce outline rules consistently
- clamp colors to palette ramps

### 4) Prune by fitness + plausibility
If structure violates constraints, either:
- repair (snap angles, clamp lengths), or
- prune offending segments/features, or
- resample trait(s)

Never ship invalid outputs.

---

## Recommended Data Interfaces (for integration)

### Inputs from environment per tile/region
- temp_mean (per season bin)
- moisture / soil_moisture_mean
- seasonality index
- windiness / storminess
- productivity index
- elevation band / slope

### Outputs from vegetation/creature generation
Plants:
- sprite variants (direction, season state, wind frames)
- interaction fields (food, cover, movement cost modifier)

Animals:
- sprite variants (direction, gait frames)
- collision footprint (tile fraction)
- movement parameters (speed, step height, jump)
- behavior-relevant traits (vision range, metabolism)

---

## Suggested Parameter Defaults (pragmatic)

- Directions: 4 (start), upgrade to 8 later.
- Plant wind frames: 3 (calm/left/right), optionally 5 (add gust).
- Plant stamps: 12–24 leaf cluster stamps.
- Palette ramps: 3–5 colors per material (wood/leaves/flowers).
- Max plant branch order: 3.
- Max animal templates: 6–8 core plans.

---

## Validation Checklist (run for every generated asset)

- [ ] Graph connected, no overlaps beyond tolerance.
- [ ] Ratios within template bounds.
- [ ] Silhouette within envelope bounds (or pruned).
- [ ] Palette-only colors (no stray colors).
- [ ] No single-pixel noise clusters after cleanup.
- [ ] Direction variants consistent (same species recognizable).
- [ ] Animation frames stable (no popping / large pixel jitter).

---

## Summary

To reliably generate realistic forms:
1) **Choose a template** (growth form/body plan) conditioned on environment.
2) **Generate base structure** as a graph/skeleton with strict constraints.
3) **Add secondary attachments** with ecological rules.
4) **Render with pixel-art rules** (ramps, stamps, outlines, cleanup).
5) **Animate by transforming structure**, not rotating final pixels.
6) **Validate and prune** every output.

This produces consistent, believable procedural plants/animals suitable for an evolution sandbox.