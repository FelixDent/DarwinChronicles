# ADR 0003: EnTT ECS Over Traditional OOP Hierarchies

**Status**: Accepted
**Date**: 2025-02-28
**Deciders**: Project founders

## Context

Organisms have many orthogonal properties (genome, brain, energy, position, body size, speed, toxin resistance). The simulation processes these properties in system-specific passes (sense, think, act, metabolize). Two main approaches:

1. **Class hierarchy** — `Organism` base class with virtual methods, subclasses for archetypes
2. **Entity Component System** — Entities are IDs, components are plain data, systems are functions over component views

## Decision

Use **EnTT** (v3.13.2) as the ECS framework. All organism state is decomposed into components. Systems are stateless functions that iterate over component views.

Components: `Genome`, `Brain`, `Energy`, `GridPosition`, `BodySize`, `Speed`, `MetabolismRate`, `ToxinResistance`.

System execution is ordered: Sense → Think → Act → Metabolize → Learn → Reproduce → Death.

## Rationale

- **No archetypes exist at design time.** Organism "types" (Tiny Sprinter, Smart Forager, Tank) emerge from evolution. A class hierarchy would require predicting what evolution produces.
- **Cache-friendly iteration.** EnTT stores components contiguously by type. Processing all `Energy` components in one pass is faster than iterating polymorphic objects.
- **Composability.** New components (e.g., adding `Armor` or `Pheromone`) don't require modifying a base class.
- **Parallelism potential.** Independent systems (e.g., nutrient regeneration vs. organism sensing) can run concurrently since they touch different component types.

## Consequences

- **Good**: Data-oriented design matches the simulation's batch-processing nature
- **Good**: EnTT is header-only, mature, well-optimized, and widely used in game dev
- **Tradeoff**: Debugging is harder — no object to inspect, just component bags
- **Tradeoff**: System ordering is manual and must be maintained carefully
