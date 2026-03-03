# ADR 0006: Three-Layer Planetary Physics Cascade

**Status**: Accepted
**Date**: 2025-02-28
**Deciders**: Project founders

## Context

The simulation needs a principled way to generate diverse worlds from a small set of input parameters. Biome distribution, resource availability, and environmental hazards should all flow from physical first principles rather than hand-authored maps.

Options considered:
1. **Hand-designed biome maps** — Maximum control, zero replayability
2. **Pure noise-based generation** — Random terrain, no physical coherence
3. **Physics-driven cascade** — Real stellar/atmospheric formulas → climate → biomes → biology

## Decision

Implement a **three-layer cascade** where each layer's outputs become the next layer's inputs:

**Layer 1 — Planetary parameters** (user-configured):
- Star: luminosity, temperature, spectral class
- Orbit: semi-major axis, eccentricity
- Planet: mass, radius, rotation period
- Atmosphere: pressure, CO₂ fraction, greenhouse gas mix

**Layer 2 — Climate computation** (derived):
- Stellar flux from luminosity and orbital distance
- Equilibrium temperature via Stefan-Boltzmann law
- Surface temperature with greenhouse offset (logarithmic CO₂ scaling)
- UV flux for mutation rate modulation
- Habitability index

**Layer 3 — Biological constraints** (derived):
- Nutrient regeneration rates scaled by temperature and moisture
- Base mutation rate modulated by UV flux
- Toxicity distribution from atmospheric chemistry
- Biome classification from temperature × moisture × elevation × toxicity

## Rationale

A Venus-like configuration (high CO₂, thick atmosphere) automatically produces: high surface temperature → mostly desert/volcanic biomes → fast mutation rates → high toxicity → organisms that evolve toxin resistance and heat tolerance. None of this needs to be scripted — it falls out of the physics.

This gives the player a meaningful parameter space to explore. Changing one number (e.g., orbital distance) reshapes the entire world coherently.

## Consequences

- **Good**: Infinite world variety from ~10 input parameters
- **Good**: Worlds are physically coherent — deserts form where they should, rain shadows make sense
- **Good**: Players learn real planetary science by experimenting with parameters
- **Good**: Preset scenarios (Earth, Mars, Venus, tidally-locked) are just parameter sets, not separate code paths
- **Tradeoff**: Some simplifications are necessary (no ocean currents, no plate tectonics, no seasons in v1)
- **Tradeoff**: Players who don't care about physics need good preset scenarios
