# ADR 0004: Energy as the Universal Currency for Emergent Behavior

**Status**: Accepted
**Date**: 2025-02-28
**Deciders**: Project founders

## Context

The simulation needs organisms to develop diverse survival strategies without hard-coded behavior trees or scripted AI. Something must create genuine tradeoffs that natural selection can act on.

Options considered:
1. **Health + mana + stamina** (traditional game resources) — Multiple currencies with designer-tuned interactions
2. **Single energy resource** — One number governs life, death, and reproduction
3. **Multi-objective fitness function** — Explicitly score organisms on multiple axes

## Decision

Use **a single energy value** as the sole resource. Every action has an energy cost derived from physics-inspired formulas:

- Basal metabolism: Kleiber's law (`0.01 × size^0.75`)
- Brain upkeep: `0.001 × synapse_count` per tick
- Movement: kinetic energy (`0.002 × size × speed²`)
- Reproduction threshold: proportional to `body_size + brain_synapses`
- Toxin damage: `toxicity × (1.0 - resistance)` per tick

Organisms die at zero energy. Organisms reproduce when energy exceeds a genome-derived threshold.

## Rationale

A single currency with physics-based costs creates automatic tradeoffs without designer tuning. A large organism has more presence but higher basal cost. A smart organism makes better decisions but pays per-synapse. A fast organism escapes threats but burns kinetic energy. No combination is universally optimal — the environment determines what works.

## Consequences

- **Good**: No hand-tuned balance required. Costs emerge from physical formulas.
- **Good**: Every genome mutation faces immediate energy-budget pressure
- **Good**: Diverse strategies (sprinter, forager, tank, generalist) emerge naturally from different energy allocation patterns
- **Risk**: If cost formulas are poorly scaled, one strategy dominates and diversity collapses
- **Mitigation**: Biome variety and climate shifts continuously change the optimal strategy
