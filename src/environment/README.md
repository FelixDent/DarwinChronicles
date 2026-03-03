# Environment Module

**Documentation:** [docs/systems/PLANETARY_MODEL.md](../../docs/systems/PLANETARY_MODEL.md)

## Overview

The environment module implements the planetary physics model that cascades from stellar parameters down to biological constraints. Star luminosity and orbital distance determine climate, which in turn shapes the conditions for life.

## Key Components

- **`planetary.h/.cpp`** — Star, Planet, Atmosphere structs and physics calculations
- **`climate.h/.cpp`** — Climate computation (temperature, UV, habitability)
- **`params.h/.cpp`** — EnvParams resource derived from climate

## Quick Example

```cpp
#include "environment/planetary.h"
#include "environment/climate.h"
#include "environment/params.h"

// Define a star (Sol-like)
Star star{.luminosity_rel_sun = 1.0f};

// Define atmosphere
Atmosphere atmosphere{
    .pressure_bar = 1.0f,
    .co2_fraction = 0.0004f,
    .o2_fraction = 0.21f,
    .n2_fraction = 0.78f,
    .albedo = 0.3f,
    .ozone_shield = 0.9f,
};

// Create planet
Planet planet(star, 1.0f, atmosphere); // 1.0 AU

// Compute climate
Climate climate = planet.compute_climate();
std::cout << "Mean temp: " << climate.mean_temp_K << "K\n";
std::cout << "Habitability: " << climate.habitability_factor << "\n";

// Derive environmental parameters
EnvParams env_params = EnvParams::from_climate(climate, planet);
```

## Core Concepts

**Three-Layer Cascade:**
1. Star/Orbit/Atmosphere → Climate (physics)
2. Climate → Biological Constraints (tolerance limits)
3. Constraints → Organism Evolution (selection pressures)

**Key Formulas:**
- Stellar flux: `F = L_star / distance²`
- Equilibrium temp: `T_eq ∝ (F × (1 - albedo))^0.25`
- Greenhouse offset: `ΔT = 33K × log₁₀(1 + CO₂ × pressure)`

For full mathematical derivations and implementation details, see the [detailed documentation](../../docs/systems/PLANETARY_MODEL.md).
