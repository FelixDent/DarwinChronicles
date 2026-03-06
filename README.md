# Darwin Chronicles

A scientifically grounded 2D evolution simulation built in C++ with an Entity Component System architecture.

## Overview

Darwin Chronicles is an emergent sandbox where players observe evolution unfold in real-time. Rather than controlling creatures directly, you shape the environment—adjusting planetary parameters, climate, and resource distribution—and watch as autonomous organisms with genome-encoded neural networks adapt, learn, and evolve across generations.

**Key Features:**
- **Procedurally generated worlds** driven by stellar and planetary physics
- **Genome-encoded organisms** with variable neural network architectures
- **Emergent behavior** through energy-constrained decision-making
- **Observable evolution** via visual trait changes and population dynamics
- **Multi-scale simulation** from planetary climate down to individual neurons

## Technical Stack

- **Language**: C++20 (performance, type safety, concurrency)
- **Build System**: CMake
- **ECS**: EnTT or custom Entity Component System (parallelization, cache-friendly)
- **Rendering**: 2D tile-based with procedural sprites (SDL2/SFML)
- **Noise**: FastNoiseLite (procedural generation)
- **Physics**: Simplified climate models, allometric scaling, energy budgets

## Repository Structure

```
darwin-chronicles/
├── assets/                          # Game assets and data
│   ├── sprites/                     # Sprite textures and atlases
│   └── procedural/                  # Procedurally generated assets (output)
│
├── src/
│   ├── main.cpp                     # Application entry point
│   │
│   ├── environment/                 # Planetary & environmental systems
│   │   ├── planetary.h/.cpp         # Star, planet, atmosphere models
│   │   ├── climate.h/.cpp           # Temperature, UV, habitability
│   │   └── params.h/.cpp            # Environmental parameters
│   │
│   ├── world/                       # World generation & grid management
│   │   ├── grid.h/.cpp              # WorldGrid resource, tile access
│   │   ├── generation.h/.cpp        # Procedural terrain generation
│   │   ├── biomes.h/.cpp            # Biome classification & properties
│   │   ├── fields.h/.cpp            # Macro field generation (noise)
│   │   └── regeneration.h/.cpp      # Nutrient regeneration systems
│   │
│   ├── organisms/                   # Organism genetics & physiology
│   │   ├── genome.h/.cpp            # Genome structure & trait extraction
│   │   ├── brain.h/.cpp             # Variable-depth neural networks
│   │   ├── metabolism.h/.cpp        # Energy systems & costs
│   │   ├── sensors.h/.cpp           # Environmental sensing
│   │   └── components.h             # ECS components
│   │
│   ├── evolution/                   # Reproduction & mutation
│   │   ├── reproduction.h/.cpp      # Reproduction mechanics
│   │   ├── mutations.h/.cpp         # Genome mutation operators
│   │   └── selection.h/.cpp         # Death & survival systems
│   │
│   ├── simulation/                  # Core simulation loop
│   │   ├── tick.h/.cpp              # Simulation step orchestration
│   │   ├── organism_update.h/.cpp   # Sense→Think→Act→Metabolize loop
│   │   └── scheduling.h/.cpp        # System execution order
│   │
│   └── rendering/                   # Visualization & UI
│       ├── sprite_gen.h/.cpp        # Procedural sprite generation
│       ├── camera.h/.cpp            # Camera controls & zoom
│       ├── debug_viz.h/.cpp         # Debug overlays
│       └── ui.h/.cpp                # User interface panels
│
├── sandboxes/                       # Isolated subsystem prototypes (SDL2 only)
│   ├── worldgen/                    # Terrain generation testbed
│   ├── weather/                     # Atmospheric weather simulation
│   ├── veggen/                      # Vegetation simulation
│   └── spritetest/                  # Sprite rendering test harness
│
├── tests/                           # Catch2 integration tests
├── benches/                         # Google Benchmark suite
├── examples/                        # Runnable scenario examples
│   ├── earth_like.cpp
│   └── mars_like.cpp
│
├── docs/                            # Detailed documentation
│   ├── systems/                     # System-specific design docs
│   │   ├── TERRAIN_GENERATION.md
│   │   ├── WEATHER_SYSTEM.md
│   │   └── PLANT_GENERATION.md
│   ├── decisions/                   # Architecture Decision Records (ADRs)
│   └── ideation/                    # Design notes (not maintained docs)
│
├── CMakeLists.txt                   # Build configuration
└── README.md                        # This file
```

## Core Systems

### 1. Parameterized Environment Model

**What:** Planetary physics (star luminosity, orbital distance, atmospheric composition) cascade through simplified climate models to determine surface conditions.

**Why:** Changing a single parameter (e.g., star distance) produces visibly different worlds—snowball planets, desert worlds, jungle super-Earths—without hand-tuning every detail.

**Components:**
- Star/orbit/atmosphere parameters → climate (temperature, UV, habitability)
- Climate → biological constraints (nutrient richness, mutation rates, toxicity)
- Biosphere constraints → organism evolution pressures

**See:** [`docs/systems/TERRAIN_GENERATION.md`](docs/systems/TERRAIN_GENERATION.md) (sandbox prototype)

---

### 2. Procedural World Generation

**What:** Noise-based generation of continuous macro fields (elevation, temperature, moisture, toxicity) which classify into biomes, each with distinct visual appearance and resource properties.

**Why:** Creates diverse, natural-looking terrain that reflects planetary climate. A hot, dry planet generates deserts; a wet, temperate planet generates forests.

**Components:**
- Noise fields (Perlin/Simplex) → height, temperature, moisture, toxicity
- Biome classification (15 types: ocean, tundra, rainforest, toxic variants, etc.)
- Tile properties (nutrients, regen rates, passability) derived from biomes

**See:** [`docs/systems/TERRAIN_GENERATION.md`](docs/systems/TERRAIN_GENERATION.md)

---

### 3. Dynamic Nutrient Regeneration

**What:** Nutrients regenerate continuously based on biome type and environmental parameters. Organisms consume nutrients, creating depletion zones and resource pressure.

**Why:** Makes the world feel alive. Population booms deplete resources, forcing migration or die-offs. Different biomes support different population densities naturally.

**Components:**
- Biome-specific regen rates (rainforest: 0.06/s, desert: 0.005/s)
- Environmental scaling (humidity, toxicity modify base rates)
- WorldGrid resource with efficient flat-array storage
- Gradient sensing for organism navigation

*(Planned — not yet implemented. See `src/world/` stubs.)*

---

### 4. Evolving Neural Controllers

**What:** Each organism's genome encodes neural network architecture (depth, width), body morphology (size, speed), and metabolic parameters. All traits have energy costs, creating evolutionary tradeoffs.

**Why:** Intelligence, size, and speed aren't free. Evolution optimizes the brain/body configuration for the environment. Small, dumb organisms thrive in sparse deserts; large, smart ones dominate rich jungles.

**Components:**
- Genome (152 floats) → architecture genes, morphology genes, physiology genes, weights
- Variable-depth feedforward networks (0-2 hidden layers, 2-8 neurons)
- Hebbian learning (within-lifetime plasticity)
- Energy costs: brain (per synapse), movement (kinetic), basal (allometric), reproduction

*(Planned — not yet implemented. See `src/organisms/` stubs.)*

---

### 5. Procedural Sprite Generation

**What:** Plant visual appearance is deterministically generated per-archetype from genome-like parameters. Organism sprites will be generated from their actual genome. Same parameters always produce the same appearance.

**Why:** Players can visually track evolution. Color, size, pattern, and structural complexity will all reflect genetic traits. Prototyped in the veggen sandbox.

**Components:**
- Archetype parameters → layered pixel art sprite (trunk, branches, foliage, health states)
- Health-state post-processing on shared structure (never regenerates skeleton per health state)
- Genome → SpriteDescriptor (planned for organism rendering module)

**See:** [`docs/systems/PLANT_GENERATION.md`](docs/systems/PLANT_GENERATION.md)

---

## Design Philosophy

Darwin Chronicles is built around three core principles:

### 1. Scientific Grounding

Use simplified but real physics wherever possible:
- **Radiative equilibrium** for planetary temperatures (Stefan-Boltzmann law)
- **Greenhouse effect** from atmospheric composition
- **Allometric scaling** for metabolic costs (Kleiber's law)
- **Kinetic energy** for movement costs

This creates intuitive cause-and-effect relationships players can understand and predict.

### 2. Emergent Complexity

Simple rules at low levels produce complex behaviors at high levels:
- **No hard-coded "survival instinct"** — organisms that find food and avoid toxins simply live longer and reproduce more
- **No pre-designed species** — all diversity emerges from mutation and selection
- **No scripted events** — population booms, migrations, and extinctions arise naturally from resource dynamics

### 3. Observable Evolution

Players must be able to **see** evolution happening:
- **Visual feedback:** Color shifts, size changes, pattern emergence
- **Population dynamics:** Boom/bust cycles, migration waves, niche separation
- **Spatial patterns:** Species distributions matching resource availability
- **Trait histograms:** Real-time plots of brain complexity, body size, etc.

Evolution shouldn't be a black box—it should be a spectacle.

---

## Quick Start

### Prerequisites

- C++20 compatible compiler (GCC 12+, Clang 14+, MSVC 2022+)
- CMake 3.20+
- Git

### Build & Run

```bash
# Clone repository
git clone https://github.com/yourusername/darwin-chronicles.git
cd darwin-chronicles

# Configure and build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# Run Earth-like scenario (recommended first experience)
./build/examples/earth_like

# Run harsh Mars-like scenario
./build/examples/mars_like

# Run lush jungle planet
./build/examples/jungle_planet
```

### Controls

- **Mouse wheel**: Zoom in/out
- **Middle mouse drag**: Pan camera
- **Space**: Pause/unpause simulation
- **Tab**: Toggle debug overlays (nutrients, temperature, toxicity)
- **1-9**: Speed controls (1x, 2x, 4x, etc.)
- **ESC**: Settings menu

---

## Project Status

**Current Phase:** Sandbox prototyping — core subsystems proven in isolation, not yet promoted to main module tree

**Prototyped (sandboxes):**
- [x] Procedural terrain generation — continent mask, ridged noise, elevation bands, slope, BFS water distances, soil, roughness, downhill routing (`sandboxes/worldgen/`)
- [x] Atmospheric weather simulation — static bake (temperature, wind, precipitation, storminess, rain shadow) + atmosphere simulation (coarse-grid T/q/wind/cloud/precip, latent heat, solar forcing, advection, convective gusts, 10 physical invariants enforced) + dynamic terrain tick (rain/snow, snowmelt, infiltration, runoff, evaporation) (`sandboxes/weather/`)
- [x] Vegetation simulation — two-layer moisture model, hydrology pipeline, canopy feedback, 8 archetypes, 5 death causes, metrics export (`sandboxes/veggen/`)
- [x] Procedural plant sprite generation — genome-driven per-archetype rendering with health states (`sandboxes/veggen/plant_sprite.cpp`)

**Stub (module tree — not yet implemented):**
- [ ] Planetary climate model (`src/environment/`)
- [ ] WorldGrid resource & nutrient regeneration (`src/world/`)
- [ ] Genome structure & neural network (`src/organisms/`)
- [ ] Reproduction & mutation systems (`src/evolution/`)
- [ ] Core simulation tick loop (`src/simulation/`)
- [ ] Rendering & UI (`src/rendering/`)

**Planned:**
- [ ] Promote sandbox subsystems into `darwin::*` module tree
- [ ] Complete organism update loop (sense→think→act→metabolize)
- [ ] Save/load simulations
- [ ] Phylogenetic tree visualization
- [ ] Performance optimizations (spatial hashing, chunking)

---

## Documentation

### System Design
- [Terrain Generation](docs/systems/TERRAIN_GENERATION.md) — Continent mask, elevation bands, slope, soil, downhill routing
- [Weather System](docs/systems/WEATHER_SYSTEM.md) — Static bake, atmosphere simulation, dynamic terrain tick
- [Plant Generation](docs/systems/PLANT_GENERATION.md) — Vegetation simulation, hydrology, procedural plant sprites

### Guides
- [Getting Started](docs/guides/GETTING_STARTED.md) — Installation & first steps
- [Scenario Creation](docs/guides/SCENARIO_CREATION.md) — Creating custom worlds
- [Tuning Guide](docs/guides/TUNING_GUIDE.md) — Balancing parameters

### Architecture
- [ECS Organization](docs/architecture/ECS_ORGANIZATION.md) — Component/system structure
- [Performance](docs/architecture/PERFORMANCE.md) — Optimization strategies

---

## Contributing

Contributions welcome! See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

**Areas of Interest:**
- Additional biome types & environmental hazards
- Alternative neural network architectures (RNNs, attention mechanisms)
- Visualization improvements (shaders, particle effects)
- Performance profiling & optimization
- Educational scenarios (textbook examples of evolution)

---

## License

This project is licensed under the MIT License - see [LICENSE](LICENSE) for details.

---

## Acknowledgments

Inspired by:
- **Tierra** (Thomas S. Ray) — Digital evolution
- **Avida** (Adami et al.) — Evolving computer programs
- **PolyWorld** (Larry Yaeger) — Neural network evolution
- **The Bibites** — Modern accessible evolution simulation

Built with:
- [EnTT](https://github.com/skypjack/entt) — ECS framework
- [FastNoiseLite](https://github.com/Auburn/FastNoiseLite) — Procedural generation
- [SDL2](https://www.libsdl.org/) / [SFML](https://www.sfml-dev.org/) — Rendering

---

**Darwin Chronicles** — Watch evolution unfold, one generation at a time.
