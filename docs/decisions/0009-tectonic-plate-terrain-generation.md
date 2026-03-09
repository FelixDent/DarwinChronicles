# ADR 0009: Tectonic Plate Terrain Generation

**Date**: 2026-03-06
**Status**: Accepted

## Context

The legacy noise-based terrain pipeline (domain-warped Perlin noise for continental shape, ridged multifractal for mountains) produces terrain that is topologically unstructured — mountain ranges appear in isolated patches that do not follow plate boundaries, and there is no spatial coherence between mountain formation, coastal structure, and drainage patterns. This limits the realism of downstream weather effects (orographic rain shadows, drainage basin formation, realistic coastlines).

A more physically motivated approach derives terrain structure from simulated plate tectonics: plates move, collide, and diverge, producing mountain chains at convergent boundaries, rifts and ocean ridges at divergent boundaries, and transform fault valleys along shear boundaries. The key design constraints are:
- Interactive performance: < 1 second total generation time for a 256×256 map
- Backward compatibility: the legacy noise mode must remain available at `plate_count == 0`
- Continuous, not binary: plate boundaries should produce smooth fields, not hard edges

## Decision

Add `generate_tectonic_terrain()` in both `sandboxes/worldgen/terrain_gen.cpp` and `sandboxes/weather/terrain_gen.cpp`. Selected when `EnvParams::plate_count > 0` (default 12). Four pipeline stages replace the old three-stage noise pipeline:

### Stage 1: Plate generation with velocity bias

`plate_count` plates (clamped 4–30) receive seeded random centers (jittered 10–90% of map extent), velocity vectors (angle uniform on [0, 2π], speed 1–4), and type (continental/oceanic from `continental_ratio`, minimum 2 continental guaranteed).

Velocity biases simulate realistic dynamics:
- **Continental plates**: +1.8 bias toward map center (supercontinent assembly, concentrates mountain ranges)
- **Oceanic plates**: +0.6 bias away from map center (seafloor spreading, enhances mid-ocean divergence)

### Stage 2: Domain-warped Voronoi

Coordinates are warped before Voronoi plate lookup using two noise layers:
- WARP1 (35-tile amplitude, 0.005 freq): large-scale continental shaping
- WARP2 (12-tile amplitude, 0.012 freq): boundary detail and irregularity

Per-tile: boundary distance = `dist_second − dist_closest`, boundary normal/tangent unit vectors, signed convergence value (relative velocity dot boundary normal, normalized to [−1, 1]).

### Stage 3: Continuous tectonic fields (three overlapping contributions)

**Macro — crustal thickness**: `crust_noise` (FBm, 3 octaves, 0.008 freq) ± 0.12 added to plate-type base elevation (`CONT_BASE = max(0.58, water_level+0.12)`, `OCEAN_BASE = 0.22`). Gaussian boundary blending (`exp(−bd²/30²)`) smoothly transitions between neighboring plate types. Continental shield uplift: broad Gaussian (`0.06 × exp(−dist_center²/80²)`) raises plate interiors.

**Meso — boundary stress**: anisotropic ridged noise sampled in boundary-aligned coordinates (5:1 tangent-to-normal stretch ratio) creates elongated mountain ranges. A cross-noise layer at 45° prevents regularity. `belt_var` (low-freq modulation along the range) varies intensity.

Boundary type effects:
- **Continent-continent**: wide plateau (`PLATEAU_WIDTH=65` Gaussian) + sharp ridged peaks (`MOUNTAIN_WIDTH=35`, `MOUNTAIN_PEAK=0.55`)
- **Ocean-continent subduction**: coastal arc at `MOUNTAIN_WIDTH=35` + offshore trench within 10 tiles (`TRENCH_DEPTH=0.06`)
- **Ocean-ocean collision**: island arc at 0.6× mountain width
- **Continental rift**: depression (`RIFT_WIDTH=20`) + rift-shoulder uplift at bd≈15
- **Mid-ocean ridge**: ocean floor depression + narrow central uplift within 5 tiles
- **Transform fault**: minor scarp (`−0.03 × exp(−bd/12)`) + anisotropic ridging

Secondary ridge spurs: when main uplift > 0.03, higher-frequency ridged noise generates foothills in the annular zone between 0.4× and 1.5× `MOUNTAIN_WIDTH` from the boundary.

**Micro — local detail**: 5-octave FBm noise with amplitude scaled by elevation (`clamp((elev−0.3)×3, 0.02, 0.12)`) — richer detail on elevated land.

### Stage 4: Thermal erosion

5 iterations of steepest-descent material transfer (`THERMAL_RATE=0.20`, `MAX_STABLE_SLOPE=0.05`) with elevation-dependent hardness (`hardness = clamp((elev−0.5)×2, 0, 0.7)`) reducing erosion at high peaks. Each iteration followed by a 15/85 blend of Laplacian smoothing (sediment deposition).

### Shared stages 4–10 (unchanged)

Tectonic output feeds into the same shared stages as legacy mode: ocean/lake flood-fill, slope/aspect/bands, EDT distance fields, soil, roughness, D8 downhill routing, flow accumulation.

`EnvParams` gains `plate_count` (uint32_t, default 12) and `continental_ratio` (float, default 0.40). All six worldgen and six weather presets updated with tuned values. The `TectonicPlate` struct and all intermediate arrays remain local to `terrain_gen.cpp`.

## Consequences

**Benefits**:
- Mountain chains form as elongated belts along plate boundaries, producing realistic rain shadows and drainage basins impossible with noise alone
- Continental vs oceanic plate distinction produces the bimodal elevation distribution observed on Earth (high continental plateaus, deep ocean basins)
- Velocity biases concentrate mountain formation at continental collision zones, reducing the number of seeds that produce geographically boring terrain
- Domain-warped Voronoi produces curved, fractal-shaped coastlines and plate boundaries rather than straight Voronoi edges
- Anisotropic ridged noise creates elongated, realistic-looking mountain chains aligned to collision belts
- Thermal erosion rounds off angular tectonic features, producing more geological-looking terrain
- Different collision types (continent-continent, oceanic-continental, island arc) produce geologically distinct mountain styles as emergent properties of the algorithm
- Improved structural realism benefits downstream weather simulation (orographic precipitation, surface runoff patterns)

**Costs / tradeoffs**:
- O(tiles × plates) Voronoi pass — fast in practice (< 200ms for 256×256 with 12 plates) but does not scale as cheaply as noise for very large maps
- All six existing worldgen presets changed meaning: `plate_count` and `continental_ratio` replace `continent_threshold` as the primary shaping controls. `continent_threshold` remains in `EnvParams` but is unused in tectonic mode
- The legacy noise mode is preserved at `plate_count == 0` but is no longer exposed via CLI flags or built-in presets
- Stochastic plate placement means some seeds produce less interesting terrain; mitigated by the wide seed range and tuned preset values
- Four noise generators in the tectonic path (warp1a, warp1b, warp2a, warp2b) plus crust, ridge, and detail noises increase memory and setup cost compared to the old simple Voronoi
