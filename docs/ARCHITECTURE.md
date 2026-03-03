# Architecture

## Module Graph

Six static libraries with an acyclic dependency graph. Each is `darwin_<module>` with a `darwin::<module>` CMake alias.

```
environment       ← planetary physics, star/orbit/atmosphere, climate computation
    ↑
   world          ← procedural terrain (FastNoiseLite), 15 biome types, nutrient fields
    ↑
organisms         ← genome (152 floats), neural brain, metabolism, sensors
    ↑
evolution         ← reproduction, Gaussian mutation, natural selection
    ↑
simulation        ← tick loop, system scheduling, time control
    ↑
rendering         ← SDL2 sprite gen, camera, debug viz, UI
```

`environment` and `organisms` are independent leaf modules. `world` depends on `environment`. `evolution` depends on `organisms`. `simulation` depends on all four above. `rendering` depends on `simulation` + `world` + `organisms`.

## Three-Layer Cascade

The core design principle: physics drives biology, not the other way around.

1. **Planetary physics** — Star luminosity, orbital distance, atmosphere composition → equilibrium temperature, UV flux, habitability zone.
2. **Climate constraints** — Temperature ranges, moisture distribution, toxicity fields → biome classification, nutrient availability, movement costs.
3. **Evolutionary pressure** — Energy scarcity, metabolic costs, mutation rates → organism adaptation, population dynamics, speciation.

Changing a single parameter (e.g., atmospheric CO₂) cascades through all three layers.

## Entity Component System

All organism state lives in EnTT components, not in class hierarchies.

**Core components**: Genome, Brain, Energy, GridPosition, BodySize, Speed, MetabolismRate, ToxinResistance.

**System execution order** (per tick):
1. **Environment** — Regenerate nutrients across the grid
2. **Sense** — Sample local gradients (nutrients, toxins, temperature)
3. **Think** — Forward pass through genome-encoded neural network
4. **Act** — Apply motor outputs as movement
5. **Metabolize** — Deduct energy costs (basal + brain + movement)
6. **Learn** — Hebbian weight updates within lifetime
7. **Reproduce** — Clone + mutate if energy exceeds threshold
8. **Death** — Remove entities at zero energy

Systems are stateless functions over component views. No system holds mutable state between ticks.

## Energy as Universal Currency

Every action costs energy. This creates genuine tradeoffs without scripted balancing:

| Cost source | Formula | Effect |
|---|---|---|
| Basal metabolism | Kleiber's law: `0.01 × size^0.75` | Larger bodies have higher upkeep |
| Brain | `0.001 × synapse_count` per tick | Smarter organisms burn more |
| Movement | `0.002 × size × speed²` | Kinetic energy cost |
| Reproduction | Proportional to `body_size + brain_synapses` | Complex organisms need more energy to reproduce |
| Toxin damage | `toxicity × (1.0 - resistance)` per tick | Unresistant organisms die in toxic zones |

Organisms that over-invest in any single trait die from energy depletion. This alone produces diverse emergent strategies.

## Genome Structure

152 contiguous floats, divided into:

- **Architecture genes** (indices 0–3): Hidden layer count (0–2), neurons per layer (2–8)
- **Morphology genes** (indices 4–7): Body size, limb count, eye count, color traits
- **Physiology genes** (indices 8–15): Speed, metabolism efficiency, toxin resistance, reproduction threshold
- **Neural weights** (indices 16–151): Connection weights for the brain network

Architecture genes define the brain's topology. Morphology genes map directly to sprite appearance. Physiology genes set metabolic parameters. Neural weights are the actual decision-making machinery.

## World Grid

Flat array of tiles. Each tile stores:
- **Biome type** — One of 15 types (Ocean, Tundra, Boreal Forest, Temperate Forest, Rainforest, Grassland, Savanna, Hot Desert, Cold Desert, Swamp, Volcanic, Toxic Wasteland, etc.)
- **Nutrient level** — Current available food (0.0–1.0 of carrying capacity)
- **Toxicity** — Environmental poison level
- **Elevation** — Height value from noise generation
- **Temperature** — Derived from latitude + elevation + noise
- **Moisture** — BFS from water sources with exponential decay

Biomes are classified from the intersection of temperature, moisture, elevation, and toxicity fields — not hand-placed.

## Procedural Generation Pipeline

Four noise fields → biome classification → tile properties:

1. **Height** — Multi-octave Perlin noise. Sea level at 0.4.
2. **Temperature** — Latitude gradient + elevation lapse rate (-6.5°C/km) + local noise.
3. **Moisture** — BFS flood-fill from ocean tiles, exponential decay inland.
4. **Toxicity** — Sparse localized hotspots via high-threshold noise.

Each field is generated independently, then combined to classify biomes using threshold logic.

## Sandboxes

`sandboxes/<name>/` contains standalone executables for iterating on subsystems in complete isolation from the `darwin::*` module tree. A sandbox links only SDL2, FastNoiseLite, and `darwin_warnings` — no `darwin_*` static libraries. The intent is that a sandbox proves out an algorithm or data structure, after which the code can be promoted into the appropriate `darwin::*` module.

The `add_darwin_sandbox(name)` CMake helper in `sandboxes/CMakeLists.txt` sets up this dependency set uniformly. Each sandbox builds to `build/sandboxes/<name>/sandbox_<name>` and is launched with `make run-sandbox SANDBOX=<name>`.

### sandboxes/worldgen

Interactive world generation testbed. Generates a full tile map from configurable planetary parameters and renders it with SDL2. Features:

- Six overlay modes: biome color, height, temperature, moisture, toxicity, raw (1-6 keys)
- Pan (WASD / arrows), zoom (scroll wheel), grid toggle (G)
- Live parameter tweaking without recompile: Tab cycles through Seed, Humidity, Toxicity, NutrientRichness, MeanTemp, NoiseScale, WaterLevel; `[` / `]` decrement / increment
- Regenerate on demand (R) or with a random seed (Space)
- Screenshot export (F12 / Ctrl+F12) and generation log export (L)
- Per-tile readout (biome, height, temperature, moisture) in the window title under cursor

Source files: `config.h/cpp` (CLI parsing, presets), `world_gen.h/cpp` (generation pipeline, stats), `renderer.h/cpp` (tile rendering, overlays, camera), `telemetry.h/cpp` (FPS counter, timing), `main.cpp` (event loop, input handling).

Uses `sandbox` namespace throughout.

## File Conventions

- Public headers: `include/darwin/<module>/<file>.h`
- Implementation: `src/<module>/<file>.cpp`
- Module build: `src/<module>/CMakeLists.txt`
- Module docs: `src/<module>/README.md`
- System deep-dives: `docs/systems/<SYSTEM>.md`
- Decision records: `docs/decisions/<NNNN>-<slug>.md`
- Sandbox sources: `sandboxes/<name>/*.{h,cpp}` (headers are local; no `include/` layer)
