# Simulation Module

## Overview

The simulation module orchestrates the main update loop, coordinating all systems in the correct order. It implements the Sense→Think→Act→Metabolize→Learn→Reproduce cycle for organisms.

## Key Components

- **`tick.h/.cpp`** — Simulation step/tick system
- **`organism_update.h/.cpp`** — Complete organism update loop
- **`scheduling.h/.cpp`** — System execution order and dependencies

## Update Loop Order

```cpp
void Simulation::run_schedule() {
    // 1. Environment updates
    nutrient_regeneration_system(world, dt);
    // future: weather, day/night cycle

    // 2. Organism sensing
    organism_sensor_system(registry, world);

    // 3. Organism thinking (neural networks)
    organism_brain_system(registry);

    // 4. Organism acting (movement)
    organism_movement_system(registry, world);

    // 5. Organism metabolism
    organism_eating_system(registry, world);
    organism_basal_metabolism_system(registry);

    // 6. Learning (Hebbian updates)
    organism_learning_system(registry);

    // 7. Reproduction & death
    organism_reproduction_system(registry, world);
    organism_death_system(registry);
}
```

## Complete Organism Update

```cpp
void organism_step(
    Organism& organism,
    WorldGrid& world,
    std::pair<uint32_t, uint32_t> position
) {
    // 1. SENSE
    auto sensors = get_sensor_inputs(organism, world, position);

    // 2. THINK
    auto outputs = organism.brain.forward(sensors);
    organism.energy -= organism.brain.compute_energy_cost();

    // 3. ACT (Move)
    auto [dx, dy] = decode_movement_outputs(outputs, organism.max_speed);
    organism.energy -= movement_cost(organism.body_size, dx, dy);
    auto new_pos = world.try_move(position, dx, dy);

    // 4. METABOLIZE (Eat)
    float consumed = world.consume_nutrients(new_pos, organism.max_food_intake);
    organism.energy += consumed * organism.metabolism_efficiency;

    // Toxin damage
    float toxin_damage = world.tile(new_pos).toxins * (1.0f - organism.toxin_resistance);
    organism.energy -= toxin_damage;

    // Basal metabolism
    organism.energy -= basal_metabolic_cost(organism.body_size);

    // 5. LEARN
    float net_energy_delta = /* compute from above */;
    organism.brain.hebbian_update(net_energy_delta);

    // 6. REPRODUCE or DIE
    if (organism.energy >= organism.replication_threshold) {
        spawn_offspring(organism);
    }
    if (organism.energy <= 0.0f) {
        despawn(organism);
    }
}
```

## Time Management

**Simulation Speed Controls:**
- 1× = real-time (60 FPS)
- 2× = double speed
- 4×, 8×, etc. = progressively faster
- Paused = freeze simulation

**Fixed vs Variable Timestep:**
- Nutrient regen: Fixed 0.2s ticks (predictable)
- Organism updates: Per-frame (smooth movement)
- Time scaling handled by a time manager
