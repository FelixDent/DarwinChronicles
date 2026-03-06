# Terrain Generation System

**Implementation:** `sandboxes/worldgen/`

## Overview

The terrain generation system produces procedural 2D tile-based worlds for the Darwin Chronicles simulation. It generates continental landmasses with realistic coastlines, inland lakes, mountain ranges, soil properties, and derived spatial fields — all from a single seed and a small set of tunable environment parameters.

The system runs as a standalone sandbox (`sandbox_worldgen`) with an interactive SDL2 viewer for real-time parameter tweaking, overlay visualization, and export. It does not link any `darwin::*` modules; it uses only SDL2 and FastNoiseLite.

## Generation Pipeline

World generation proceeds through eleven sequential stages, timed independently. Each stage builds on the outputs of previous stages.

| Stage | Timing Key | Description |
|---|---|---|
| **1. Height Field** | `height_ms` | Produces the continental heightmap using domain-warped noise. Two low-frequency Perlin noise layers (seed offsets +10000 and +11000) warp input coordinates by up to 20 tiles before sampling, creating organic, non-radial landform shapes. The warped coordinate is then used for: a 4-octave Perlin terrain detail, a continental shape noise, a coastline detail noise, and a lake carving noise. Edge falloff pushes map borders toward ocean. |
| **2. Ridge Field** | `ridge_ms` | Generates mountain range relief using ridged multifractal noise. Plain Perlin noise is sampled and inverted through `1 - abs(noise)` to produce ridge lines along zero-crossings, creating connected elongated mountain ranges rather than random peaks. A second octave at double frequency adds branching detail. A low-frequency region mask restricts mountains to distinct zones per continent. Ridges are suppressed near coastlines. The ridge field is kept as a separate array used for renderer decoration (`TerrainTile::elevation`). |
| **3. Unify Height** | `unify_ms` | Merges the ridge field into the continental height field (scale factor ~0.18). Mountains now push the unified height upward, making them affect hydrology, slope, distance fields, and elevation band classification — not just renderer decoration. This is a key change from earlier versions where the ridge field was purely visual. |
| **4. Ocean/Lake Flood-Fill** | `ocean_lake_ms` | Classifies all tiles below the water level as water, then uses BFS flood-fill to identify connected water components. A component is classified as ocean if it touches a map edge and contains at least 200 tiles. All other water components are classified as lakes. |
| **5. Slope, Aspect, Elevation Bands** | `slope_band_ms` | Computes slope at each tile using central-difference gradients on the **unified** height field (not the continental-only field). Also computes the aspect field: the slope direction as `atan2(dy, dx)`, ranging from -π to π. Elevation bands (Lowland/Hills/Mountains) are derived purely from unified height using fixed offsets above water level (Lowland: 0–18%, Hills: 18–30%, Mountains: 30%+). |
| **6. Euclidean Distance Transform** | `dist_fields_ms` | Computes exact Euclidean distances (replacing the previous BFS Manhattan distances). Uses the Felzenszwalb separable parabola-envelope algorithm in two 1D passes (horizontal then vertical). Two passes are run: one from ocean tiles (`dist_ocean`) and one from all water tiles including lakes (`dist_water`). |
| **7. Soil Fields** | `soil_ms` | Computes two soil properties for each land tile. Soil fertility is derived from a Perlin noise base, a water proximity bonus, and a slope penalty. Soil hold (water retention capacity) is derived from a separate material noise, a lowland elevation bonus, and a steepness penalty. Both fields are zero on water tiles. |
| **8. Roughness** | `roughness_ms` | Computes roughness as the standard deviation of heights in the 3×3 neighborhood centered on each tile, scaled by a constant (20.0) to map typical stddev values to the 0–1 range. This replaces the previous formula that blended ridged elevation and slope. |
| **9. Downhill Routing** | `downhill_ms` | For each tile, finds the steepest-descent neighbor among all 8 directions (D8 routing) and stores that neighbor's coordinates as `downhill_x`, `downhill_y`. Uses the unified height field, so mountains now have realistic drainage toward valleys. Also includes lake outflow computation: BFS identifies each lake component, finds the lowest adjacent land tile (the spill point), and routes all lake tiles toward the spill point, enabling realistic lake drainage to rivers. |
| **10. Flow Accumulation** | `river_ms` | Sorts tiles from highest to lowest, then walks the downhill graph accumulating a flow count. Each tile starts with 1.0 units of flow; each tile's flow is added to its downhill neighbor's. The resulting `river_flow` values are log₂-scaled for display (raw counts can be very large). This replaces the previous stub that left `river_flow` at 0. |
| **11. Tile Assembly** | — | Assembles all fields into the flat `TerrainTile` array. |

## World Data Model

The world is represented as a `Terrain` struct containing a flat vector of `TerrainTile` values indexed by `(y * width + x)`.

### TerrainTile Fields

| Field | Type | Range | Description |
|---|---|---|---|
| `elev01` | float | 0.0 – 1.0 | Unified height (continent + merged ridge). Values below the water level are underwater. |
| `band` | ElevBand | enum | Elevation classification: Water, Lowland, Hills, or Mountains. Derived from `elev01`. |
| `is_ocean` | bool | — | True if this tile is part of a large edge-connected water body. |
| `is_lake` | bool | — | True if this tile is part of a small inland water body. |
| `slope01` | float | 0.0 – 1.0 | Terrain gradient magnitude from central differences on unified height, scaled by 10×. |
| `aspect` | float | -π – π | Slope direction angle: `atan2(dy, dx)`. Points in the direction of steepest ascent. |
| `dist_ocean` | float | 0.0 – N | Euclidean distance (tiles) to the nearest ocean tile. |
| `dist_water` | float | 0.0 – N | Euclidean distance (tiles) to the nearest water tile (ocean or lake). |
| `river_flow` | float | 0.0 – N | Log₂-scaled flow accumulation. Higher values indicate main river channels. |
| `soil_fertility` | float | 0.0 – 1.0 | Composite fertility score from noise, water proximity, and slope. |
| `soil_hold` | float | 0.0 – 1.0 | Water retention capacity from material noise, elevation, and slope. |
| `roughness` | float | 0.0 – 1.0 | Local height variance (3×3 stddev × 20). Drives terrain decoration. |
| `elevation` | float | 0.0 – 1.0 | Ridge-only component retained for renderer decoration. Peaks on mountain ridges, zero on water. |
| `downhill_x` | int | -1 or 0–W | X coordinate of the downhill neighbor tile (-1 if no lower neighbor). |
| `downhill_y` | int | -1 or 0–H | Y coordinate of the downhill neighbor tile (-1 if no lower neighbor). |

### Environment Parameters (EnvParams)

| Parameter | Default | Range | Description |
|---|---|---|---|
| `water_level` | 0.45 | 0.0 – 0.8 | Height threshold below which tiles are classified as water. Higher values produce more ocean. |
| `noise_scale` | 0.02 | 0.001 – 0.1 | Frequency multiplier for the terrain detail noise. Higher values produce smaller, more detailed features. |
| `continent_threshold` | 0.55 | 0.0 – 1.0 | Continental shape bias. Higher values produce larger, more consolidated landmasses. |
| `ridge_strength` | 1.0 | 0.0 – 3.0 | Multiplier for the ridged elevation field. Higher values produce taller, more prominent mountain ranges. |

## World Presets

Each preset provides a curated set of environment parameters to produce a distinct world style.

| Preset | Water Level | Noise Scale | Continent Threshold | Ridge Strength | Character |
|---|---|---|---|---|---|
| **earth** | 0.45 | 0.02 | 0.55 | 1.0 | Balanced land/water ratio with moderate mountain ranges. Default preset. |
| **venus** | 0.35 | 0.02 | 0.65 | 1.4 | Low water level with consolidated continents and prominent ridges. |
| **mars** | 0.30 | 0.02 | 0.50 | 0.8 | Very low water level, broad landmass with subdued terrain relief. |
| **snowball** | 0.55 | 0.02 | 0.60 | 0.6 | High water level producing scattered islands with gentle terrain. |
| **tropical** | 0.45 | 0.025 | 0.50 | 1.2 | Earth-like water ratio with finer terrain detail and stronger ridges. |
| **toxic** | 0.40 | 0.02 | 0.55 | 1.5 | Moderate water with very strong mountain ridges. |

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
| 6 | DistWater | Green (near any water) to red (far from water) |
| 7 | SoilFertility | Brown (low) to green (high) |
| 8 | SoilHold | Brown (low) to green (high) |
| 9 | Roughness | Black (smooth) to yellow (rough) |
| 0 | Aspect | Hue wheel by slope direction angle (-π to π) |
| - | RiverFlow | Blue-tinted intensity by log-scaled flow accumulation |

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

Generation timings are recorded for each of the eleven timed pipeline stages and printed to the console and the log file.

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
  --help                Show help
```

## Integration Points

The worldgen sandbox serves as a prototype for terrain generation that feeds into downstream sandboxes:

- **Weather sandbox** (`sandboxes/weather/`): Contains its own copy of the world generation code (synced with worldgen changes) and layers atmospheric simulation on top — temperature, wind, precipitation, evaporation, and snow. The terrain's unified height, slope, elevation bands, distance fields, and downhill routing all influence weather behavior (lapse rate, orographic uplift, rain shadows, surface water runoff).
- **Vegetation sandbox** (`sandboxes/veggen/`): Uses a spatial terrain model with elevation gradients, slope, and a two-layer moisture system (surface water and root moisture). Terrain properties determine where different plant species can establish and thrive.
- **Main simulation** (`src/world/`): The `darwin::world` module implements a Whittaker biome classification system that maps temperature and moisture conditions onto biome types. Terrain generation concepts prototyped in this sandbox inform the world module's grid and terrain design.

Each downstream system maintains its own terrain implementation rather than linking the sandbox code directly, following the sandbox convention of prototyping in isolation before promoting patterns into `darwin::*` modules.

## Source Files

| File | Purpose |
|---|---|
| `sandboxes/worldgen/terrain_gen.h` | `TerrainTile`, `Terrain`, `ElevBand`, `GenerationTimings` definitions and `generate_terrain` declaration |
| `sandboxes/worldgen/terrain_gen.cpp` | All generation pipeline stages: height field (domain-warped), ridge field, unify, ocean/lake flood-fill, slope/aspect/bands, EDT distance fields, soil, roughness (3×3 stddev), downhill routing (D8 + lake outflows), flow accumulation |
| `sandboxes/worldgen/config.h` | `EnvParams`, `SandboxConfig`, and `Preset` structures |
| `sandboxes/worldgen/config.cpp` | Preset definitions and CLI argument parsing |
| `sandboxes/worldgen/renderer.h` | `Camera` and `Renderer` classes |
| `sandboxes/worldgen/renderer.cpp` | Camera transforms, terrain color mapping, tile rendering with elevation decoration |
| `sandboxes/worldgen/telemetry.h` | `OverlayMode` enum (11 modes), `TerrainStats`, `FPSCounter`, overlay/legend/button rendering declarations |
| `sandboxes/worldgen/telemetry.cpp` | Statistics computation, overlay color functions (including aspect and river flow), bitmap font, legend rendering with per-overlay info description text, UI buttons |
| `sandboxes/worldgen/main.cpp` | SDL2 initialization, event loop, parameter tweaking, screenshot/log export |
| `sandboxes/worldgen/CMakeLists.txt` | Build configuration via `add_darwin_sandbox(worldgen)` |
