# ADR 0014: Two-Layer Quasi-Geostrophic Dynamics

**Date**: 2026-03-07
**Status**: Accepted

## Context

The weather sandbox atmosphere simulation produces good thermal structure (seasonal cycle, latitude gradients) and moisture behavior, but lacks realistic synoptic-scale weather systems. The previous wind/pressure model was an unbalanced "PGF + damping" system that could not sustain baroclinic instability — the real physical mechanism for mid-latitude cyclone generation.

Synoptic variance fraction (k=3–8 wavenumbers) was stuck at 13–20%, with planetary scale (k=1–2) dominating at 70%+. No amount of parameter tuning could fix this because the dynamical core lacked the equations needed for balanced flow and instability growth.

## Decision

Replace the wind/pressure core in `atmosphere.cpp` (tick steps 2–3) with a two-layer quasi-geostrophic (QG) model. This is the standard minimal dynamical core for producing baroclinic instability on coarse grids. The four QG fields (`psi1`, `psi2`, `q1_pv`, `q2_pv`) were already declared in `AtmosphereCell` — they are now initialized and actively used.

### New Fields (AtmosphereCell — initialized and used)

```cpp
float psi1 = 0.0f;    // upper layer streamfunction (cell^2/day)
float psi2 = 0.0f;    // lower layer streamfunction
float q1_pv = 0.0f;   // upper layer potential vorticity
float q2_pv = 0.0f;   // lower layer potential vorticity
```

### Constants (as implemented)

```
QG_BETA       = 0.873   // 1/day/cell (beta-plane at ~45 deg)
QG_Ld         = 5.0     // deformation radius in cells
QG_invLd2     = 0.04    // 1/Ld^2
QG_SHEAR_GAIN = 1.0     // (cells/day) per (degC/cell) for thermal wind
QG_C_T        = 0.5     // degC per psi unit for T_anom mapping
QG_NU4        = 0.3     // cells^4/day hyperviscosity on PV (strong scale-selective damping)
QG_EKMAN_R    = 1.5     // 1/day Rayleigh drag on lower layer (strong surface friction)
QG_SOR_ITERS  = 200     // SOR iterations for PV inversion
QG_SOR_OMEGA  = 1.6     // SOR over-relaxation parameter
```

### Tick Pipeline (steps 2–3 replaced)

Old steps 2–3 (pressure from T, wind from PGF+Coriolis+damping) are replaced by QG step 2, which is internally ordered as:

- **2a**: Compute background thermal wind shear `U_s(y)` from zonal-mean `dT_base/dy`. Thermal wind relation: vertical shear proportional to horizontal temperature gradient.
- **2b**: Advect PV semi-Lagrangianly. Upper layer advected by `u1 = -dpsi1/dy + U_s/2`; lower layer by `u2 = -dpsi2/dy - U_s/2`. CFL-clamped to `MAX_TRACE_DIST = 1.5` cells.
- **2c**: Ekman drag on lower-layer PV — `q2_pv -= QG_EKMAN_R * lap_psi2 * dt`. Terrain roughness enhances drag via `avg_roughness * 0.5`.
- **2d**: Del-4 hyperviscosity — biharmonic diffusion `dq/dt -= QG_NU4 * del4(q)` applied to both PV layers. Strongly damps grid-scale noise while preserving synoptic eddies.
- **2e**: PV inversion — SOR (200 iterations, ω=1.6) solves the coupled elliptic system for `psi1`, `psi2` given `q1_pv`, `q2_pv`. x-periodic; y-wall: `psi = 0` at pole rows.
- **2f**: Diagnose winds from streamfunction gradients and export to `cell.u/v` (lower layer) and `cell.u_upper/v_upper` (upper layer).
- **2g**: Map `T_anom += QG_C_T * (psi1 - psi2)` — thermal wind constraint: anomalous temperature is proportional to inter-layer streamfunction difference (warm-core lows, cold-core highs).

All subsequent steps (semi-Lagrangian advection of moisture tracers, vertical exchange, solar forcing, moisture/condensation/precipitation) are unchanged and use the QG-derived winds.

### What Gets Replaced

- Pressure computation from T + convergence (old step 2) — removed
- Wind from PGF + Coriolis + damping (old step 3) — removed
- Laplacian T_anom diffusion — replaced by del-4 hyperviscosity on PV
- Old constants removed: `K_PRESSURE`, `K_DAMP`, `K_CONVERGENCE`, `K_PRESSURE_DIFFUSE`, `K_HEAT_DIFFUSE`, `K_UPPER_DAMP`

### What Stays

- Moisture system (evaporation, condensation, cloud lifecycle, precipitation)
- Ground temperature and solar forcing
- Terrain coupling (step 1)
- Seasonal baseline tracking (step 1b)
- Semi-Lagrangian advection of moisture tracers (q_anom, cloud, precip_budget)
- All overlays and diagnostics (wind, temperature, stability, etc.)
- Dynamics coupling (precipitation → soil moisture → evaporation feedback)
- T prognostics discipline and energy conservation invariants (ADR-0013)

### PV Inversion Details

The elliptic system (coupled across layers):
```
q1 = del2(psi1) + invLd2*(psi2 - psi1) + beta*y
q2 = del2(psi2) + invLd2*(psi1 - psi2) + beta*y
```

Rearranged for SOR Gauss-Seidel (updated `psi1` used immediately when computing `psi2` in the same sweep):
```
psi = (psi_L + psi_R + psi_U + psi_D - rhs) / (4 + invLd2)
```

## Consequences

**What becomes easier:**

- Synoptic-scale weather systems (mid-latitude cyclones and anticyclones) emerge naturally from baroclinic instability rather than requiring noise forcing.
- Wind field is geostrophically balanced — temperature anomalies are dynamically consistent with wind patterns via the thermal wind constraint.
- Del-4 hyperviscosity is more physically appropriate than Laplacian diffusion: it strongly damps grid-scale noise while preserving synoptic-scale eddies.
- Ekman drag with terrain roughness enhancement gives realistic surface friction without ad-hoc damping constants.

**Tradeoffs:**

- PV inversion (SOR 200 iterations at 64×32 coarse grid) adds compute cost each tick. At current grid sizes this is negligible.
- QG is a mid-latitude approximation. Tropical dynamics (Hadley cell, ITCZ convergence zones) are not well-represented. Existing solar forcing and terrain coupling partially compensate.
- `QG_SHEAR_GAIN`, `QG_C_T`, `QG_EKMAN_R`, and `QG_NU4` require tuning; initial values chosen conservatively and will need adjustment as the synoptic variance spectrum is measured.
- The QG model does not include explicit moisture-PV coupling (moist QG). Convective heating enters only through the existing latent heat and convective instability paths.
- The old `docs/decisions/0012-two-layer-qg-dynamics.md` was created with an incorrect ADR number (conflicting with ADR-0012 basin spillway storage). This file is the correctly numbered record.
