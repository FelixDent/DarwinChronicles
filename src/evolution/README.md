# Evolution Module

**Related Documentation:** [docs/systems/NEURAL_EVOLUTION.md](../../docs/systems/NEURAL_EVOLUTION.md)

## Overview

The evolution module handles reproduction, genome mutation, and organism death/selection. Organisms reproduce when they accumulate sufficient energy, passing mutated copies of their genome to offspring.

## Key Components

- **`reproduction.h/.cpp`** — Reproduction mechanics (energy splitting, offspring spawning)
- **`mutations.h/.cpp`** — Genome mutation operators (Gaussian perturbation)
- **`selection.h/.cpp`** — Death conditions and survival checks

## Quick Example

```cpp
#include "evolution/reproduction.h"
#include "evolution/mutations.h"
#include "evolution/selection.h"

// Check if organism can reproduce
if (organism.energy >= organism.replication_threshold) {
    // Create offspring
    Genome child_genome = organism.genome;

    // Apply mutations
    float mutation_rate = organism.genome.baseline_mutation_rate()
        * bio_constraints.mutation_uv_factor;  // Scaled by UV

    child_genome.mutate(mutation_rate);

    // Split energy
    float child_energy = organism.energy / 2.0f;
    organism.energy /= 2.0f;

    // Spawn child organism
    auto child = entity_manager.create();
    entity_manager.emplace<Organism>(child, child_genome, child_energy, /*age=*/0);
    entity_manager.emplace<GridPosition>(child, GridPosition::adjacent_to(organism_pos));
}

// Check for death
if (organism.energy <= 0.0f) {
    entity_manager.destroy(organism_entity);
}
```

## Core Concepts

**Reproduction Threshold:**
```cpp
threshold = (K_REPRO_BODY * body_size) + (K_REPRO_BRAIN * brain_synapses);
```
- Larger bodies + complex brains = harder to reproduce
- Creates r/K selection pressure

**Mutation Operator:**
- Gaussian perturbation on each gene
- Base rate from genome (evolvability evolves!)
- Scaled by environmental UV (harsh planets → faster evolution)
- Respects gene bounds [0.0, 1.0]

**Death Conditions:**
- Energy ≤ 0 (starvation)
- Optional: Age limit, environmental hazards

**Selection Emerges Naturally:**
- Organisms that find food → survive longer → reproduce more
- No explicit "fitness function" needed
