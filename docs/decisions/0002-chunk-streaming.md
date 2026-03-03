# ADR 0002: Fixed-Size World Grid Without Chunk Streaming

**Status**: Accepted
**Date**: 2025-02-28
**Deciders**: Project founders

## Context

The world needs to store per-tile data (biome, nutrients, toxicity, elevation, temperature, moisture) and support fast spatial queries for organism sensing. Two approaches:

1. **Chunk streaming** — Load/unload world regions dynamically as the camera moves. Supports unbounded worlds.
2. **Fixed flat array** — Allocate the entire world grid up front. Bounded size, simple indexing.

## Decision

Use a **fixed-size flat array** for the world grid. The entire world is generated at startup and held in memory.

Grid dimensions are set by the scenario configuration (e.g., 512×512 or 1024×1024 tiles). Each tile is a compact struct (~32 bytes), so a 1024×1024 world is ~32 MB — well within budget.

## Consequences

- **Good**: O(1) tile access by coordinate. No cache miss penalties from pointer chasing.
- **Good**: Nutrient regeneration can iterate linearly over the flat array — cache-friendly
- **Good**: Organism spatial queries (sensor gradients) are simple index arithmetic
- **Good**: No streaming complexity, no chunk boundary edge cases
- **Tradeoff**: World size is bounded. A 2048×2048 world (~128 MB) is practical; beyond that, reconsider.
- **Tradeoff**: Full world generation at startup means a loading pause — acceptable for a simulation
