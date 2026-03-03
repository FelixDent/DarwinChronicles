# Organisms Module

**Documentation:** [docs/systems/NEURAL_EVOLUTION.md](../../docs/systems/NEURAL_EVOLUTION.md)

## Overview

The organisms module implements genome-encoded neural networks with variable architecture, body morphology, and metabolic parameters. All traits have realistic energy costs, creating evolutionary tradeoffs between intelligence, size, and speed.

## Key Components

- **`genome.h/.cpp`** — Genome structure (152 floats) and trait extraction methods
- **`brain.h/.cpp`** — Variable-depth neural networks with Hebbian learning
- **`metabolism.h/.cpp`** — Energy systems, costs (brain, movement, basal, reproduction)
- **`sensors.h/.cpp`** — Environmental sensing (nutrient/toxin gradients, internal state)
- **`components.h`** — ECS components (Organism, Brain, Energy, etc.)

## Quick Example

```cpp
#include "organisms/genome.h"
#include "organisms/brain.h"
#include "organisms/metabolism.h"

// Create random genome
Genome genome = Genome::random(152);

// Extract traits
float body_size = genome.body_size();        // 0.5 - 5.5
float max_speed = genome.max_speed();        // 0.5 - 2.0
size_t brain_depth = genome.brain_depth();   // 0, 1, or 2 hidden layers
size_t brain_width = genome.brain_width();   // 2, 4, or 8 neurons per layer

// Create brain from genome
Brain brain = Brain::from_genome(genome);

// Sense environment
std::vector<float> sensors = {
    nutrient_grad_x,
    nutrient_grad_y,
    toxin_grad_x,
    toxin_grad_y,
    energy_level,
    hunger_level,
    temp_stress,
    uv_exposure,
};

// Think (neural network forward pass)
std::vector<float> outputs = brain.forward(sensors);
float move_x = outputs[0];  // Movement X
float move_y = outputs[1];  // Movement Y

// Compute energy costs
float brain_cost = brain.compute_energy_cost();                      // Per synapse
float move_cost = metabolism::movement_cost(body_size, move_x, move_y);
float basal_cost = metabolism::basal_cost(body_size);
```

## Core Concepts

**Genome Structure (152 floats):**
- Architecture genes (3) → Neural network depth/width
- Morphology genes (2) → Body size, max speed
- Physiology genes (3) → Metabolism, mutation, learning rates
- Controller weights (144) → Neural network synaptic weights

**Energy Costs Create Tradeoffs:**
- Brain: 0.016 - 0.144 energy/step (scales with synapses)
- Movement: ∝ body_size × speed² (kinetic energy)
- Basal: ∝ body_size^0.75 (Kleiber's law)
- Reproduction: ∝ body_size + brain_size

**Emergent Archetypes:**
- Tiny Sprinter: Small, fast, dumb (r-selection)
- Smart Forager: Medium, learns gradients (K-selection)
- Tank: Large, slow, efficient (stable environments)
- Adaptive Generalist: Balanced, evolvable

For complete neural network implementation and energy formulas, see the [detailed documentation](../../docs/systems/NEURAL_EVOLUTION.md).
