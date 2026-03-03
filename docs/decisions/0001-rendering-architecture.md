# ADR 0001: SDL2 Tile-Based Rendering with Procedural Sprites

**Status**: Accepted
**Date**: 2025-02-28
**Deciders**: Project founders

## Context

Darwin Chronicles needs to render hundreds to thousands of organisms on a tiled world map in real time. Organisms must be visually distinguishable by their genetic traits so players can observe evolution happening.

Options considered:
1. **SDL2 with software rendering** — Simple, portable, sufficient for 2D tile grids
2. **SDL2 with GPU-accelerated textures** — Hardware-accelerated blitting via `SDL_Renderer`
3. **OpenGL/Vulkan directly** — Maximum performance, high complexity
4. **Raylib** — Higher-level 2D/3D engine, less control

## Decision

Use **SDL2 with hardware-accelerated texture rendering** (`SDL_Renderer` + `SDL_Texture`).

Organism sprites are **procedurally generated from genome data** at birth, cached as textures, and blitted each frame. No sprite sheets for organisms — each one is unique.

Terrain uses **pre-authored tilesets** (PixelOdyssey 16x16) mapped to biome types.

Multi-tile organisms (5–29 tiles based on body size) are composited from genome-derived visual traits: base color from toxin resistance (HSV hue), pattern from neural architecture, eye/limb count from brain dimensions.

## Consequences

- **Good**: Every organism is visually unique — evolution is directly observable
- **Good**: SDL2 is widely available, easy to build from source as fallback
- **Good**: Texture caching amortizes sprite generation cost (only at birth)
- **Risk**: Multi-tile organisms (up to 29 tiles) at high population counts could stress the renderer
- **Mitigation**: LOD system — at far zoom, reduce to single-tile colored dots; full sprites only when zoomed in
