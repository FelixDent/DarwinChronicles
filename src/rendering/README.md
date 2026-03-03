# Rendering Module

**Documentation:** [docs/systems/SPRITE_GENERATION.md](../../docs/systems/SPRITE_GENERATION.md)

## Overview

The rendering module generates deterministic visual representations of organisms from their genome. Each organism's appearance (color, size, pattern, eyes, limbs) directly reflects its genetic traits, making evolution visually observable.

## Key Components

- **`sprite_gen.h/.cpp`** — Genome → SpriteDescriptor pipeline
- **`camera.h/.cpp`** — Camera controls, zoom, panning
- **`debug_viz.h/.cpp`** — Debug overlays (nutrient heatmap, temperature, toxicity)
- **`ui.h/.cpp`** — UI panels (stats, controls, trait histograms)

## Quick Example

```cpp
#include "rendering/sprite_gen.h"

// Generate sprite descriptor from genome
SpriteDescriptor descriptor = sprite_descriptor_from_genome(organism.genome);

// Descriptor contains:
// - footprint: std::vector<Vec2i>   (5-29 tiles based on body size)
// - base_color: Color               (toxin resistance → red-to-green)
// - accent_color: Color             (deterministic variation)
// - pattern: Pattern                (Solid/Striped/Spotted based on behavior)
// - eyes: uint8_t                   (1-3 based on brain complexity)
// - limb_offsets: std::vector<Vec2i> (0-4 based on brain complexity)

// Spawn multi-tile sprite
for (size_t i = 0; i < descriptor.footprint.size(); ++i) {
    Vec2i offset = descriptor.footprint[i];
    Color color;

    switch (descriptor.pattern) {
        case Pattern::Solid:
            color = descriptor.base_color;
            break;
        case Pattern::Striped:
            color = ((offset.x + offset.y) % 2 == 0)
                ? descriptor.base_color
                : descriptor.accent_color;
            break;
        case Pattern::Spotted:
            color = (i % 3 == 0)
                ? descriptor.accent_color
                : descriptor.base_color;
            break;
    }

    renderer.draw_tile(
        position.x + offset.x * TILE_SIZE,
        position.y + offset.y * TILE_SIZE,
        color
    );
}
```

## Core Concepts

**Genome → Visual Trait Mapping:**

| Genome Trait | Visual Feature | Mapping |
|--------------|----------------|---------|
| Body size (0.5-5.5) | Tile count | 5-29 tiles (radius 1-3) |
| Brain depth/width | Eyes | 1-3 eyes |
| Brain complexity | Limbs | 0-4 limb nubs |
| Toxin resistance | Hue | Red (0.0) → Green (1.0) |
| Metabolism | Saturation | Higher → more vibrant |
| Max speed | Pattern | Fast (>1.6) → Striped |
| Risk aversion | Pattern | Cautious (>0.7) → Spotted |

**Deterministic:** Same genome always produces same appearance (seeded RNG)

**Multi-tile Organisms:** Larger/smarter creatures span multiple grid cells

**Observable Evolution:**
- Population shifts from red to green in toxic zones (resistance evolves)
- Three-eyed organisms emerge in complex environments (intelligence)
- Size increases in nutrient-rich biomes (body optimization)
- Striped patterns appear in fast-paced competitive zones

For complete sprite generation algorithms and rendering integration, see the [detailed documentation](../../docs/systems/SPRITE_GENERATION.md).
