# ADR 0013: Atmosphere Energy Conservation and T Prognostics Discipline

**Date**: 2026-03-07
**Status**: Accepted

## Context

The atmosphere simulation exhibited a persistent energy drift that grew monotonically over multi-year runs. An artificial bulk energy correction was masking the problem: each tick the total energy was measured and a proportional correction applied to all cells. This kept the reported `energy_drift_pct` near zero, but suppressed physically real phenomena (storm latent heat release, cold-front formation) whenever the correction fired — equivalent to removing a ghost source rather than fixing it.

Separately, the `T_ref` formula contained a double-counted seasonal latitude term: `T_ref = -15 + solar_seasonal * 48 + season_offset * latitude * 12`. The `solar_seasonal` term already encodes the seasonal latitude effect, so the additive `season_offset * latitude * 12` amplified the pole-to-equator gradient artificially, producing exaggerated seasonal swings.

A third issue was T prognostics inconsistency: some code paths read `cell.T` (the composite `T_base + T_anom`) and then modified it directly before the end-of-tick recompute, creating double-counting paths where both a forcing and its reflected value in `cell.T` influenced the same step.

Finally, q > q_sat supersaturation could accumulate silently via advection and diffusion: bilinear interpolation of a saturated air mass into a colder cell would produce `q > q_sat(T_new)` with no enforcement until the next condensation step.

## Decision

Four linked changes were made together because they interact:

1. **Remove double-counted T_ref term**: `T_ref = -15 + solar_seasonal * 48` with no additive latitude modifier. The seasonal latitude gradient comes exclusively from `solar_seasonal`, which already varies with latitude and season.

2. **T prognostics discipline**: All fast physics writes only to `T_anom`. Ground-air coupling, convective release, and anomaly relaxation all modify `T_anom`. `cell.T = T_base + T_anom` is recomputed at the beginning of step 6 (moisture) so every step reads a consistent composite. No physics path modifies `cell.T` directly.

3. **Upper-layer Stefan-Boltzmann OLR** (K_RAD_UPPER=0.025, added to step 4c vertical exchange): The upper troposphere was accumulating heat with no radiation path to space. Real-atmosphere outgoing longwave radiation (OLR) is dominated by upper tropospheric emission. Adding T^4 cooling at a reduced rate (lower optical depth than the surface) removes the artificial energy source, eliminates the need for the bulk correction, and is physically correct. The bulk energy correction is removed.

4. **Post-advection q saturation enforcement**: After advection and diffusion, q is clamped to `q_sat(T)` for every cell. Cloud evaporation is also capped to prevent it from pushing q above saturation.

Parameter retuning followed to rebalance the system after the T_ref fix removed ~12°C of excess tropical heating:

| Parameter | Old | New | Reason |
|---|---|---|---|
| K_RAD | 0.15 | 0.09 | Less aggressive cooling needed without the OLR-absent upper layer |
| K_CONVERGENCE | 1.5 | 0.8 | Reduced pressure feedback to prevent wind overshoot |
| K_ANOM_RELAX_T | 0.02 | 0.06 | Faster anomaly decay (~17-day half-life vs ~35) prevents stale anomalies accumulating |
| OCEAN_EVAP_RATE | 0.15 | 0.10 | Rebalance after evap/precip ratio shifted |
| LAND_EVAP_RATE | 0.05 | 0.03 | Same |
| CLOUD_PRECIP_THRESHOLD | 0.12 | 0.07 | Clouds rain out sooner without excess moisture loading |
| CLOUD_PRECIP_RATE | 1.2 | 2.5 | Faster rainout compensates lower threshold |
| BUDGET_OCEAN_RECHARGE | 0.20 | 0.08 | Prevents moisture budget oversaturation |
| BUDGET_RAIN_COST | 1.5 | 3.0 | Budget depletes faster per rain event |
| EXPECTED_LAPSE | -25 | -18 | Realistic mid-troposphere lapse allows inversions |
| K_VERT_HEAT | 0.04 | 0.02 | Gentler vertical exchange avoids over-homogenization |

## Consequences

**Positive:**
- Energy drift is physically bounded: +3.2% over 365 days, with seasonal oscillation ±40% around equilibrium (the oscillation is real — summer hemispheres store more energy than winter).
- No artificial correction masking physical instability — if a future code change introduces a true energy source, `energy_drift_pct` will reveal it.
- T prognostics discipline prevents an entire class of double-counting bugs in future physics additions. Any new forcing must route through `T_anom`.
- q saturation enforcement closes a conservation gap that could allow phantom moisture accumulation over long runs.
- All six world presets stable over 365-day headless runs.

**Negative / Tradeoffs:**
- T_ref change reduced peak tropical temperatures from ~40°C to ~33°C. This is more physically accurate but changes the balance any future code assumed.
- The bulk energy correction was a convenient backstop; removing it means future bugs in the energy budget will manifest as visible drift rather than being silently corrected.
- Phase 4 `energy_drift_pct` now reflects true drift. Users running diagnostics should expect ±40% seasonal oscillation as normal; monotonic growth (> ~5% net drift per year after accounting for season) indicates a problem.
- `K_ANOM_RELAX_T=0.06` is a faster decay than the old 0.02. Weather anomalies have a ~17-day half-life instead of ~35 days. This slightly reduces long-lived blocking patterns.

## No-Artificial-Equilibrium-Forcing Rule

As a design principle going forward: **do not add bulk energy or water corrections to force the simulation toward a target equilibrium**. Such corrections mask real bugs. The correct approach is to identify and fix the physical mechanism that is causing drift (missing OLR path, wrong boundary conditions, numerical leakage) and add that mechanism explicitly.
