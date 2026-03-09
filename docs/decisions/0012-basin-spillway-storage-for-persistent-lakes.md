# ADR 0012: Basin Spillway Storage for Persistent Lakes

**Date**: 2026-03-07
**Status**: Accepted

## Context

After the D8 flow accumulation work (ADR-0011), emergent rivers produced clean channel networks but persistent lakes still did not form. ADR-0010 (geology-first architecture) removed lake classification from terrain generation entirely, with the intent that lakes would emerge dynamically from precipitation pooling in terrain depressions. Two problems prevented this:

1. **D8 sinks drain immediately.** WSE multi-neighbor routing routes water to every lower neighbor; terminal sink tiles accumulate surface water locally but have no hydrological memory — each tick starts fresh so water never builds up to fill a basin.
2. **Terrain depressions are too shallow.** Procedural DEMs from the tectonic pipeline have most depressions within a few height units of their spill point. Without sub-tile relief, WSE routing finds leakage paths before any meaningful pooling occurs.

Two approaches were considered:

**Option A: Pre-fill (Planchon-Darboux).** Run a depression-filling pass at terrain-gen time to identify and tag lake tiles. Rejected: this re-introduces terrain-gen lake classification (reverts ADR-0010) and makes lakes static — they cannot drain, shrink seasonally, or participate in river-lake spill chains.

**Option B: Dynamic basin spillway with volume accounting.** At init time, identify D8 sink basins via BFS and record each basin's spill elevation. During simulation, aggregate surface water into a per-basin volume and only spill when mean WSE exceeds the spill height. Add micro-relief noise to deepen basin floors.

The same changeset also addressed two related issues: the D8 accumulation pass treated quickflow (storm runoff) and baseflow (groundwater discharge) identically, and flash runoff from the surface-water cap was being silently discarded to ocean drain rather than routed downstream.

## Decision

**Basin spillway storage** was implemented as Option B. At `init_dynamics`:

- BFS from each D8 sink identifies its drainage basin (all tiles whose D8 path terminates at that sink).
- Per basin, compute `basin_sink_elev` (lowest point), `basin_spill_elev` (minimum boundary elevation — the lowest rim tile adjacent to a different basin or ocean), `basin_area` (tile count), and `basin_spill_tile` (the tile index through which overflow exits).
- A two-octave micro-relief noise field (wavelengths 8 tiles and 3 tiles, amplitude scaled by `1 − slope01`) is added to the hydrology elevation used during init, deepening flat basins into bowl-shaped depressions.

During each tick, for basins with `depression_depth = basin_spill_elev − basin_sink_elev >= 0.001`:

- `basin_volume` aggregates surface water across all member tiles.
- If mean WSE (volume / area + sink_elev) < `basin_spill_elev`: retain all water (lake storage).
- If mean WSE >= `basin_spill_elev`: excess routes through `basin_spill_tile` to the downstream basin or ocean, enabling lake-river spill chains.

`DynamicState` gains: `basin_id` (uint16_t per tile), `basin_spill_elev`, `basin_sink_elev`, `basin_area`, `basin_volume`, `basin_spill_tile` (per basin), and `num_basins`.

**Two-reservoir D8 accumulation** was added in the same changeset. The single `accum_discharge` pass is split into:

- `accum_quick`: seeds from surface runoff routed to WSE neighbors + coastal drain + cap overflow flash runoff. EMA tau=0.3 days (~7 hours — storm pulse timescale).
- `accum_base`: seeds from groundwater baseflow. EMA tau=10 days (aquifer recession timescale).

`DynamicTile` gains `quickflow` and `baseflow_d`. `DynamicTile::discharge` becomes `quickflow + baseflow_d`. Both reservoirs propagate independently along the same D8 graph.

**Overflow routing fix**: flash runoff from surface-water cap overflow now routes to the D8 downstream neighbor rather than being immediately counted as ocean drain. `WaterBudget` gains `total_coastal_drain` (WSE drain to ocean tiles) and `total_overflow` (cap overflow) as separate fields, plus `total_headwater` and `total_accum_raw` for D8 seeding diagnostics. All budget fields changed from float to double.

`SINK_TOLERANCE` was lowered from 0.002 to 0.001 because basin spillway storage handles lake retention — the tolerance only needs to prevent numerical drainage of flat cells.

## Consequences

**What becomes easier:**

- Persistent lakes form dynamically in topographic depressions without terrain-gen pre-classification. Lakes respond to precipitation seasonality.
- Spill-chain connectivity: a lake that overflows routes water to the next downstream basin, producing lake-river systems with correct connectivity.
- The two-reservoir discharge model allows the Discharge (F5) overlay to implicitly distinguish storm-pulse channels (quickflow-dominated) from perennial baseflow rivers.
- `WaterBudget` now closes more accurately: flash runoff is tracked rather than silently consumed.

**Tradeoffs:**

- Basin init requires BFS over all D8 sinks — O(N tiles) one-time cost. Negligible for current grid sizes (256×192) but scales linearly with grid area.
- The micro-relief noise deepens basins uniformly rather than only in geomorphically appropriate locations (a pragmatic approximation — ideally basin depth would derive from geological substrate).
- Basin volume aggregation re-sums surface water across basin tiles each tick — O(basin_tiles). Acceptable at current scales.
- Basin IDs are uint16_t, limiting the system to 65,534 basins. Typical runs produce fewer than a few hundred qualifying basins.
- Basin surface_water is stored as aggregate volume, so `surface_water` on individual lake tiles may not reflect actual lake depth — depth is inferred from `basin_volume / basin_area`.
- Single-outlet spill routing (lowest rim tile) does not model multi-outlet basins or bifurcating deltas.
