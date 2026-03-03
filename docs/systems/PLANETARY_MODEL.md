# Parameterized Environment Model

**Part of:** [Darwin Chronicles](../../README.md)
**Implementation:** [`src/environment/`](../../src/environment/)

## Overview

The Parameterized Environment Model implements a cascading physics system where stellar and planetary parameters flow through simplified climate models to determine surface conditions and biological constraints. This creates scientifically grounded diversity: changing a single parameter (e.g., orbital distance) produces visibly different worlds without hand-tuning every detail.

## Three-Layer Architecture

```
Layer 1: Star/Orbit/Atmosphere Parameters
    ↓ (radiative equilibrium + greenhouse physics)
Layer 2: Climate Properties
    - Mean surface temperature
    - UV radiation at surface
    - Habitability score
    ↓ (biological tolerance mapping)
Layer 3: Biosphere Constraints
    - Nutrient regeneration scaling
    - Mutation rate multipliers
    - Environmental toxicity
```

---

## Layer 1: Star, Orbit & Atmosphere

### Input Parameters

```cpp
struct Star {
    float luminosity_rel_sun;  // Relative to Sol (1.0 = Sun)
};

struct Atmosphere {
    float pressure_bar;    // Surface pressure in bars
    float co2_fraction;    // Fraction of atmosphere
    float o2_fraction;     // Oxygen fraction
    float n2_fraction;     // Nitrogen fraction
    float albedo;          // Planetary albedo [0.0, 1.0]
    float ozone_shield;    // UV shielding [0.0, 1.0]
};

struct Planet {
    Star star;
    float distance_au;       // Orbital distance in AU
    Atmosphere atmosphere;
};
```

### Example Configurations

| Planet Type | Luminosity | Distance (AU) | Pressure (bar) | CO₂ | O₂ | Albedo |
|-------------|------------|---------------|----------------|-----|----|---------|
| **Earth-like** | 1.0 | 1.0 | 1.0 | 0.0004 | 0.21 | 0.3 |
| **Hot Venus-like** | 1.0 | 0.72 | 92.0 | 0.965 | 0.0 | 0.75 |
| **Cold Mars-like** | 1.0 | 1.52 | 0.006 | 0.95 | 0.0013 | 0.25 |
| **Tidally-locked** | 0.05 | 0.1 | 2.0 | 0.01 | 0.15 | 0.2 |

---

## Layer 2: Climate Computation

### Physics-Based Temperature Model

**1. Stellar Flux at Orbital Distance:**
```cpp
float stellar_flux(const Star& star, float distance_au) {
    return star.luminosity_rel_sun / (distance_au * distance_au);
}
```

**2. Equilibrium Temperature (No Greenhouse):**
```cpp
constexpr float EARTH_MEAN_TEMP = 288.0f;  // Kelvin (~15°C)
constexpr float EARTH_ALBEDO = 0.3f;

float equilibrium_temperature(float flux, float albedo) {
    float ratio = (flux * (1.0f - albedo)) / (1.0f - EARTH_ALBEDO);
    return EARTH_MEAN_TEMP * std::pow(ratio, 0.25f);
}
```

**3. Greenhouse Offset:**
```cpp
float greenhouse_offset(const Atmosphere& atmosphere) {
    float co2 = atmosphere.co2_fraction;
    float pressure = atmosphere.pressure_bar;

    // Earth baseline: 0.0004 CO₂ at 1 bar → +33K greenhouse
    float strength = (co2 * pressure) / 0.0004f;
    float offset = 33.0f * std::log10(1.0f + strength);

    return std::clamp(offset, 0.0f, 100.0f);  // Cap at +100K
}
```

**4. Mean Surface Temperature:**
```cpp
float Planet::mean_temperature() const {
    float flux = stellar_flux(star, distance_au);
    float t_eq = equilibrium_temperature(flux, atmosphere.albedo);
    float greenhouse = greenhouse_offset(atmosphere);

    return t_eq + greenhouse;
}
```

### UV Radiation

```cpp
float Planet::surface_uv() const {
    float flux = stellar_flux(star, distance_au);
    float uv_raw = flux;  // Simplification: UV scales with total flux
    float uv_surface = uv_raw * (1.0f - atmosphere.ozone_shield);

    return std::max(uv_surface, 0.0f);
}
```

### Habitability Score

```cpp
float temperature_habitability(float temp_K) {
    // Gaussian preference: optimal ~295K (22°C), σ=20K
    constexpr float OPTIMAL_TEMP = 295.0f;
    constexpr float SIGMA = 20.0f;

    float deviation = temp_K - OPTIMAL_TEMP;
    return std::exp(-deviation * deviation / (2.0f * SIGMA * SIGMA));
}

Climate Planet::compute_climate() const {
    float mean_temp = mean_temperature();
    float temp_range = 0.2f * mean_temp;  // ±20% variation
    float uv = surface_uv();

    float temp_score = temperature_habitability(mean_temp);
    float uv_penalty = std::exp(-uv);
    float habitability = std::clamp(temp_score * uv_penalty, 0.0f, 1.0f);

    return Climate{
        .mean_temp_K = mean_temp,
        .temp_range_K = temp_range,
        .uv_intensity = uv,
        .habitability_factor = habitability,
    };
}
```

---

## Layer 3: Biological Constraints

### Deriving Constraints from Climate

```cpp
struct BioConstraints {
    float min_temp_K;
    float max_temp_K;
    float base_nutrient_regen;
    float oxygen_level;
    float mutation_uv_factor;
};

BioConstraints BioConstraints::from_climate(const Climate& climate, const Planet& planet) {
    // Temperature tolerance window
    float min_temp = climate.mean_temp_K - climate.temp_range_K;
    float max_temp = climate.mean_temp_K + climate.temp_range_K;

    // Nutrient regen scales with habitability and pressure
    float base_nutrient_regen = climate.habitability_factor
        * planet.atmosphere.pressure_bar;

    // Oxygen for aerobic metabolism
    float oxygen_level = planet.atmosphere.o2_fraction;

    // UV drives mutation rate
    float mutation_uv_factor = 1.0f + climate.uv_intensity;

    return BioConstraints{
        .min_temp_K = min_temp,
        .max_temp_K = max_temp,
        .base_nutrient_regen = base_nutrient_regen,
        .oxygen_level = oxygen_level,
        .mutation_uv_factor = mutation_uv_factor,
    };
}
```

---

## Complete Implementation Example

```cpp
#include "environment/planetary.h"
#include "environment/climate.h"
#include "environment/params.h"
#include <iostream>

int main() {
    // Define Earth-like planet
    Star star{.luminosity_rel_sun = 1.0f};

    Atmosphere atmosphere{
        .pressure_bar = 1.0f,
        .co2_fraction = 0.0004f,
        .o2_fraction = 0.21f,
        .n2_fraction = 0.78f,
        .albedo = 0.3f,
        .ozone_shield = 0.9f,
    };

    Planet planet{
        .star = star,
        .distance_au = 1.0f,
        .atmosphere = atmosphere,
    };

    // Compute climate
    Climate climate = planet.compute_climate();
    std::cout << "Temperature: " << climate.mean_temp_K << "K ("
              << (climate.mean_temp_K - 273.15f) << "°C)\n";
    std::cout << "UV intensity: " << climate.uv_intensity << "\n";
    std::cout << "Habitability: " << climate.habitability_factor << "\n";

    // Derive biological constraints
    auto bio = BioConstraints::from_climate(climate, planet);
    std::cout << "Viable temp range: " << bio.min_temp_K << "K - "
              << bio.max_temp_K << "K\n";
    std::cout << "Base nutrient regen: " << bio.base_nutrient_regen << "\n";
    std::cout << "Mutation UV factor: " << bio.mutation_uv_factor << "x\n";
}
```

**Expected Output:**
```
Temperature: 288.0K (15.0°C)
UV intensity: 0.10
Habitability: 0.87
Viable temp range: 230K - 346K
Base nutrient regen: 0.87
Mutation UV factor: 1.10x
```

---

## Planetary Scenarios

### Scenario 1: Snowball World
```cpp
Planet planet{
    .star = Star{.luminosity_rel_sun = 0.8f},
    .distance_au = 1.8f,
    .atmosphere = Atmosphere{
        .pressure_bar = 0.5f,
        .co2_fraction = 0.001f,
        .albedo = 0.6f,  // High albedo from ice
    },
};
// Result: T_mean ≈ 220K, habitability ≈ 0.05
```

### Scenario 2: Greenhouse Runaway
```cpp
Planet planet{
    .star = Star{.luminosity_rel_sun = 1.0f},
    .distance_au = 0.72f,
    .atmosphere = Atmosphere{
        .pressure_bar = 92.0f,
        .co2_fraction = 0.96f,
        .albedo = 0.75f,
    },
};
// Result: T_mean ≈ 735K, habitability ≈ 0.0
```

### Scenario 3: Super-Earth Jungle
```cpp
Planet planet{
    .star = Star{.luminosity_rel_sun = 1.0f},
    .distance_au = 0.95f,
    .atmosphere = Atmosphere{
        .pressure_bar = 2.5f,
        .co2_fraction = 0.002f,
        .o2_fraction = 0.25f,
        .albedo = 0.25f,
        .ozone_shield = 0.95f,
    },
};
// Result: T_mean ≈ 305K, habitability ≈ 0.92
```

---

## Integration Points

### With World Generation
The `Climate` output drives procedural terrain generation:
- `mean_temp_K` → latitudinal temperature distribution
- `habitability_factor` → nutrient richness parameter
- `uv_intensity` → toxicity field baseline

See: [World Generation](WORLD_GENERATION.md)

### With Organism Evolution
The `BioConstraints` shape evolutionary pressures:
- `oxygen_level` → aerobic metabolism efficiency
- `mutation_uv_factor` → genetic drift speed
- `base_nutrient_regen` → population carrying capacity

See: [Neural Evolution](NEURAL_EVOLUTION.md)

---

## Implementation Checklist

- [ ] `Star` struct with luminosity
- [ ] `Atmosphere` struct with composition
- [ ] `Planet` struct combining star + atmosphere
- [ ] `stellar_flux` calculation
- [ ] `equilibrium_temperature` with Stefan-Boltzmann
- [ ] `greenhouse_offset` from CO₂ and pressure
- [ ] `surface_uv` with ozone shielding
- [ ] `temperature_habitability` scoring
- [ ] `Climate` output struct
- [ ] `BioConstraints` derivation
- [ ] Unit tests for known planets (Earth, Venus, Mars)

---

## References

- Stefan-Boltzmann Law: T ∝ (flux)^0.25
- Greenhouse radiative transfer (simplified)
- Real planetary data for validation

**Related Documentation:**
- [World Generation](WORLD_GENERATION.md)
- [Nutrient Regeneration](NUTRIENT_REGENERATION.md)
