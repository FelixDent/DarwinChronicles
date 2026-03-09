# ADR 0011: D8 Steepest-Descent Flow Accumulation for River Visualization

**Date**: 2026-03-07
**Status**: Accepted

## Context

The weather sandbox's dynamic simulation previously relied on WSE (water-surface-elevation) multi-neighbor flow for runoff routing and tracked discharge as a per-tile EMA-smoothed outflow sum. While WSE correctly distributes water across all downhill neighbors, it produces noisy and slow-to-converge discharge values that make emergent river channels blurry and hard to interpret in the Discharge (F5) overlay.

Additionally, the system lacked any global accounting of water flows: it was not possible to diagnose whether the simulation had a water budget leak, how much water was being lost to ocean drain versus evaporation, or whether the groundwater recharge rates were physically plausible.

A diagnostic tool was also needed for end-of-run analysis of headless runs to assess whether river networks formed correctly, whether storage was realistic, and whether the water balance was conserved.

## Decision

Two complementary additions were made to `dynamics.h/cpp`:

1. **D8 steepest-descent flow accumulation** — `DynamicState` gains three new fields computed once at initialization: `downhill` (single steepest-descent neighbor index per tile, the D8 pointer), `topo_order` (tile indices sorted from highest to lowest elevation), and `accum_discharge` (flow accumulation buffer reset and repopulated each tick). Each tick after WSE routing, tiles are visited in topographic order and each propagates its accumulated discharge plus local runoff plus baseflow to its single D8 downhill neighbor. This produces a clean catchment-area accumulation without EMA artifacts.

   A `SINK_TOLERANCE=0.002` deliberately allows micro-depressions to retain a small amount of standing water rather than draining completely, creating potential lake sites where flow converges.

   The `DynamicTile::discharge` field continues to hold WSE outflow per tick (unchanged). The `accum_discharge` buffer is separate and drives the Discharge (F5) overlay and `print_hydrology_diagnostics()`.

2. **WaterBudget struct** — `DynamicState::budget` accumulates cumulative totals across the run: total precipitation, surface evaporation, soil evaporation, ocean drain (WSE + cap overflow flash runoff), groundwater recharge, snowmelt, and initial storage. Flash runoff from the surface-water cap is explicitly tracked as ocean drain to close the accounting. `print_hydrology_diagnostics()` (called at end of headless runs) reports the full budget alongside storage percentiles, discharge network statistics, a discharge log-histogram, lake connected-component analysis, discharge by elevation band, and discharge by aridity zone.

WSE multi-neighbor routing was retained for the physical water redistribution step — D8 accumulation operates as a separate diagnostic/visualization pass on top of it, not as a replacement for actual routing.

## Consequences

**Positive:**
- The Discharge (F5) overlay now shows clean, EMA-artifact-free river networks that correctly express upstream catchment area.
- The water budget provides a quantitative closure check: `precip + snowmelt ≈ evap_surface + evap_soil + ocean_drain + storage_change` should hold to within a few percent for steady-state runs.
- `print_hydrology_diagnostics()` makes headless verification of hydrology changes tractable without requiring visual inspection of the SDL2 renderer.
- SINK_TOLERANCE micro-depressions create natural lake sites that interact correctly with groundwater baseflow.

**Tradeoffs:**
- Three additional `std::vector` fields in `DynamicState` increase memory usage by approximately 3× the tile count in integers plus one float vector (moderate cost for large grids).
- `topo_order` computation at init adds a one-time sort cost proportional to `W × H × log(W × H)`, which is negligible compared to the static weather bake.
- D8 accumulation adds one full grid pass per tick. At interactive framerates this is acceptable; at very high time-scale multipliers it could be a bottleneck if the tile count is large.
- The D8 pointer follows the steepest-descent neighbor of the raw terrain, which may not perfectly match the WSE-derived actual flow path when standing water has accumulated. The visualization may therefore deviate from actual water movement in heavily flooded areas.
