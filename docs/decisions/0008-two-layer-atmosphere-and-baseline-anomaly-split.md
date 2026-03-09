# ADR 0008: Two-Layer Atmosphere and Climate Baseline / Weather Anomaly Split

**Date**: 2026-03-06
**Status**: Accepted

## Context

The weather sandbox atmosphere simulation previously used a single flat cell with composite fields (T, q, p) that mixed slow seasonal climate and fast transient weather into the same values. This caused two problems:

1. **Numeric coupling**: Advecting temperature transported both the climate signal and short-lived anomalies together. Relaxing T back toward T_ref after advection undid the advection's work on fronts, forcing a difficult constant-vs-relaxation trade-off.

2. **Missing vertical structure**: The real atmosphere has distinct layers with different dynamics. The boundary layer interacts with terrain and evaporation; the free troposphere carries jet streams and transports heat across latitudes. Without a second layer, orographic uplift and convective instability lacked a plausible mechanism for moisture to escape the surface and condense aloft.

The 5-phase improvement plan addressed this in phases 2 and 3 respectively.

## Decision

### Phase 2: Baseline / Anomaly Split

Split each cell's thermodynamic state into two components stored separately:

- **Baseline** (`T_base`, `q_base`, `p_base`): updated infrequently from the static bake + seasonal cycle. Provides a stable reference that drifts with the seasons but not with individual weather systems.
- **Anomaly** (`T_anom`, `q_anom`, `p_anom`): advected, diffused, and relaxed toward zero each tick. Carries fronts, heat waves, and cold air outbreaks.
- **Composite** (`T`, `q`, `p`): base + anomaly, recomputed end-of-tick. All external code (renderer, dynamics, telemetry) reads the composite; the split is internal to `tick_atmosphere`.

This decouples climate stability from weather variability. T_ref relaxation now acts only on the anomaly, not on the full field, which allows fronts to persist longer while still preventing runaway drift.

### Phase 3: Upper Troposphere Layer

Add a second atmospheric layer per cell representing the free troposphere (~500 hPa), with fields `T_upper`, `q_upper`, `u_upper`, `v_upper`, `p_upper`, and a derived `stability` value (T_upper − T; negative = convectively unstable).

The upper layer:
- Has its own wind field independent of the surface layer, enabling jet-stream-like flow patterns
- Exchanges heat and moisture with the surface layer based on static stability
- Receives forcing from the terrain coupling step (Phase 0) when steep terrain extends into the mid-troposphere
- Provides a reservoir that condensation and latent heat release can interact with

A new `Stability` overlay (F2) visualizes the lapse rate between layers, red for convectively unstable columns and blue for temperature inversions.

### Supporting phases

- **Phase 0** (terrain coupling): Wind-slope uplift, aspect-modulated solar heating, maritime moderation, and river evaporation are now computed in the terrain-coupling tick step rather than being baked-in constants, making the atmosphere respond to actual terrain geometry each tick.
- **Phase 1** (moisture budget): A `precip_budget` field per cell tracks available precipitation capacity. It is advected with wind, depleted when rain falls, and recharged over ocean, adding spatial coherence to rainfall patterns without requiring explicit moisture transport bookkeeping.
- **Phase 4** (energy tracking): `AtmosphereState::initial_energy` and `total_energy` are tracked for drift accounting. `AtmosphereStats` reports solar_in, radiative_out, latent_release, and energy_drift_pct each tick.

## Consequences

**Easier:**
- Temperature fronts persist more naturally because anomaly relaxation is gentler than full-field relaxation was
- Orographic effects are more physically grounded (terrain coupling step reads actual slope/aspect geometry)
- Debugging climate vs. weather issues is clearer with separate baseline and anomaly fields
- The upper wind overlay (F1) makes jet-stream dynamics visible to developers

**Harder:**
- `AtmosphereCell` struct is significantly larger (~65 floats per cell vs. ~20 before); memory usage for the coarse grid increases proportionally
- The tick function is more complex — more steps with stricter ordering requirements
- Unit tests for the atmosphere simulation would need to verify both layers and both split fields, not just composites
- The upper troposphere dynamics are simplified (no explicit Hadley circulation, no radiative balance between layers); this is an acknowledged approximation for a 2D sandbox

**Preserved:**
- All external code continues to read composite T/q/p — the API is unchanged
- `AtmosphereState::sample()` and `sample_nearest()` work on any `float AtmosphereCell::*` field pointer, so the new fields are accessible without API changes
