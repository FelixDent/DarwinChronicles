# Plant Generation System

**Implementation:** `sandboxes/veggen/`
**Visual testing:** `sandboxes/spritetest/`

## Overview

The plant generation system is a vegetation simulation that models plant growth, death, reproduction, and ecological competition across a spatial terrain grid. Plants are rendered as procedural pixel-art sprites whose appearance is driven by species traits, growth phase, and health status. The system runs as a standalone sandbox that does not link any `darwin::*` modules.

The simulation operates on a 3x3 tile grid. Each tile carries terrain properties (elevation, slope, soil), a two-layer moisture model, and seasonal climate variation. Eight plant archetypes compete for resources, with populations governed by biome-specific weights, density limits, and environmental suitability. Metrics collection tracks population dynamics, mortality breakdown, and ecological indicators over time.

---

## Terrain System

Terrain is generated once at initialization from a biome preset's base values, with per-tile variation added through elevation gradients, hash-based noise, and derived quantities.

### Per-Tile Spatial Variation

Each tile in the grid carries static terrain properties computed at initialization:

| Property | Source | Purpose |
|---|---|---|
| Elevation (`elev01`) | Preset base + diagonal gradient + noise | Drives temperature lapse rate and runoff direction |
| Slope (`slope01`) | Max elevation difference to neighbors | Affects infiltration rate and runoff speed |
| Soil fertility | Preset base + noise | Constrains which species can establish |
| Soil hold | Preset base + noise | Controls how quickly water infiltrates from surface to root layer |
| Temperature offset | Lapse rate from elevation (-6.5 C per unit) + micro-noise (up to 1 C) | Creates per-tile temperature variation within a biome |
| Orographic factor | Windward position + elevation uplift - rain shadow | Multiplier on precipitation, simulating orographic lift from westerly wind |
| Downhill neighbor | Lowest adjacent tile by elevation | Used for surface water runoff routing |

### Two-Layer Moisture Model

Water exists in two pools per tile:

- **Surface water** -- accumulates from precipitation, drains through infiltration and runoff. Represents standing water and puddles.
- **Root moisture** -- the subsurface moisture that plants actually consume. Filled by infiltration from the surface layer.

External code (renderer, metrics) reads moisture through the `soil_moisture()` accessor, which returns root moisture.

### Hydrology Cycle

Each simulation tick, the hydrology update processes water movement in this order:

1. **Precipitation** adds to surface water, scaled by the tile's orographic factor.
2. **Infiltration** transfers surface water to root moisture. Rate depends on soil hold and is reduced on steeper slopes (water runs off before soaking in).
3. **Evaporation** removes water from the surface layer first, then from root moisture. Evaporation demand is driven by temperature and suppressed by precipitation (humidity) and canopy cover (shade reduces evaporation up to 40%).
4. **Plant uptake** draws from root moisture proportional to each plant's biomass and water need.
5. **Runoff** routes remaining surface water to the precomputed downhill neighbor. A temporary buffer prevents order-dependent artifacts. Steeper slopes produce higher runoff fractions.

### Plant-Environment Feedback

Living plants contribute to a per-tile canopy cover value (0 to 1), recomputed each tick. Trees contribute the most canopy per unit biomass, followed by palms, shrubs, and ground cover. Canopy cover feeds back into the climate system by reducing evaporation demand, creating a stabilizing loop: denser vegetation retains more soil moisture, which in turn supports further growth.

---

## Vegetation Simulation

### Plant Archetypes

Eight archetypes span the range from ground cover to emergent canopy trees. Each archetype has characteristic growth speed, temperature tolerance, water needs, clustering behavior, and lifespan.

| Archetype | Growth Speed (biomass/day) | Approx. Maturity | Base Lifespan (years) | Cluster Affinity |
|---|---|---|---|---|
| Grass | 0.017 | ~60 days | 2 | Highly gregarious (+0.8) |
| Moss | 0.008 | ~120 days | 5 | Extremely gregarious (+0.9) |
| Shrub | 0.0014 | ~2 years | 15 | Moderate (+0.3) |
| Succulent | 0.00055 | ~5 years | 25 | Very territorial (-0.6) |
| Broadleaf Tree | 0.00018 | ~15 years | 80 | Territorial (-0.3) |
| Conifer | 0.00014 | ~20 years | 100 | Gregarious (+0.5) |
| Palm | 0.00020 | ~14 years | 60 | Loosely grouped (+0.1) |
| Tropical Tree | 0.00015 | ~18 years | 70 | Territorial (-0.4) |

Growth speed values have +/-20% random variation per individual. Lifespans have +/-30% variation. Trait values (water need, drought tolerance, temperature range, hue, leafiness, branchiness) are generated per plant based on archetype defaults and climate-driven biases from the active biome preset.

Cluster affinity governs spatial behavior. Positive values mean same-species neighbors reduce competition (gregarious species form meadows or dense stands). Negative values mean all neighbors increase competition (territorial species space themselves apart).

### Growth Phases

Plant biomass ranges from 0 to 1. Three growth phases determine visual size and reproduction eligibility:

| Phase | Biomass Range | Notes |
|---|---|---|
| Small | 0.00 -- 0.33 | Seedlings. Small-phase trees are hidden in rendering. Seedling sprites render with reduced opacity. |
| Medium | 0.33 -- 0.66 | Intermediate size. |
| Large | 0.66 -- 1.00 | Full size. Only Large-phase healthy plants can reproduce. |

### Density-Dependent Growth

Each neighbor on the same tile reduces a plant's growth rate. The suppression formula uses a reciprocal function: each neighbor reduces effective growth by approximately 15%. This models competition for light and nutrients without hard cutoffs.

### Reproduction

Only mature (Large phase) healthy plants can reproduce. Reproduction probability per spawn cycle scales with growth speed, so fast-growing species (grass, moss) reproduce more frequently than slow-growing trees.

Offspring land on the parent's tile or an adjacent tile in the 3x3 neighborhood. Establishment follows the same density-dependent soft cap as initial spawning: probability decreases linearly as tile density approaches the biome's max plants per tile, with a hard cap at twice the maximum.

### Death Causes

Five causes of death are tracked:

| Cause | Trigger |
|---|---|
| Drought | Water reserve drops below 0.1 |
| Cold | Temperature below the plant's minimum viable temperature |
| Heat | Temperature above the plant's maximum viable temperature |
| Competition | Default cause when stress reaches lethal levels from crowding |
| Senescence | Age exceeds 90% of the plant's maximum lifespan |

When a plant dies, it remains visible for 15 simulation days (the dead removal timer), during which its biomass decays. After removal, the metrics collector is notified.

### Stress and Recovery

Stress accumulates from environmental pressure (drought, temperature extremes) and crowding, and is continuously reduced by a recovery term. Recovery rate depends on environmental suitability and biomass (mature plants recover faster). This design prevents seasonal stress from ratcheting up irreversibly over multiple annual cycles.

Health states map directly from stress level:

| Health | Stress Range |
|---|---|
| Healthy | 0.0 -- 0.3 |
| Stressed | 0.3 -- 0.6 |
| Brown | 0.6 -- 0.9 |
| Dead | 0.9 -- 1.0 |

Mature plants resist stress accumulation better than seedlings (up to 4x resistance at full biomass). Drought tolerance further reduces water stress for adapted species. Brown-health plants also lose biomass over time, potentially shrinking to a smaller growth phase before dying.

Senescence adds increasing stress once a plant passes 75% of its maximum lifespan, accelerating toward death as age approaches the limit.

---

## Procedural Plant Sprites

Sprites are generated deterministically from a plant's traits, growth phase, and health status. The generation pipeline in `sandboxes/veggen/plant_sprite.cpp` uses a material buffer system that first lays down structural elements (wood, leaf, accent, shadow materials), then colors them using a palette derived from the plant's traits.

### Species Visual Differences

Visual appearance is driven by the ecological traits in `PlantTraits`:

- **base_hue** -- selects position along a leaf color gradient (yellow-green through blue-green). Climate biases this value: hot+wet biomes produce rich dark greens, cold biomes shift toward blue-green, hot+dry biomes produce olive/yellow tones.
- **leafiness** -- controls foliage density and brightness. Higher precipitation biomes produce higher leafiness values.
- **branchiness** -- determines branch complexity for tree and shrub archetypes.
- **sprite_seed** -- seeds the random generator for deterministic per-individual variation in branch angles, leaf placement, and other structural details.

Each archetype has a distinct structural generator (grass blades, shrub canopies, conifer silhouettes, broadleaf crowns, succulent pads, moss mats, palm fronds, tropical tree canopies). The material buffer tracks wood pixel order (trunk, primary branch, twig) for accurate coloring.

A 5-shade lighting model simulates directional light from the upper-left. Surface normals are approximated from pixel position relative to the canopy center to produce natural shading bands.

### Health Effects on Appearance

Health states are applied as post-processing on the same structural skeleton:

- **Healthy** -- full palette colors.
- **Stressed** -- desaturated, slightly yellowed.
- **Brown** -- shifted toward brown/ochre tones.
- **Dead** -- grayscale with reduced contrast.

The underlying structure (trunk, branch pattern, canopy shape) does not change between health states.

### The Spritetest Sandbox

The `sandboxes/spritetest/` sandbox provides a visual iteration tool for sprite generation. It renders a composite BMP image with all species displayed as a grid: each row is a species, and columns show three healthy instances (demonstrating within-species variation from different seeds), one stressed instance, and one dead instance.

Spritetest defines 19 named species profiles spanning all 8 archetypes, each with specific ecological traits that produce visually distinct results. It shares `plant_sprite.cpp`, `vegetation.cpp`, and `metrics.cpp` source files directly from the veggen sandbox via its CMakeLists.txt.

Usage: `sandbox_spritetest [seed] [species_index|all]`

---

## Biome Presets

Eight biome presets configure the terrain, climate, species composition, and ground color for the simulation.

| # | Biome | Temp Base (C) | Seasonal Amp (C) | Precip Base | Max Plants/Tile | Dominant Archetypes |
|---|---|---|---|---|---|---|
| 0 | Tropical Rainforest | 27 | 3 | 0.9 | 6 | Tropical Tree, Palm, Broadleaf |
| 1 | Savanna | 25 | 8 | 0.4 | 4 | Grass, Shrub |
| 2 | Temperate Forest | 12 | 12 | 0.6 | 4 | Broadleaf, Conifer, Shrub |
| 3 | Boreal | 0 | 15 | 0.5 | 3 | Conifer, Moss |
| 4 | Desert | 30 | 10 | 0.1 | 2 | Succulent |
| 5 | Tundra | -5 | 10 | 0.3 | 3 | Moss, Grass |
| 6 | Alpine Meadow | 5 | 10 | 0.5 | 4 | Grass, Moss |
| 7 | Wetland | 15 | 10 | 0.8 | 4 | Grass, Moss, Shrub |

Each preset also specifies base elevation, slope, soil fertility, soil hold capacity, and RGB ground color. Archetype weights control the probability of each species appearing, with the two highest-weighted archetypes selected as "dominant species" at initialization (with a 20% chance to promote a rarer species for variety).

Temperature and precipitation vary sinusoidally with the year (365 sim-days), using the base and amplitude values.

---

## Interactive Controls and Overlays

In interactive mode, the sandbox provides keyboard controls and visual overlays.

### Controls

| Key | Action |
|---|---|
| Left/Right | Cycle biome preset |
| Space | Reset plants (clear and respawn) |
| +/- | Adjust simulation speed (0.5x, 1x, 2x, 4x, 8x) |
| P | Pause/unpause |
| 1-7 | Select overlay (None, Root Moisture, Temperature, Health, Elevation, Surface Water, Canopy Cover) |
| G | Toggle grid lines |
| F | Toggle FPS counter |
| S | Save screenshot as BMP |
| ESC | Quit |

### Overlays

| Key | Overlay | Visualization |
|---|---|---|
| 1 | None | Plants on biome-colored ground only |
| 2 | Root Moisture | Brown (dry) to blue (wet) |
| 3 | Temperature | Blue (cold) through green to red (hot) |
| 4 | Health | Average plant stress per tile: green/yellow/brown/gray |
| 5 | Elevation | Dark green (low) through brown to white (high) |
| 6 | Surface Water | Tan (dry) through cyan to deep blue (flooded) |
| 7 | Canopy Cover | Pale (open) to dark green (dense canopy) |

### UI Panel

A left-side panel displays grid-wide averages for temperature, precipitation, root moisture, surface water, canopy cover percentage, elevation range, and live plant count. The current biome name, simulation day, season, and speed are shown at the top.

---

## Headless CLI Modes

### Auto-Screenshot Mode

`sandbox_veggen --auto [seed]`

Renders all 8 biome presets at four seasonal snapshots (day 90/Spring, 180/Summer, 270/Autumn, 350/Winter) and saves each frame as a BMP file. Output filenames follow the pattern `veggen_NN_Biome_Name_season.bmp`. Requires SDL video initialization for offscreen rendering.

### Metrics Mode

`sandbox_veggen --metrics [seed] [years]`

Runs all 8 biomes headlessly for a configurable number of years (default 10) at 60 FPS equivalent tick rate. For each biome, the collector takes a snapshot every 30 simulation days and exports a CSV file (`metrics_NN_Biome_Name.csv`) plus a summary to stdout. No SDL video is required.

---

## Metrics and Diagnostics

The `MetricsCollector` (defined in `sandboxes/veggen/metrics.h`) attaches to the simulation state and records periodic snapshots.

### Snapshot Contents

Each snapshot (taken every 30 sim-days by default) records:

- **Population**: living count, dead-pending count, per-archetype breakdown
- **Age distribution**: four bins (Seedling, Juvenile, Mature, Old) based on age relative to growth speed
- **Mortality breakdown**: deaths by cause (Drought, Cold, Heat, Competition, Senescence) for the interval
- **Recruitment**: new plants and removed plants during the interval
- **Spatial distribution**: mean and variance of plants per tile
- **Environment**: average temperature and moisture across the grid
- **Biomass statistics**: mean and variance
- **Growth-density correlation**: Pearson r between per-plant growth rate and neighbor count (expected negative, confirming density-dependent suppression)
- **Species fractions**: proportion of each archetype in the living population
- **Replacement ratio**: births divided by mature plant count (target around 1.0 for stable populations)

### CSV Export Format

The CSV header includes aggregate columns followed by per-archetype population, fraction, and stress columns, plus age bin totals. One row per snapshot.

### Printed Summary

The `print_summary` method outputs a compact report per biome covering population trajectory, final species composition, total births and deaths, mortality breakdown, age distribution, average growth-density correlation, population stability (coefficient of variation over the second half of the run), and average replacement ratio.

---

## Integration Points

- **Spritetest** (`sandboxes/spritetest/`) compiles `plant_sprite.cpp`, `vegetation.cpp`, and `metrics.cpp` directly from `sandboxes/veggen/` and includes veggen headers via its include path. It does not duplicate any simulation or sprite code.
- **Terrain model** follows the same conceptual design as the weather sandbox (`sandboxes/weather/`): elevation-driven lapse rate, orographic precipitation, surface/subsurface moisture layers, and downhill runoff routing. The veggen implementation is simplified for a 3x3 grid rather than the full world-scale grid.
- Both sandboxes depend only on SDL2, FastNoiseLite (via the `add_darwin_sandbox` helper), and `darwin_warnings`. They share no code with `darwin::*` modules.

---

## Source Files

| File | Role |
|---|---|
| `sandboxes/veggen/vegetation.h` | Data structures, enums, biome preset declaration, SimState interface |
| `sandboxes/veggen/vegetation.cpp` | Biome presets, terrain generation, hydrology, spawning, growth, death, reproduction |
| `sandboxes/veggen/plant_sprite.h` | Sprite generation interface |
| `sandboxes/veggen/plant_sprite.cpp` | Procedural pixel-art sprite generator for all 8 archetypes |
| `sandboxes/veggen/metrics.h` | MetricsCollector, MetricsSnapshot, DeathCause, AgeBin definitions |
| `sandboxes/veggen/metrics.cpp` | Snapshot collection, CSV export, summary printing |
| `sandboxes/veggen/renderer.h` | Renderer class, overlay modes, sprite cache |
| `sandboxes/veggen/renderer.cpp` | Tile rendering, plant sprite rendering with Y-sort, overlay visualization, UI panel |
| `sandboxes/veggen/telemetry.h` | FPS counter, text rendering, color helpers |
| `sandboxes/veggen/telemetry.cpp` | Bitmap font, panel drawing, overlay color functions |
| `sandboxes/veggen/main.cpp` | Interactive loop, CLI mode dispatch, input handling |
| `sandboxes/veggen/CMakeLists.txt` | Build definition (uses `add_darwin_sandbox` helper) |
| `sandboxes/spritetest/main.cpp` | Species profiles, composite sprite sheet generation |
| `sandboxes/spritetest/CMakeLists.txt` | Build definition, shares veggen source files |
