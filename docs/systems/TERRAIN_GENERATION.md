# Terrain Generation System

**Implementation:** `sandboxes/worldgen/`

## Overview

The terrain generation system produces procedural 2D tile-based worlds for the Darwin Chronicles simulation. It generates continental landmasses with realistic coastlines, inland lakes, mountain ranges, soil properties, and derived spatial fields — all from a single seed and a small set of tunable environment parameters.

The system runs as a standalone sandbox (`sandbox_worldgen`) with an interactive SDL2 viewer for real-time parameter tweaking, overlay visualization, and export. It does not link any `darwin::*` modules; it uses only SDL2 and FastNoiseLite.

## Generation Modes

Two generation modes are selected by `EnvParams::plate_count`:

- **Tectonic mode** (`plate_count > 0`, default for all presets): Simulates tectonic plates to produce structurally realistic mountain chains, continental shields, and ocean ridges. See tectonic pipeline below.
- **Legacy noise mode** (`plate_count == 0`): Pure fractal noise pipeline (the original approach). Available for comparison.

## Tectonic Pipeline (Default)

When `plate_count > 0`, stages 1–10 are the tectonic-specific pipeline. Stages after stage 10 are shared between both modes.

### Stage 1: Plate Generation

`plate_count` plates (clamped to 4–30) are created with seeded random centers (jittered 10–90% of map extent to avoid edge clustering), velocity vectors (angle uniform on [0, 2π], speed 1–4), and type (continental or oceanic) sampled from `continental_ratio`. At least 2 continental plates are guaranteed.

Each continental plate also computes a **spine direction** perpendicular to its velocity vector (like the Andes running perpendicular to the Nazca plate's subduction direction). The spine stores `spine_dx`, `spine_dy` (normalized direction) and `spine_offset` (random position variation ±15 tiles). For plates with negligible velocity, a random spine direction is assigned. This is used in Stage 3 to create interior mountain ridges.

After initial assignment, velocities are biased to simulate realistic plate dynamics:
- **Continental plates** receive a bias vector pointing toward the map center (strength 1.8), simulating supercontinent assembly. This concentrates mountain ranges toward the interior.
- **Oceanic plates** receive a bias vector pointing away from the map center (strength 0.6), simulating seafloor spreading. This enhances mid-ocean ridge formation.

### Stage 2: Domain-Warped Voronoi

Two layers of domain warp are applied to coordinates before the Voronoi plate lookup, curving all plate boundaries:

| Layer | Seeds | Amplitude | Frequency | Purpose |
|---|---|---|---|---|
| WARP1 | seed+10000, seed+11000 | 65 tiles | 0.004 | Large-scale continental shaping |
| WARP2 | seed+12000, seed+13000 | 25 tiles | 0.010 | Boundary detail and irregularity |

After warping, each tile finds its closest and second-closest plates by Euclidean distance. The following per-tile fields are stored for stage 3:
- `boundary_dist` = `dist_second − dist_closest` plus moderate FBm noise (from warp1a/warp2a layers at 0.010 and 0.022 freq, amplitude ±5 and ±2 tiles). The noise perturbs the Voronoi edge locally so boundaries appear wavy rather than linear.
- `boundary_nx/ny` = unit vector from plate A center to plate B center (boundary normal), then **rotated** by a per-tile noise-based angle (up to ~35° total from two noise layers at 0.007 and 0.020 freq). This breaks straight-line alignment along plate boundaries without destroying structural coherence.
- `boundary_tx/ty` = 90°-rotated boundary normal (boundary tangent, points along the boundary)
- `convergence_field` = `(rel_vx × bnx + rel_vy × bny) / 8.0`, clamped to [−1, 1] (positive = convergent, negative = divergent)

### Stage 3: Continuous Tectonic Fields

Four overlapping contributions are combined to produce the final height:

**Macro — continuous continental field**:
- Continent shapes are derived from a blended *continentality* score, not directly from plate polygon membership. This causes coastlines to diverge from Voronoi cell edges.
- `continentality = blended_cont × 0.55 + crust × 0.25 + cont_warp × 0.20`
  - `blended_cont`: plate type (1.0 continental / 0.0 oceanic) smoothly blended across plate boundaries with Gaussian falloff `exp(−bd²/35²)`
  - `crust`: `crust_noise` (FBm, 4 octaves, 0.006 freq, seed+3000) — large-amplitude variation that breaks plate polygon shapes
  - `cont_warp`: `cont_warp_noise` (FBm, 3 octaves, 0.008 freq, seed+4000) — independent deformation of continent edges
- Base elevation mapped from continentality: above the threshold → land `[water_level+0.04, CONT_BASE+0.05]`; below → ocean `[OCEAN_BASE, water_level−0.06]`
- **Province/craton mask**: a per-tile tectonic activity score is derived from `convergence_field` and proximity to plate boundaries. Active zones (convergent belts, young collision boundaries) receive an activity score near 1.0; stable cratons (old continental interiors far from boundaries) receive near 0.0. This score gates `meso_ridge_noise`, `terrain_var_noise`, and the 6-octave micro-detail noise — noise amplitudes are multiplied by the mask so that stable platform interiors remain smooth while active margins are textured. Prevents fine-grained noise from washing out continent-scale structural signals.
- **Continental shield uplift**: for tiles on continental plates, a broad Gaussian `0.06 × exp(−dist_center² / 90²)` raises the plate interior
- **Continental spine**: each continental plate has a spine (a directional ridge through its interior, perpendicular to plate drift). Per-tile spine distance is measured from a line through the plate center in the `spine_dx/spine_dy` direction, offset by `spine_offset`. Uplift = `0.15 × ridge_strength × Gaussian(spine_dist, σ=40) × ridge_mod × center_fade` where `ridge_mod` is **along-strike fragmentation noise** sampled along the spine axis (produces distinct peaks and saddles rather than a uniform ridge — simulates segmented ranges), and `center_fade = exp(−dist_center²/100²)` attenuates at plate margins. Spine Gaussian widened from σ=25 → σ=40 for broader interior ranges. Simulates continental spine ranges (Andes, Rockies).
- **Meso-scale ridges**: `meso_ridge_noise` (ridged fractal, 3 octaves, 0.012 freq, seed+8000) creates interior escarpments and secondary ridge arcs within continental interiors. Contribution = `max(0, meso) × 0.08 × center_fade × province_mask` — scaled by province mask so cratons receive minimal meso structure.
- **Large-scale terrain variation**: `terrain_var_noise` (FBm, 3 octaves, 0.004 freq, seed+6000) adds ±0.12 on land × province_mask, ±0.04 on ocean — breaks flat continental interiors into rolling hills and plains
- **Basin depressions**: `basin_noise` (FBm, 3 octaves, 0.012 freq, seed+7000) carves inland features on continental tiles:
  - Strong negative values (`< −0.20`) create depressions below water level; depth scales with distance from plate boundary (deeper inland), max depression strength 0.14
  - Moderate negative values (`−0.20 to 0.00`) create shallow valleys (−0.04 × basin_val)
- Coastlines emerge naturally from the continuous continental field and domain warping — **no separate coastline noise layer is applied**.

**Meso — boundary stress**:

Anisotropic ridged noise is sampled in boundary-aligned coordinates (tangent direction = along-range axis, normal direction = across-range axis) with a 5:1 stretch ratio to create elongated mountain ranges. A second cross-noise layer at 45° prevents excessive regularity. `belt_var` (low-frequency modulation along the range) varies mountain height.

| Boundary type | Convergence | Height effect |
|---|---|---|
| Continent-continent | > 0.05 | Wide plateau: `MOUNTAIN_PEAK × 0.8 × strength × exp(−bd² / PLATEAU_WIDTH²)` + sharp **segmented** peaks: `MOUNTAIN_PEAK × strength × exp(−bd / MOUNTAIN_WIDTH) × ridge_mod` where `belt_var` modulates `ridge_mod` with ±40% amplitude along range strike (creates massifs and saddles) + **foothill envelope**: `0.06 × strength × exp(−bd² / 90²)` + **foreland basin**: `−0.05 × strength × exp(−(bd−55)² / 20²)` behind the range |
| Ocean-continent | > 0.05 | Coastal arc: `MOUNTAIN_PEAK × 1.1 × strength × arc_falloff × ridge_mod` (arc_falloff = `exp(−offset_bd / MOUNTAIN_WIDTH)`) + **foothill envelope**: `0.05 × strength × exp(−offset_bd² / 80²)` + **back-arc basin**: `−0.06 × strength × exp(−(bd−45)² / 20²)` on the continental side + offshore trench: `−TRENCH_DEPTH × strength × exp(−bd / 5)` within 10 tiles of the oceanic side |
| Ocean-ocean | > 0.05 | Island arc: `MOUNTAIN_PEAK × 0.6 × strength × exp(−bd / (MOUNTAIN_WIDTH × 0.6)) × ridge_mod` |
| Continental rift | < −0.05 | Depression: `−RIFT_DEPTH × 1.5 × strength × exp(−bd / RIFT_WIDTH)` + rift shoulders: Gaussian uplift peaking at bd=15 |
| Mid-ocean ridge | < −0.05 | Ocean floor depression + central narrow uplift `0.06 × strength × exp(−bd / 3)` within 5 tiles |
| Mixed divergent | < −0.05 | Depression: `−RIFT_DEPTH × strength × exp(−bd / RIFT_WIDTH)` |
| Transform fault | −0.05 to 0.05 | Minor scarp `−0.03 × exp(−bd / 12)` + slight anisotropic ridging |

**Secondary ridge spurs**: when `boundary_uplift > 0.03`, higher-frequency ridged noise generates foothills/spur ridges in an annular zone between `MOUNTAIN_WIDTH × 0.4` and `MOUNTAIN_WIDTH × 1.5` tiles from the boundary. Spurs have amplitude up to 30% of the main boundary uplift.

**Micro — local detail**:
6-octave FBm detail noise (seed+0, freq=`noise_scale`) with amplitude `clamp((elev_so_far − 0.2) × 2.5, 0.02, 0.14) × province_mask` — detail is richer on elevated active zones, suppressed over stable cratons, near-zero underwater.

**Edge falloff**: `clamp((edge_dist − 0.70) / 0.30, 0, 1) × 0.4` pushes map borders toward ocean.

**New noise generators used in stage 3 (added in round 5):**
- **Curl noise warp** (seed+26000/26001, OpenSimplex2 FBm, 2 octaves, 0.008 freq): two-component curl noise that warps the coordinate space used to sample ridge noise before boundary stress computation. Displacement up to ~22 tiles breaks straight ridge alignment, producing curved organic ridge geometry.
- **Lithology noise** (seed+27000, OpenSimplex2 FBm, 2 octaves, 0.005 freq): spatially varies the effective ridge frequency across the map, simulating regions with different rock types — hard basement rock produces close-spaced ridges, soft sediment produces wide-spaced rolling terrain.
- **Piedmont roughness** (seed+28000, OpenSimplex2 FBm, 3 octaves, 0.025 freq): applied at mountain-to-foothill margins, adds coarse roughness characteristic of real piedmont deposits and alluvial fans.
- **Multi-direction ridge blending**: boundary ridge noise is now blended from three components — 40% sampled in the primary boundary-tangent direction, 25% sampled at a 30° rotation, and 35% isotropic (frequency-scaled). This prevents ridges from perfectly aligning parallel to plate boundaries on every collision.

**Step 3b — Three-scale relief domain warping** (inserted after stage 3 height assembly): a three-scale domain warp is applied to the full height field before erosion to break any remaining linear tectonic artifacts. Three FBm warp layers (12-tile, 5-tile, and 2-tile displacement at progressively higher frequencies) deform the height field at continental, regional, and local scales respectively.

### Stage 4 (Tectonic only): Material-Conserving Thermal Erosion

6 iterations of thermal erosion (with gentler parameters than previously) soften angular tectonic features into geological forms. Material is conserved: eroded from a source cell and deposited to its lower neighbors.
- **Erosion**: for each non-border tile, excess slope above `MAX_STABLE_SLOPE = 0.055` is computed for all 8 neighbors. Effective transfer rate = `THERMAL_RATE × (1 − hardness)` where `THERMAL_RATE = 0.12` and `hardness = clamp((elev − 0.5) × 2, 0, 0.7)`. Transfer amount = `min(total_excess × rate, max_diff × 0.5)`.
- **Deposition**: eroded material is split proportionally across lower neighbors (weighted by their excess slope), so steeper drops receive more sediment. No material is destroyed.
- **Smoothing**: each iteration ends with a 15/85 blend of 4-neighbor Laplacian smoothing simulating sediment spreading.

The gentler thermal parameters (rate 0.12 vs 0.20, threshold 0.055 vs 0.05) complement the more powerful stream-power erosion in stage 5, preventing over-smoothing while still removing the sharpest tectonic edges.

### Stage 4b (Tectonic only): Geological Depression Formation

Three physically-justified processes create natural lake basins with distinct morphologies. This stage runs before stream-power erosion so drainage networks develop realistic outlets from these basins. All depressions are clamped at `water_level − 0.01` to allow sub-sea-level features (analogous to the Dead Sea) without creating spurious ocean tiles.

**Process 1 — Tectonic Grabens**: rift basins form at divergent plate boundaries where crust pulls apart.
- Only applied within the rift corridor: `convergence_field < −0.1` and `boundary_dist ≤ 12` tiles.
- Subsidence is Gaussian across the corridor (σ=6 tiles) × segmentation noise along the boundary tangent (breaks the continuous rift into discrete en echelon basins) × irregular edge noise.
- Maximum subsidence 0.08 elev01 (~580 m). Analogues: East African Rift lakes (Tanganyika, Malawi, Turkana).

**Process 2 — Glacial Scouring**: a per-tile glaciation potential field (latitude ramp 0.45→0.75, elevation ramp water+0.15→water+0.40; combined `lat_factor × 0.7 + elev_factor × 0.5`) gates two sub-processes:

- **2a — Overdeepened trough basins**: jittered-grid candidates weighted by glaciation potential. Each accepted candidate stamps a slope-aligned elliptical basin (8–20 tile major axis / 2–6 tile minor axis) with smoothstep bowl depth 0.02–0.07 elev01. A rock-lip sill is raised at the down-valley outlet end (25% of bowl depth). Produces finger-lake geometry (Great Lakes, Scandinavian and Patagonian lakes).
- **2b — Kettle fields**: cluster centers on flat outwash terrain (local 7×7 relief < 0.03) each spawn 3–8 circular depressions (radius 1.5–4 tiles, depth 0.005–0.020), weighted by local glaciation potential. Mimics Minnesota/Manitoba pockmarked lake districts left by melting ice blocks.

**Process 3 — Volcanic Calderas**: collapsed volcano craters near convergent arcs.
- Placement criteria: `convergence_field > 0.15` (volcanic arc zone), `boundary_dist` 3–25 tiles, elevation ≥ `water_level + 0.10`.
- Up to 6 calderas per world (deterministic hash placement, probability ∝ convergence strength).
- Slightly elliptical (4–9 tile radius), smoothstep bowl depth 0.03–0.08, with a raised rim annulus (30–70% of bowl depth) extending to 1.3× the crater radius. Analogues: Crater Lake (Oregon), Toba (Sumatra), Pinatubo (Philippines).

**Interaction with downstream stages**: these basins survive thermal erosion (stage 4) and stream-power erosion (stage 5) with their depressions intact, since erosion only removes material from high points. The weather simulation's basin spillway storage system then fills them when precipitation accumulates, producing 10–60 persistent lakes per preset depending on terrain geometry.

### Stage 5 (Tectonic only): Stream-Power Erosion with Sediment Capacity

40 iterations of stream-power law erosion replace the previous simple hydraulic erosion for more realistic valley-carving behavior:
- **Flow accumulation**: D8 steepest-descent flow directions computed; upstream flow counts propagated from highest to lowest tile (height-sorted order). This is repeated every iteration to update flow paths as terrain evolves.
- **Valley carving**: each land tile loses height proportional to `stream_power = flow × slope`, modulated by rock hardness `clamp((elev − 0.65) × 4, 0, 0.8)`. High-elevation tiles with large flow accumulation receive a **mountain canyon boost** (2× carving) to produce dramatic gorge features.
- **Sediment capacity and deposition**: each tile computes a sediment transport capacity proportional to stream power. When a tile's transported sediment exceeds capacity (stream loses power — e.g., valley widens or slope decreases), excess sediment is deposited to the downhill neighbor, building alluvial fans and valley floors. Unlike the previous fixed-fraction deposition, capacity-limited transport produces more realistic depositional patterns.
- **Effect**: 40 short iterations produce incremental but cumulative valley incision with realistic graded longitudinal profiles. Mountain canyons, piedmont fans, and graded river valleys emerge from the iterative process.

### Stage 5b (Tectonic only): Valley-Floor Smoothing

After stream-power erosion, river valleys have been carved to varying depths. This stage smooths valley floors without affecting ridges or mountain terrain:
- **Target selection**: tiles above water level but below `water_level + 0.15`. Smoothing strength is now **scaled by elevation** so high-elevation terrain receives near-zero smoothing — mountain structure is preserved while only low-elevation valley floors are affected.
- **Low-pass filter**: a weighted average of the tile and its lower neighbors smooths the floor without raising ridges or peaks. Ridges (tiles higher than most neighbors) are explicitly excluded.
- **Effect**: river valleys gain flatter floors, depositional plains form naturally at valley mouths, and V-valley edges soften into U-profiles. Mountain detail is unchanged.

### Stage 5d (Tectonic only): Valley Drowning

After stream-power erosion and valley-floor smoothing, low-lying river valleys near the coast are selectively drowned to create rias, estuaries, and fjords:
- **Flow accumulation threshold**: only valleys with significant upstream drainage (in-pipeline flow accumulation computed during stage 5 stream-power erosion, above a per-tile threshold) are eligible for drowning. This ensures only true river corridors are affected, not every low point. Note: this flow accumulation is a transient value within the generation pipeline — it is not stored in `TerrainTile`. The `river_flow` field was removed from the terrain data model; rivers emerge dynamically in the weather simulation.
- **Regional sea-level anomaly**: a low-frequency noise field (different seed from terrain) creates spatial variation in local base sea level, so drowning is more pronounced in some coastal regions than others.
- **Fjord detection**: tiles with high local relief and high latitude score receive preferential drowning — the combination of deep valleys + polar latitudes produces fjord geometry. Lower-latitude coasts with lower relief instead produce ria (drowned river valley) and estuary geometry.
- **Effect**: dendritic drowned-valley inlets extend inland from the coast, visually distinguishing coastal morphology between sheltered fjord coastlines (high-latitude, high-relief) and subtropical ria coasts.

### Stage 6 (Tectonic only): Distance-from-Coast Inland Elevation Bias

Applies a gentle height boost to inland land tiles to bias mountain formation away from coastlines:
- **BFS from coastal tiles**: all land tiles adjacent to at least one water tile are seeded at distance 1. BFS expands inland assigning integer distances to each land tile.
- **Exponential saturation uplift**: `bias = INLAND_BIAS_STRENGTH × (1 − exp(−dist / INLAND_BIAS_HALFLIFE))`. At distance 0 (coastal tiles) no uplift is applied; the function saturates at `INLAND_BIAS_STRENGTH = 0.12` for large distances. Half-life is `INLAND_BIAS_HALFLIFE = 15` tiles.
- **Effect**: mountains that formed near coastlines in stage 3 are pushed up and inland. Coastal lowlands are relatively suppressed.

### Stage 6b (Tectonic only): Noise-Warped Coastal Plain Enforcement

Real coastlines typically have a zone of flat lowland between the sea and the interior uplands. This stage enforces that pattern:
- **Band width per tile**: a noise-warped function determines each coastal tile's target lowland width. The nominal band is 1.5–7 tiles wide; FBm warp noise displaces the band boundary organically, so the coastal plain narrows and widens irregularly rather than being a uniform shelf.
- **Slope gate**: tiles whose slope exceeds a threshold (cliff and fjord coasts) are excluded — steep coasts remain steep.
- **Lowering**: tiles inside the band whose elevation is above the target coastal plain height are lowered to match, forming a flat or gently sloping lowland zone.
- **Effect**: continental coasts gain a natural coastal plain that transitions into upland. The irregular band width prevents the artificial "ring" appearance of a uniform nearshore zone.

### Stage 7 (Tectonic only): Exposure-Based SDF Coastal Remodeling

Previous uniform coastline fractalization replaced with a physically motivated process that distinguishes exposed and sheltered coasts:

**Stage 7a — Coastline Signed Distance Field**: BFS from the water/land boundary assigns each land and water tile a signed distance to the coastline. Used as the foundation for all subsequent coastal shaping.

**Stage 7b — Wave Exposure Computation**: 16-direction fetch raymarching (max 60 tiles per direction) measures how much open ocean each coastal tile "sees" in each direction. Tiles facing open ocean receive high exposure scores; tiles tucked into bays or behind headlands receive low scores. A 3-pass Gaussian blur smooths the raw exposure field and the coastal slope proxy to remove single-tile artifacts.

**Stage 7c — Coastal Slope Proxy**: slope magnitude near the coast is used to distinguish cliff faces (high slope → excluded from reshaping) from gentle lowland coasts (low slope → reshaped freely). This gate ensures steep fjord walls and sea cliffs are not softened by coastal displacement.

**Stage 7d — Exposure-Driven Anisotropic Displacement**: elevation adjustments are applied based on each tile's exposure score using spectral control:
- **Sheltered coasts** (low exposure — bays, inlets, fjord heads): high-frequency noise suppressed 90%, mid-frequency 70%, broad-frequency 30%. Results in smooth, calm bay shores.
- **Exposed coasts** (high exposure — headlands, open ocean shore): full noise amplitude applied, producing irregular cliffed or rocky profiles.
- A gated coastal profile ramp (1–3 tile width, 50% blend) applies a smooth transition from the coastal elevation to the inland terrain while respecting detected cliff geometry.

**Stage 7b (new) — Depositional Coastal Features**:
- **River-mouth deltas**: coastal tiles at the mouth of high-flow rivers receive fan-shaped lobe deposits with subtle elevation buildup. Distributary channels are carved through the delta surface, mimicking braided delta geometry.
- **Barrier islands**: on moderate-exposure coasts, a narrow offshore ridge is raised just beyond the shoreline with tidal inlet gaps at intervals, producing barrier island / lagoon morphology.

**Stage 7c (new) — Clustered Skerry Generation**: exposed headlands (high-exposure coastal land tiles) seed Poisson-distributed cluster centers with a minimum 8-tile spacing. Each cluster center spawns a field of small rocky islets within a 3–6 tile radius, using jittered noise to determine per-candidate elevation. Creates realistic skerry archipelagos off exposed capes without uniform random scatter.

**Stage 7d (new) — Sub-Bay Inlet Carving**: low-exposure concave coastal sections (sheltered bays) are candidates for inlet carving. From the bay coastline, steepest-ascent tracing proceeds inland for up to 8 tiles, slightly lowering the traced path to carve a narrow inlet or estuary recess. Produces fjord-like incisions in sheltered bays without requiring them to have pre-existing drainage.

### Stage 8 (Tectonic only): Basin Flood-Fill Lakes

Generates realistic inland lakes by detecting terrain basins and flooding them to their spill point:
- **Local minima detection**: scans all land tiles between `water_level` and `water_level + 0.30` for cells lower than all 8 neighbors. Up to `MAX_BASINS = 200` basins are retained (sorted lowest-first to fill deepest depressions first).
- **Priority-queue flood fill**: each basin expands via a min-heap, adding cells by height. The fill level rises to match each new cell. Expansion stops when the fill level exceeds `basin.min_height + LAKE_DEPTH_MAX` (spill found) or the basin drains into an already-water tile (ocean-connected, aborted). Max 500 cells per basin prevents runaway on flat terrain.
- **Lake floor assignment**: filled cells below the spill level are set to a depth-graded lake floor: `water_level − 0.02 − depth_frac × 0.04`, ranging from −0.02 (shallow edges) to −0.06 (deep center).
- **Minimum size filter**: basins smaller than `MIN_LAKE_SIZE = 4` tiles are discarded, preventing single-tile puddles.
- These lake tiles are detected as inland lakes by the shared `compute_ocean_lake` stage.

### Stage 9 (Tectonic only): Water Speckle Removal

Cleans up isolated noise-induced water pixels that would create artifacts in downstream stages:
- **Connected-component labeling**: all tiles below `water_level` are labeled by 8-connectivity BFS.
- **Small-component removal**: any water component with fewer than 3 tiles is raised to `water_level + 0.01`, converting it back to land.
- This prevents the ocean/lake flood-fill stage from classifying isolated noise pixels as lakes, and avoids spurious distance-field and flow accumulation artifacts.

### Stage 9b (Tectonic only): Debris Islet Removal

Removes land bodies too small to be meaningful coastal features (smaller than the speckle threshold in stage 9):
- **Land connected-component labeling**: all land tiles are labeled by 8-connectivity BFS, separate from the water pass.
- **Tiny island removal**: land components with fewer than 8 tiles are lowered to below `water_level`, converting them to water. This threshold is larger than the stage 9 water speckle threshold (3 tiles) to catch slightly larger but still degenerate debris fragments that stage 7c skerry generation or coastal displacement may have introduced.
- **Effect**: prevents single-tile or 2–7 tile land fragments from appearing as isolated debris in ocean areas, keeping the map visually clean.

### Stage 9c (Tectonic only): Morphological Narrow Feature Cleanup

Removes degenerate narrow coastal geometry that coastal processing stages may produce:
- **Neck removal**: 1-tile-wide land necks connecting two larger land bodies across an otherwise water-filled gap are removed (the neck tile is converted to water) to prevent unrealistic isthmus structures thinner than a single tile.
- **Corner bridge removal**: tiles that connect two land areas only diagonally (4-connected water isolation) are converted to water, eliminating diagonal-only "bridges" that produce jagged staircase coastlines.
- **Peninsula tip pruning**: peninsula tips reduced to a single tile surrounded by water on 3+ sides are eligible for removal if they would otherwise cause extreme pointedness without enough upstream drainage to justify a headland.
- **Effect**: coastal geometry is cleaner and avoids degenerate topology artifacts introduced by the interplay of flood-fill, coastal displacement, and skerry generation.

### Stage 10 (Tectonic only): SDF Curvature Flow Coastline Smoothing

Previous Laplacian smoothing of the coastal plain replaced with a more physically motivated SDF curvature flow:
- **Coastal band restriction**: only tiles within 3 tiles of the coastline (measured by stage 7a's signed distance field) participate in the smoothing pass. Interior terrain and deep ocean are untouched.
- **Curvature flow**: 4 iterations of mean-curvature flow (dt=0.18 per iteration) applied to the coastal height field. Curvature flow moves the coastline in the direction that minimizes local boundary length — convex peninsulas shrink slightly, concave bays fill slightly, producing organic rounded coastline geometry.
- **Effect**: coastlines gain smooth, anti-aliased curves that look natural rather than jagged or staircased. The curvature flow approach preserves large-scale coastal geometry (headlands, bays remain where they were placed) while only rounding sharp corners at the individual-tile scale. Produces visually cleaner results than Laplacian smoothing, which tended to uniformly shrink all coastal features.

Key constants: `CONT_BASE = max(0.58, water_level + 0.12)`, `OCEAN_BASE = 0.22`, `MOUNTAIN_WIDTH = 35`, `MOUNTAIN_PEAK = 0.75` (boosted from 0.60 for more dramatic mountain relief), `RIFT_WIDTH = 20`, `RIFT_DEPTH = 0.10`, `PLATEAU_WIDTH = 65`, `TRENCH_DEPTH = 0.06`, `THERMAL_ITERS = 6`, `THERMAL_RATE = 0.12`, `MAX_STABLE_SLOPE = 0.055`, `STREAM_POWER_ITERS = 40`, `INLAND_BIAS_STRENGTH = 0.12`, `INLAND_BIAS_HALFLIFE = 15`, `LAKE_DEPTH_MAX = 0.08`, `MIN_LAKE_SIZE = 4`, `MAX_BASINS = 200`, `WARP1_STRENGTH = 65`, `WARP2_STRENGTH = 25`. Spine Gaussian σ=40 tiles. Spine uplift strength: `0.15 × ridge_strength`. Meso ridge contribution: up to 0.08 per tile × province_mask. terrain_var_noise land scale: 0.12 × province_mask. basin_noise depression strength: up to 0.14. Ridge coordinate warp: curl noise displacement up to ~22 tiles (seed+26000/26001).

## Legacy Noise Pipeline

When `plate_count == 0`, stages 1–3 use fractal noise:

| Stage | Timing Key | Description |
|---|---|---|
| **1. Height Field** | `height_ms` | Domain-warped continental noise. Two Perlin layers (seed +10000/+11000) warp input coordinates by up to 20 tiles. Continental shape, coastline detail, and lake carving noises applied. Edge falloff pushes borders toward ocean. |
| **2. Ridge Field** | `ridge_ms` | Ridged multifractal noise (`1 − |noise|`) for mountain ranges. Region mask restricts mountains to distinct zones. Ridges suppressed near coastlines. Kept as `TerrainTile::elevation` for renderer decoration. |
| **3. Unify Height** | `unify_ms` | Ridge field merged into continental height at scale ~0.18. |

## Shared Pipeline (Post-Height Stages)

Both modes share these stages after their mode-specific height generation and erosion passes. For tectonic mode, all 10 tectonic-specific stages above run before these shared stages.

**Design principle**: terrain generation produces the geological substrate. Hydrology (rivers, lakes, flow accumulation) is no longer computed at terrain-gen time — it emerges dynamically from precipitation interacting with the geological substrate in downstream simulations (see weather sandbox dynamics).

| Stage | Timing Key | Description |
|---|---|---|
| **Ocean Flood-Fill** | `ocean_ms` | BFS on all tiles below `water_level`. Components touching a map edge with ≥ 200 tiles become ocean. Lake classification removed — lakes form dynamically. |
| **Slope, Aspect, Elevation Bands** | `slope_band_ms` | Central-difference gradients on unified height → `slope01`. Aspect = `atan2(dy, dx)`. Elevation bands classified by **local relief** (max−min in R=4 tile neighborhood) and slope: Mountains = relief > 0.22 AND slope > 0.18; Hills = relief > 0.12 OR (slope > 0.12 AND elevation > water_level + 0.12); Lowland = everything else above water. |
| **Euclidean Distance Transform** | `dist_fields_ms` | Felzenszwalb separable parabola-envelope EDT for `dist_ocean` only. `dist_water` removed (no lake classification). |
| **Roughness** | `roughness_ms` | Standard deviation of heights in 3×3 neighborhood × 20.0. |
| **Geology** | `geology_ms` | `compute_geology()` assigns rock type and soil properties based on tectonic context (see Geology Stage below). |
| **Tile Assembly** | — | Assembles all fields into the flat `TerrainTile` array. |

**Removed from the shared pipeline**: `is_lake`, `dist_water`, soil fertility/hold fields, D8 downhill routing, flow accumulation, `river_flow`, `downhill_x/y`.

### Geology Stage

`compute_geology()` assigns a `RockType` and `SoilTexture` to each tile based on tectonic context, elevation, and slope. Derived hydraulic parameters physically link the bedrock and soil to downstream hydrological behavior.

**Rock type assignment**:
- Convergent mountain belts (high elevation + high slope near convergent boundaries) → Metamorphic
- Volcanic arcs (ocean-continent subduction zones, basaltic ocean floor) → Basalt
- Stable continental shields and high cratons → Granite
- Sedimentary basins, continental interiors at moderate elevation → Sandstone or Limestone (karst-prone)
- Low-elevation coastal plains and back-arc basins → Shale

**Soil texture assignment**:
- Exposed bedrock on steep slopes → Sand (coarse, low retention)
- Mountain footslopes and floodplains → Loam (balanced drainage)
- Low-relief inland plains → Silt or Clay (fine-grained, high retention)
- Coastal wetlands and peat-prone latitudes → Peat (high porosity, waterlogged)

**Derived hydraulic parameters** (stored in `TerrainTile`):

| Field | Unit | Description |
|---|---|---|
| `bedrock_hardness` | [0..1] | Resistance to erosion and incision. Granite=0.85, Basalt=0.70, Limestone=0.50, Sandstone=0.45, Shale=0.30, Metamorphic=0.90. |
| `bedrock_permeability` | [0..1] | Deep infiltration potential below the soil zone. Limestone=0.60, Sandstone=0.55, Basalt=0.40, Granite=0.15, Shale=0.05, Metamorphic=0.10. |
| `soil_depth` | m | Soil column depth [0..3]. Thick in valleys/plains, thin on steep rocky slopes. |
| `porosity` | fraction | Void fraction of the soil [0.25..0.65]. Peat=0.65, Sand=0.38, Loam=0.45, Silt=0.48, Clay=0.50. |
| `ksat` | m/s | Saturated hydraulic conductivity. Sand≈50×10⁻⁶, Loam≈5×10⁻⁶, Silt≈1×10⁻⁶, Clay≈0.1×10⁻⁶. Controls infiltration rate in dynamics. |
| `field_capacity` | vol. fraction | Soil water content at field capacity (drainage ceases). Sand=0.10, Loam=0.30, Silt=0.33, Clay=0.42, Peat=0.55. |
| `wilting_point` | vol. fraction | Soil water content below which plants cannot extract water. Sand=0.04, Loam=0.12, Clay=0.20. |
| `erodibility` | [0..1] | Susceptibility to erosion. High for Shale/Silt, low for Granite/Metamorphic. |

## World Data Model

The world is represented as a `Terrain` struct containing a flat vector of `TerrainTile` values indexed by `(y * width + x)`.

### TerrainTile Fields

**Geometry / Classification**

| Field | Type | Range | Description |
|---|---|---|---|
| `elev01` | float | 0.0 – 1.0 | Unified height (continent + merged ridge). Values below the water level are underwater. |
| `band` | ElevBand | enum | Elevation classification: Water, Lowland, Hills, or Mountains. Derived from `elev01`. |
| `is_ocean` | bool | — | True if this tile is part of a large edge-connected water body. |
| `slope01` | float | 0.0 – 1.0 | Terrain gradient magnitude from central differences on unified height, scaled by 10×. |
| `aspect` | float | -π – π | Slope direction angle: `atan2(dy, dx)`. Points in the direction of steepest ascent. |
| `dist_ocean` | float | 0.0 – N | Euclidean distance (tiles) to the nearest ocean tile. |
| `roughness` | float | 0.0 – 1.0 | Local height variance (3×3 stddev × 20). Drives terrain decoration. |
| `elevation` | float | 0.0 – 1.0 | Ridge-only component retained for renderer decoration. Peaks on mountain ridges, zero on water. |

**Bedrock Geology**

| Field | Type | Range | Description |
|---|---|---|---|
| `rock` | RockType | enum | Bedrock classification: Granite, Basalt, Limestone, Sandstone, Shale, Metamorphic. |
| `bedrock_hardness` | float | 0.0 – 1.0 | Resistance to erosion and incision. |
| `bedrock_permeability` | float | 0.0 – 1.0 | Deep infiltration potential below the soil zone. |

**Regolith / Soil (Root Zone)**

| Field | Type | Range | Description |
|---|---|---|---|
| `soil` | SoilTexture | enum | Soil texture class: Sand, Loam, Silt, Clay, Peat. |
| `soil_depth` | float | 0.0 – 3.0 m | Soil column depth. Thick in valleys and plains, thin on steep rocky slopes. |
| `porosity` | float | 0.25 – 0.65 | Void fraction of the soil column. |
| `ksat` | float | m/s | Saturated hydraulic conductivity. Controls infiltration rate in dynamic simulation. |
| `field_capacity` | float | vol. fraction | Volumetric water content when gravitational drainage ceases. |
| `wilting_point` | float | vol. fraction | Volumetric water content below which plants cannot extract water. |
| `erodibility` | float | 0.0 – 1.0 | Susceptibility to erosion. High for Shale/Silt, low for Granite/Metamorphic. |

**Removed fields** (hydrology is now fully dynamic): `is_lake`, `dist_water`, `river_flow`, `soil_fertility`, `soil_hold`, `downhill_x`, `downhill_y`.

### Environment Parameters (EnvParams)

| Parameter | Default | Range | Description |
|---|---|---|---|
| `water_level` | 0.45 | 0.0 – 0.8 | Height threshold below which tiles are classified as water. Higher values produce more ocean. |
| `noise_scale` | 0.02 | 0.001 – 0.1 | Frequency multiplier for terrain detail noise (both modes). |
| `continent_threshold` | 0.55 | 0.0 – 1.0 | Continental shape bias (legacy noise mode only). |
| `ridge_strength` | 1.0 | 0.0 – 3.0 | Multiplier for mountain strength. In tectonic mode: scales convergent boundary uplift. In legacy mode: scales the ridge noise field. |
| `plate_count` | 12 | 0 – 30 | Number of tectonic plates. 0 selects legacy noise mode. Values clamped to 4–30 when > 0. |
| `continental_ratio` | 0.40 | 0.0 – 1.0 | Fraction of plates that are continental (tectonic mode only). Continental plates sit higher (base 0.55 vs 0.30) and produce taller mountains at continental-continental collisions. |

## World Presets

Each preset provides a curated set of environment parameters. All presets use tectonic mode (`plate_count > 0`).

| Preset | Water Level | Noise Scale | Ridge Strength | Plate Count | Continental Ratio | Character |
|---|---|---|---|---|---|---|
| **earth** | 0.45 | 0.02 | 1.0 | 12 | 0.40 | Balanced land/water with moderate mountain chains. Default preset. |
| **venus** | 0.35 | 0.02 | 1.4 | 8 | 0.60 | Low water, consolidated continents with strong collision ranges. |
| **mars** | 0.30 | 0.02 | 0.8 | 10 | 0.50 | Very low water, broad mixed landmass with subdued relief. |
| **snowball** | 0.55 | 0.02 | 0.6 | 14 | 0.30 | High water producing scattered islands with gentle terrain. |
| **tropical** | 0.45 | 0.025 | 1.2 | 10 | 0.45 | Earth-like with finer detail and stronger collision ridges. |
| **toxic** | 0.40 | 0.02 | 1.5 | 16 | 0.35 | Many small plates produce fragmented terrain with tall ridge belts. |

Presets are selected via the `--preset` CLI flag. Individual parameters can be overridden with `--water-level`, `--continent-bias`, and `--ridge-strength`.

## Interactive Controls

### Camera

| Input | Action |
|---|---|
| W / Up | Pan up |
| S / Down | Pan down |
| A / Left | Pan left |
| D / Right | Pan right |
| Mouse wheel | Zoom in/out at cursor position (0.25× to 4.0×) |

### Overlays

| Key | Overlay | Color Mapping |
|---|---|---|
| 1 | None | Base terrain colors |
| 2 | Continental | Grayscale heightmap (black = low, white = high) |
| 3 | Slope | Black (flat) to yellow (steep) |
| 4 | ElevBand | Categorical: blue (Water), green (Lowland), gold (Hills), gray (Mountains) |
| 5 | DistOcean | Green (near ocean) to red (far from ocean) |
| 6 | Roughness | Black (smooth) to yellow (rough) |
| 7 | Aspect | Hue wheel by slope direction angle (-π to π) |
| 8 | Geology | Categorical by RockType: red (Granite), orange (Basalt), yellow (Limestone), tan (Sandstone), dark gray (Shale), purple (Metamorphic) |
| 9 | SoilTexture | Categorical by SoilTexture: sandy yellow (Sand), green-brown (Loam), gray-brown (Silt), blue-gray (Clay), dark brown (Peat) |

### Parameter Tweaking

| Key | Action |
|---|---|
| Tab | Cycle through tweakable parameters (Seed, WaterLevel, NoiseScale, ContinentThreshold, RidgeStrength) |
| ] | Increase active parameter by one step |
| [ | Decrease active parameter by one step |
| Shift + ] / [ | Increase/decrease by 10× step |

Parameter changes trigger an automatic world regeneration.

### Other Controls

| Key | Action |
|---|---|
| R | Regenerate world with current parameters |
| Space | Regenerate with a new random seed |
| G | Toggle grid overlay |
| F | Toggle FPS display in window title |
| L | Export generation log to `terraingen_log_<seed>.txt` |
| F12 | Save screenshot as BMP |
| Ctrl+F12 | Save full screenshot as BMP (alternate filename) |
| Escape | Quit |

The REGENERATE button in the top-right corner of the window also triggers regeneration with a new random seed.

On startup the camera is automatically fitted to show the entire world (`Camera::fit_world`) so the full map is visible without any panning or zooming.

## Rendering

### Base Terrain Colors

The renderer assigns colors based on tile classification and elevation:

| Tile Type | Color Scheme |
|---|---|
| Ocean | Dark-to-medium blue gradient by depth (deeper water is darker) |
| Lake | Light blue (fixed color) |
| Lowland | Tan/sandy tones, varying slightly with height |
| Hills | Olive-brown tones |
| Mountains | Gray-to-white gradient by elevation |

### Terrain Decoration

Land tiles receive visual decoration based on their roughness value:

| Roughness Range | Decoration |
|---|---|
| Below 0.15 | Flat — no decoration |
| 0.15 to 0.45 | Hills — two small triangular bumps |
| 0.45 to 0.65 | Mountains — a tall sharp triangle |
| 0.65 and above | Mountains with snow caps — a white triangle peak atop the mountain |

Decorations are drawn in a darker shade of the tile's base color. They only appear when the tile is rendered at a size of 4 pixels or larger.

### UI Elements

The renderer displays a terrain legend panel in the bottom-left corner showing elevation band distribution percentages. When an overlay is active, a corresponding overlay legend appears above the terrain legend, showing either a gradient bar with labels or categorical swatches depending on the overlay type. Each overlay legend also shows a short info description line explaining what the field represents and how it was computed.

The window title bar shows the current world dimensions, seed, tile-under-cursor details (elevation band, height, slope, ocean distance, soil fertility), active overlay name, and active tweak parameter with its value.

## Telemetry and Statistics

The telemetry system (`telemetry.h`, `telemetry.cpp`) computes and displays world statistics after each generation:

- Tile counts: total, ocean, lake, and land tiles with percentages
- Elevation band distribution (Water, Lowland, Hills, Mountains)
- Field statistics: min, max, mean, and standard deviation for height and slope; min, max, and mean for fertility, soil hold, and roughness
- Maximum distance values for ocean and water distance fields
- Maximum river flow value

Generation timings are recorded for each of the timed pipeline stages and printed to the console and the log file. `GenerationTimings` fields: `height_ms`, `ridge_ms`, `unify_ms`, `ocean_ms`, `slope_band_ms`, `dist_fields_ms`, `geology_ms`, `roughness_ms`, `total_ms`. (Removed: `ocean_lake_ms`, `soil_ms`, `downhill_ms`, `river_ms`.)

## CLI Usage

```
sandbox_worldgen [options]
  --seed N              World seed (default: 42)
  --width N             World width in tiles (default: 256)
  --height N            World height in tiles (default: 256)
  --preset NAME         Preset: earth, venus, mars, snowball, tropical, toxic
  --water-level F       Override water level [0.0, 0.8]
  --continent-bias F    Override continent threshold [0.0, 1.0]
  --ridge-strength F    Override ridge strength [0.0, 3.0]
  --headless            Generate terrain, render BMP, print stats, exit (no window)
  --help                Show help
```

All presets use tectonic mode by default. Legacy noise mode is not exposed via CLI flags; it requires setting `plate_count = 0` in `EnvParams` programmatically.

In headless mode, terrain is rendered at 1 pixel per tile with the following improved rendering pipeline:

**Hillshade**: NW directional lighting (azimuth 315°, altitude 40°), slope exaggeration `slope_mag × 6.0` for moderate photoreal shading without metallic over-amplification.

**Ocean**: depth mapped through `depth^0.6` gamma compression before applying a quadratic Bernstein color gradient (shallow: ~(25, 52, 95) → deep: ~(8, 22, 78)). Gamma compression causes the shelf-to-abyss transition to be smooth without a bright coastal halo ring. Very subtle depth-slope shading (clamped to 0.85–1.0) adds fine detail to shelf edges. **Tectonics-aware bathymetry**: river shelf boost (tiles adjacent to high river-flow land extend the shelf), margin-dependent shelf width (convergent margins narrower, passive margins wider), and convergent margin trench darkening.

**Lakes**: depth-based gradient — lighter blue at shores (shallower), progressively darker toward center.

**Land**: a climate-driven biome blending model computes weighted contributions from seven biome types (tundra, boreal, alpine, arid, steppe, tropical, temperate). Biome weights are derived from a per-tile climate model: temperature from latitude + elevation lapse rate; moisture from a composite of dist_ocean, latitude, slope (exposed rock = less green), continental drying (tiles > 25 tiles from ocean dry progressively), roughness and aspect variation, and soil proxy (ridges are bare, valleys are vegetated). Logistic functions with visible ecotone widths produce smooth biome transitions rather than hard cutoffs.

**Snow**: computed from temperature (latitude + lapse rate), slope steepness (steep faces shed snow), elevation ramp, pole-facing aspect preference (slope aspect relative to hemisphere), and moisture. Tint varies from blue-white (cold/dry) to warm-white (near-threshold temperature).

**Aspect-based albedo**: at elevation, sun-facing slopes receive a warm color shift; shadow-facing slopes shift cooler-darker. Adds material variety to mountain terrain.

**Rivers**: scaling width and intensity by a sqrt-based discharge fraction; brown sediment tint applied in low-elevation estuaries where rivers enter the sea; major trunks visually distinct from minor tributaries.

The output BMP is saved as `terrain_{preset}_{seed}.bmp` in the working directory.

## Integration Points

The worldgen sandbox serves as a prototype for terrain generation that feeds into downstream sandboxes:

- **Weather sandbox** (`sandboxes/weather/`): Contains its own copy of the world generation code (synced with worldgen changes) and layers atmospheric simulation on top — temperature, wind, precipitation, evaporation, and snow. The terrain's unified height, slope, elevation bands, and distance fields influence weather behavior (lapse rate, orographic uplift, rain shadows). Geology parameters (`ksat`, `field_capacity`, `bedrock_permeability`) directly govern infiltration rates, groundwater storage, and baseflow in the dynamic simulation. Surface water routing uses WSE multi-neighbor flow rather than the precomputed D8 downhill routing that was previously stored in TerrainTile.
- **Vegetation sandbox** (`sandboxes/veggen/`): Uses a spatial terrain model with elevation gradients, slope, and a two-layer moisture system (surface water and root moisture). Terrain properties determine where different plant species can establish and thrive.
- **Main simulation** (`src/world/`): The `darwin::world` module implements a Whittaker biome classification system that maps temperature and moisture conditions onto biome types. Terrain generation concepts prototyped in this sandbox inform the world module's grid and terrain design.

Each downstream system maintains its own terrain implementation rather than linking the sandbox code directly, following the sandbox convention of prototyping in isolation before promoting patterns into `darwin::*` modules.

## Source Files

| File | Purpose |
|---|---|
| `sandboxes/worldgen/terrain_gen.h` | `TerrainTile` (geometry + bedrock geology + soil fields), `Terrain`, `ElevBand`, `RockType`, `SoilTexture`, `GenerationTimings` definitions and `generate_terrain` declaration |
| `sandboxes/worldgen/terrain_gen.cpp` | All generation pipeline: `generate_tectonic_terrain` (domain-warped Voronoi with boundary distance noise + normal rotation, spine generation with σ=40 and along-strike fragmentation, curl noise ridge warp seed+26000/26001, lithology noise seed+27000, piedmont roughness seed+28000, multi-direction ridge blending, province/craton mask, continuous tectonic fields, along-strike belt segmentation, three-scale relief domain warp step 3b, thermal erosion 6 iters rate 0.12 threshold 0.055, stream-power erosion 40 iters with canyon boost and sediment capacity, valley-floor smoothing 5b with elevation-scaled strength, valley drowning 5d with flow accumulation + sea-level anomaly noise + fjord detection, distance-from-coast inland bias, noise-warped coastal plain enforcement 6b, exposure-based SDF coastal remodeling 7 with 16-direction fetch raymarching + anisotropic displacement + coastal profile ramp, depositional features 7b with river-mouth deltas and barrier islands, clustered skerry generation 7c with Poisson cluster centers and islet fields, sub-bay inlet carving 7d, basin flood-fill lakes, water speckle removal, debris islet removal 9b, morphological narrow feature cleanup 9c, SDF curvature flow coastline smoothing 10 with 4 iterations dt=0.18 in 3-tile coastal band), `generate_height_field` (legacy domain-warped noise), `generate_ridge_field` (legacy ridged noise), `unify_height_field` (legacy), shared stages (ocean BFS only — no lake classification, slope/aspect/bands with relief-based classification, EDT for dist_ocean only, roughness, `compute_geology` for RockType + SoilTexture + hydraulic parameters). `TectonicPlate` struct: `cx/cy`, `vx/vy`, `is_continental`, `spine_dx/spine_dy`, `spine_offset`. |
| `sandboxes/worldgen/main.cpp` | SDL2 initialization, event loop, parameter tweaking, screenshot/log export; headless BMP export mode with gamma-compressed ocean depth, climate-driven biome land coloring (7 biome types, logistic blending, latitude+lapse temp model, composite moisture), aspect-based snow and albedo variation, lake depth gradient, tectonics-aware bathymetry, and river discharge-hierarchy rendering. |
| `sandboxes/worldgen/config.h` | `EnvParams` (including `plate_count`, `continental_ratio`), `SandboxConfig`, and `Preset` structures |
| `sandboxes/worldgen/config.cpp` | Preset definitions with tectonic parameters, CLI argument parsing |
| `sandboxes/worldgen/renderer.h` | `Camera` and `Renderer` classes |
| `sandboxes/worldgen/renderer.cpp` | Camera transforms, terrain color mapping, tile rendering with elevation decoration |
| `sandboxes/worldgen/telemetry.h` | `OverlayMode` enum (11 modes), `TerrainStats`, `FPSCounter`, overlay/legend/button rendering declarations |
| `sandboxes/worldgen/telemetry.cpp` | Statistics computation, overlay color functions (including aspect and river flow), bitmap font, legend rendering with per-overlay info description text, UI buttons |
| `sandboxes/worldgen/main.cpp` | SDL2 initialization, event loop, parameter tweaking, screenshot/log export, headless BMP export mode |
| `sandboxes/worldgen/CMakeLists.txt` | Build configuration via `add_darwin_sandbox(worldgen)` |
