# ADR 0010: Geology-First Terrain Architecture

**Date**: 2026-03-07
**Status**: Accepted

## Context

The previous `TerrainTile` stored hydrology fields that were precomputed at terrain-generation time: `is_lake`, `dist_water`, `river_flow`, `soil_fertility`, `soil_hold`, `downhill_x/y`. These fields encoded a static snapshot of drainage topology based on the initial height field alone.

Several problems emerged with this approach:

1. **Hydrology inconsistency with weather dynamics**: The weather sandbox's dynamic simulation ran its own precipitation, infiltration, and runoff model — but the downhill routing used by dynamics was baked in from terrain-gen. When precipitation interacted with the landscape in non-trivial ways (e.g., pooling in basins, groundwater contribution), the precomputed D8 single-neighbor routing diverged from what actual water would do on the final terrain.

2. **No geological substrate for infiltration**: Infiltration in dynamics was modulated by `soil_hold`, a heuristic combining elevation and noise. There was no connection between rock type, soil texture, and hydraulic behavior. A granite ridge and a shale floodplain would infiltrate identically if their elevations and noise values matched.

3. **Soil fields were duplicated heuristics**: `soil_fertility` and `soil_hold` were computed from Perlin noise + water proximity — they did not represent anything materially grounded. Geology-derived soil properties (ksat, field capacity, wilting point) can be physically derived from first principles and validated against real soil science data.

4. **Lakes as static features**: Basin flood-fill lakes were baked into terrain-gen and then also rediscovered by the dynamics simulation. This created a disconnect — the terrain said "lake here" but dynamics treated those tiles like any other low-lying tile.

5. **River flow as a static field**: `river_flow` was a precomputed log-scaled D8 flow accumulation. It could not respond to dynamic changes in precipitation, seasonal variation, or groundwater contribution.

## Decision

**Terrain generation creates geological substrate only. Hydrology is fully dynamic.**

1. **Remove hydrology fields from TerrainTile**: `is_lake`, `dist_water`, `river_flow`, `soil_fertility`, `soil_hold`, `downhill_x`, `downhill_y` removed.

2. **Add geological substrate fields to TerrainTile**:
   - `RockType` enum (Granite, Basalt, Limestone, Sandstone, Shale, Metamorphic) — assigned by tectonic context
   - `SoilTexture` enum (Sand, Loam, Silt, Clay, Peat) — derived from rock type and landscape position
   - Hydraulic parameters: `bedrock_hardness`, `bedrock_permeability`, `soil_depth`, `porosity`, `ksat` (saturated hydraulic conductivity), `field_capacity`, `wilting_point`, `erodibility`

3. **`compute_geology()`** assigns rock types and soil properties deterministically from tectonic context, elevation, and slope — not from noise. Convergent mountain belts → Metamorphic; volcanic arcs → Basalt; stable cratons → Granite; sedimentary basins → Sandstone/Limestone; coastal lowlands → Shale with clay-silt soils.

4. **Weather sandbox dynamics updated**:
   - Infiltration now uses `ksat` and `field_capacity` from the tile's geology
   - `DynamicTile` gains `groundwater` (reservoir in depth-equivalent meters) and `discharge` (total outflow per tick)
   - Surface water routing uses WSE (water-surface-elevation) based multi-neighbor flow, routing to all downhill neighbors weighted by slope gradient instead of a single precomputed D8 neighbor
   - Rivers and lakes emerge dynamically from precipitation × geology interaction rather than being baked in

5. **`AtmosphereCell` removes `avg_river_flow`** — river evaporation bonus removed since river discharge is now a dynamic field, not available at terrain coupling time.

6. **Overlay set updated**: DistWater, SoilFertility, SoilHold, RiverFlow overlays removed; Geology and SoilTexture overlays added (keys 8-9).

7. **`GenerationTimings` updated**: `ocean_lake_ms`, `soil_ms`, `downhill_ms`, `river_ms` replaced by `ocean_ms`, `geology_ms`.

## Consequences

**What becomes easier or better:**
- Infiltration behavior is physically grounded — karst limestone terrain leaks quickly, granite ridges shed water, clay floodplains retain it. This produces more realistic drought and flood patterns without tuning heuristics.
- Lake formation is emergent — lakes form wherever precipitation exceeds drainage capacity, they grow and shrink seasonally, and they can dry up during drought. No disconnection between terrain classification and dynamic state.
- River discharge reflects actual precipitation patterns and responds to seasonal variation and drought. Groundwater baseflow maintains perennial streams.
- Geological substrate provides a natural bridge to future vegetation simulation — `ksat`, `field_capacity`, and `wilting_point` directly parameterize root-zone water availability for plants.
- Overlay information is more meaningful — Geology and SoilTexture communicate actual substrate properties that affect downstream behavior.

**What becomes harder or is lost:**
- Headless BMP river rendering from worldgen is no longer possible (no precomputed river_flow). River visualization moves to the weather sandbox where discharge is computed dynamically.
- The worldgen sandbox no longer shows lake positions at generation time — lakes only appear in the weather sandbox after precipitation accumulates.
- `AtmosphereCell` loses the river evaporation feedback from `avg_river_flow`, slightly reducing terrain coupling fidelity.
- The weather sandbox terrain_gen.cpp must continue to be kept in sync with worldgen terrain_gen.cpp manually (no change here, but the geology stage adds another sync surface).
