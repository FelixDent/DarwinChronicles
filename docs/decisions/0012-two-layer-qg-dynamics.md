# ADR 0012: Two-Layer Quasi-Geostrophic Dynamics

> **Note**: This file was created with an incorrect ADR number (conflicts with ADR-0012 basin spillway storage). The canonical record is [`0014-two-layer-qg-dynamics.md`](0014-two-layer-qg-dynamics.md). This file is retained for git history but superseded.

## Status: Superseded by ADR-0014

## Context

The weather sandbox atmosphere simulation produces good thermal structure (seasonal cycle, latitude gradients) and moisture behavior, but lacks realistic synoptic-scale weather systems. The current wind/pressure model is an unbalanced "PGF + damping" system that cannot sustain baroclinic instability -- the real physical mechanism for mid-latitude cyclone generation.

Synoptic variance fraction (k=3-8 wavenumbers) is stuck at 13-20%, with planetary scale (k=1-2) dominating at 70%+. No amount of parameter tuning can fix this because the dynamical core lacks the equations needed for balanced flow and instability growth.

## Decision

Replace the wind/pressure core with a two-layer quasi-geostrophic (QG) model. This is the standard minimal dynamical core for producing realistic baroclinic instability on coarse grids.

### New Fields (AtmosphereCell)

```cpp
float psi1 = 0.0f;    // upper layer streamfunction (cell^2/day)
float psi2 = 0.0f;    // lower layer streamfunction
float q1_pv = 0.0f;   // upper layer potential vorticity
float q2_pv = 0.0f;   // lower layer potential vorticity
```

### Constants

```cpp
BETA       = 0.873    // 1/day/cell (beta-plane at 45 deg)
Ld         = 5.0      // deformation radius in cells
invLd2     = 0.04     // 1/Ld^2
SHEAR_GAIN = 2.0      // (cells/day) per (degC/cell) for thermal wind
C_T        = 1.5      // degC per psi unit for T_anom mapping
NU4        = 0.05     // cells^4/day hyperviscosity on PV
EKMAN_R    = 0.5      // 1/day Rayleigh drag on lower layer (2-day timescale)
```

### New Tick Pipeline

1. Terrain coupling (unchanged)
2. Seasonal baseline update (T_base, q_base -- unchanged)
3. **QG: Compute background shear U_s(y) from dT_base/dy**
4. **QG: PV inversion -- solve for psi1, psi2 from q1_pv, q2_pv** (SOR or FFT-x + tridiag-y)
5. **QG: Diagnose winds u1,v1,u2,v2 from streamfunction**
6. **QG: Advect PV using total winds (background + perturbation)**
7. **QG: Apply del-4 hyperviscosity + Ekman drag to PV**
8. **Export lower-layer winds to cell.u, cell.v; upper to cell.u_upper, cell.v_upper**
9. **Map T_anom = C_T * (psi1 - psi2)** for thermal coupling
10. Semi-Lagrangian advection of moisture tracers (q_anom, cloud, precip_budget) using exported winds
11. Vertical exchange (simplified -- stability from QG temperature)
12. Solar forcing + ground temp (unchanged, but T_anom now set by QG)
13. Moisture/condensation/precipitation (unchanged, uses QG winds + convergence)
14. Composite update

### What Gets Replaced

- Pressure computation from T_anom/convergence (steps 3-4 old) -- replaced by QG PV inversion
- Wind from PGF + Coriolis + damping (step 5 old) -- replaced by QG balanced winds
- T_anom relaxation -- replaced by QG thermal wind constraint
- Laplacian diffusion on T_anom -- replaced by del-4 hyperviscosity on PV

### What Stays

- Moisture system (evap, condensation, cloud lifecycle, precipitation)
- Ground temperature and solar forcing
- Terrain coupling
- Seasonal baseline tracking
- Semi-Lagrangian advection of moisture tracers
- All overlays and diagnostics (wind, temperature, etc.)
- Dynamics coupling (precipitation -> soil moisture -> evaporation)

### PV Inversion

For each layer, PV relates to streamfunction:
```
q1 = del2(psi1) + invLd2 * (psi2 - psi1) + beta * y
q2 = del2(psi2) + invLd2 * (psi1 - psi2) + beta * y
```

Inversion: given q1,q2, solve for psi1,psi2. Use SOR (50-300 iterations at 64x32) or FFT in x + tridiagonal in y per wavenumber.

### Grid and Boundaries

- 64x32, Earth-like: 625 km per cell
- x: periodic (wrap)
- y: wall (no-normal-flow: psi = 0 at boundaries, or v = 0)
- beta-plane centered at 45 deg latitude

## Consequences

- Synoptic-scale weather systems (mid-latitude cyclones) will emerge naturally from baroclinic instability
- Wind field becomes balanced (geostrophic) rather than noise-forced
- Temperature anomalies are dynamically consistent with wind patterns
- Computational cost increases slightly (PV inversion each tick) but 64x32 is trivial
- Existing moisture/thermal/dynamics systems are preserved
