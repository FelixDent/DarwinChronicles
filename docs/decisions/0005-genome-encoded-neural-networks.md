# ADR 0005: Genome-Encoded Variable-Depth Neural Networks

**Status**: Accepted
**Date**: 2025-02-28
**Deciders**: Project founders

## Context

Organisms need a decision-making mechanism that can be inherited, mutated, and selected for. The mechanism must allow a range from simple reactive agents to more sophisticated planners, and this range must be evolvable.

Options considered:
1. **Behavior trees** — Structured, interpretable, but hard to mutate smoothly
2. **Fixed-topology neural networks** — Evolvable weights, but topology is a design choice
3. **Variable-topology neural networks** (NEAT-style) — Evolve both topology and weights
4. **Genome-encoded variable-depth feedforward networks** — Architecture genes set topology, weight genes set connections

## Decision

Use **genome-encoded feedforward neural networks** with variable depth and width.

Architecture genes (4 floats) determine:
- Number of hidden layers: 0, 1, or 2
- Neurons per hidden layer: 2 to 8

This gives a range from a 8→2 direct mapping (no hidden layers, 16 weights) to a 8→8→8→2 deep network (up to 136 weights). The architecture is encoded in the genome alongside morphology and physiology, so it evolves under the same mutation and selection pressure.

Inputs (8): nutrient gradient N/S/E/W, toxin gradient, energy level, hunger, temperature.
Outputs (2): movement X, movement Y.
Activation: tanh.

Within-lifetime **Hebbian learning** adjusts weights based on correlated activations, allowing individual adaptation without changing the genome.

## Rationale

- Architecture genes let evolution discover the right brain complexity for the environment. In a simple uniform world, no-hidden-layer organisms dominate (cheap brains). In a complex toxic world, deeper networks survive (better gradient navigation).
- Brain complexity has a direct metabolic cost (0.001 per synapse per tick), so evolution can't just make brains bigger — they must earn their upkeep.
- Hebbian learning adds individual plasticity on top of evolutionary adaptation, increasing behavioral diversity within a generation.

## Consequences

- **Good**: Brain complexity is itself an evolvable, selectable trait
- **Good**: No NEAT-style structural mutation complexity — architecture changes through 4 float mutations
- **Good**: Metabolic cost prevents runaway brain growth
- **Tradeoff**: Maximum network size is capped (2 hidden layers, 8 wide) — limits cognitive sophistication
- **Tradeoff**: Feedforward only — no recurrence, no temporal memory
