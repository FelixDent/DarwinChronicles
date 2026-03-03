# Procedural Sprite Generation from Genome

**Part of:** [Darwin Chronicles](../../README.md)
**Implementation:** [`src/rendering/`](../../src/rendering/)

## Overview

The Procedural Sprite Generation system creates deterministic visual representations of organisms from their genome. Every organism's appearance—color, size, pattern, eyes, limbs—directly reflects its genetic traits, making evolution visually observable in real-time. When toxin resistance evolves, the population shifts from red to green. When intelligence increases, three-eyed organisms emerge. When body size optimizes for a biome, creatures visibly grow or shrink. This creates an intuitive feedback loop: the player sees evolution happening, not just as numbers in a spreadsheet, but as living, breathing changes in the simulated ecosystem.

## Genome → Visual Trait Pipeline

### Complete Mapping Table

| Genome Trait | Range | Visual Feature | Visual Range | Formula |
|--------------|-------|----------------|--------------|---------|
| **Body size** | 0.5 - 5.5 | Tile count | 5 - 29 tiles | radius = 1 + floor(size / 2) |
| **Brain depth** | 0, 1, 2 | Eyes | 1 - 3 | depth + 1 |
| **Brain width** | 2, 4, 8 | Limbs | 0 - 4 | width / 2 |
| **Toxin resistance** | 0.0 - 1.0 | Hue | 0° - 120° | Red → Green |
| **Metabolism eff.** | 0.6 - 1.0 | Saturation | 40% - 100% | Higher → vibrant |
| **Max speed** | 0.5 - 2.0 | Pattern | Solid/Striped | Fast → Striped |
| **Learning rate** | 0.0 - 0.01 | Pattern | Solid/Spotted | Cautious → Spotted |

---

## Sprite Descriptor Structure

### Core Data Structure

```cpp
struct Vec2i {
    int x, y;
};

struct Color {
    float r, g, b;
};

enum class Pattern {
    Solid,      // Uniform color
    Striped,    // Alternating bands
    Spotted,    // Accent spots on base
};

struct SpriteDescriptor {
    std::vector<Vec2i> footprint;      // Multi-tile organism shape
    Color base_color;                   // Primary body color
    Color accent_color;                 // Secondary color for patterns
    Pattern pattern;                    // Visual pattern type
    uint8_t eyes;                       // Number of eyes (1-3)
    std::vector<Vec2i> limb_offsets;   // Limb/antenna positions
};
```

### Generation Function

```cpp
SpriteDescriptor sprite_descriptor_from_genome(const Genome& genome) {
    // Extract genome traits
    float body_size = genome.body_size();
    size_t brain_depth = genome.brain_depth();
    size_t brain_width = genome.brain_width();
    float toxin_resistance = genome.toxin_resistance();  // Derived trait
    float metabolism_efficiency = genome.metabolism_efficiency();
    float max_speed = genome.max_speed();
    float learning_rate = genome.learning_rate();

    // Generate visual features
    auto footprint = generate_footprint(body_size);
    Color base_color = color_from_traits(toxin_resistance, metabolism_efficiency);
    Color accent_color = accent_color_from_genome(genome);
    Pattern pattern = pattern_from_behavior(max_speed, learning_rate);
    uint8_t eyes = static_cast<uint8_t>(std::min(brain_depth + 1, size_t(3)));
    auto limb_offsets = generate_limbs(brain_width);

    return SpriteDescriptor{
        .footprint = std::move(footprint),
        .base_color = base_color,
        .accent_color = accent_color,
        .pattern = pattern,
        .eyes = eyes,
        .limb_offsets = std::move(limb_offsets),
    };
}
```

---

## Multi-Tile Footprint Generation

### Size-Based Radius

```cpp
std::vector<Vec2i> generate_footprint(float body_size) {
    // Map body size to grid radius
    int radius;
    if (body_size < 1.5f) {
        radius = 1;  // Tiny: 5 tiles (cross pattern)
    } else if (body_size < 3.5f) {
        radius = 2;  // Medium: 13 tiles (diamond)
    } else {
        radius = 3;  // Large: 29 tiles (large diamond)
    }

    return generate_diamond_shape(radius);
}

std::vector<Vec2i> generate_diamond_shape(int radius) {
    std::vector<Vec2i> tiles;

    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            // Manhattan distance for diamond shape
            if (std::abs(dx) + std::abs(dy) <= radius) {
                tiles.push_back({dx, dy});
            }
        }
    }

    return tiles;
}
```

### Footprint Examples

**Radius 1 (5 tiles):**
```
    ■
  ■ ● ■  (● = center/origin)
    ■
```

**Radius 2 (13 tiles):**
```
      ■
    ■ ■ ■
  ■ ■ ● ■ ■
    ■ ■ ■
      ■
```

**Radius 3 (29 tiles):**
```
        ■
      ■ ■ ■
    ■ ■ ■ ■ ■
  ■ ■ ■ ● ■ ■ ■
    ■ ■ ■ ■ ■
      ■ ■ ■
        ■
```

---

## Color Generation

### Base Color: Hue from Toxin Resistance

```cpp
Color color_from_traits(float toxin_resistance, float metabolism_efficiency) {
    // Hue: Red (0°) → Yellow (60°) → Green (120°)
    // Maps toxin resistance [0.0, 1.0] to [0°, 120°]
    float hue = toxin_resistance * 120.0f;

    // Saturation: Higher metabolism = more vibrant
    // Maps efficiency [0.6, 1.0] to [40%, 100%]
    float saturation = (metabolism_efficiency - 0.6f) / 0.4f * 0.6f + 0.4f;

    // Value: Always bright
    float value = 0.9f;

    return hsv_to_rgb(hue, saturation, value);
}

Color hsv_to_rgb(float h, float s, float v) {
    float c = v * s;
    float x = c * (1.0f - std::abs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;

    float r, g, b;
    if (h < 60.0f) {
        r = c; g = x; b = 0.0f;
    } else if (h < 120.0f) {
        r = x; g = c; b = 0.0f;
    } else {
        r = 0.0f; g = c; b = x;
    }

    return Color{r + m, g + m, b + m};
}
```

**Color Evolution Example:**
- Low toxin resistance (0.2): **Red-orange** (hue 24°)
- Medium resistance (0.5): **Yellow-green** (hue 60°)
- High resistance (0.9): **Bright green** (hue 108°)

**Observable Evolution:** In toxic biomes, population shifts from red → green over generations.

### Accent Color: Deterministic Variation

```cpp
Color accent_color_from_genome(const Genome& genome) {
    // Use genome hash for deterministic but varied color
    uint32_t hash = genome_hash(genome);
    float hue_offset = static_cast<float>(hash % 360);

    float base_hue = genome.toxin_resistance() * 120.0f;
    float accent_hue = std::fmod(base_hue + hue_offset, 360.0f);

    return hsv_to_rgb(accent_hue, 0.8f, 0.7f);
}

uint32_t genome_hash(const Genome& genome) {
    // Simple hash from first few genes
    uint32_t hash = 0;
    for (size_t i = 0; i < 5 && i < genome.genes.size(); ++i) {
        hash += static_cast<uint32_t>(genome.genes[i] * 1000.0f) << (i * 3);
    }
    return hash;
}
```

**Result:** Each lineage has a unique accent color, making family trees visually trackable.

---

## Pattern Generation

### Pattern Selection

```cpp
Pattern pattern_from_behavior(float max_speed, float learning_rate) {
    // Fast organisms (>1.6): Striped (motion blur aesthetic)
    if (max_speed > 1.6f) {
        return Pattern::Striped;
    }

    // Cautious learners (>0.007): Spotted (camouflage aesthetic)
    if (learning_rate > 0.007f) {
        return Pattern::Spotted;
    }

    // Default: Solid
    return Pattern::Solid;
}
```

### Pattern Rendering

```cpp
Color tile_color_for_pattern(
    const SpriteDescriptor& descriptor,
    size_t tile_index,
    Vec2i offset
) {
    switch (descriptor.pattern) {
        case Pattern::Solid:
            return descriptor.base_color;

        case Pattern::Striped:
            // Checkerboard based on position
            if ((offset.x + offset.y) % 2 == 0) {
                return descriptor.base_color;
            } else {
                return descriptor.accent_color;
            }

        case Pattern::Spotted:
            // Every 3rd tile is accent color
            if (tile_index % 3 == 0) {
                return descriptor.accent_color;
            } else {
                return descriptor.base_color;
            }
    }
    return descriptor.base_color;  // Unreachable
}
```

**Visual Examples:**

**Striped Pattern (Fast Runner):**
```
  A B A      (A = base_color, B = accent_color)
B A B A B    Alternating checkerboard
  A B A
```

**Spotted Pattern (Cautious Learner):**
```
  B          (Every 3rd tile is accent)
A A B
  A A A B
```

---

## Eyes & Limbs

### Eye Count from Brain Depth

```cpp
uint8_t eye_count(size_t brain_depth) {
    // Depth 0 → 1 eye
    // Depth 1 → 2 eyes
    // Depth 2 → 3 eyes
    return static_cast<uint8_t>(std::min(brain_depth + 1, size_t(3)));
}
```

**Visual Representation:**
- Eyes rendered as small bright circles on center tile
- Positioned symmetrically around origin
- Larger brains → more eyes → "looks smarter"

### Limb Generation from Brain Width

```cpp
std::vector<Vec2i> generate_limbs(size_t brain_width) {
    size_t limb_count = std::min(brain_width / 2, size_t(4));
    std::vector<Vec2i> limbs;

    // Place limbs at cardinal directions
    Vec2i directions[] = {
        {1, 0},   // Right
        {-1, 0},  // Left
        {0, 1},   // Up
        {0, -1},  // Down
    };

    for (size_t i = 0; i < limb_count; ++i) {
        limbs.push_back(directions[i]);
    }

    return limbs;
}
```

**Limb Count Examples:**
- Width 2 → 1 limb (single antenna)
- Width 4 → 2 limbs (left/right pair)
- Width 8 → 4 limbs (full cardinal set)

**Visual Representation:**
- Small protrusions from edge tiles
- Color: Darker shade of base_color
- Larger brains → more limbs → "looks complex"

---

## Rendering Integration

### Spawning Multi-Tile Organism

```cpp
void spawn_organism_sprite(
    Renderer& renderer,
    EntityID organism_entity,
    const SpriteDescriptor& descriptor,
    Vec2 position
) {
    constexpr float TILE_SIZE = 16.0f;

    // Spawn body tiles
    for (size_t i = 0; i < descriptor.footprint.size(); ++i) {
        Vec2i offset = descriptor.footprint[i];
        Color color = tile_color_for_pattern(descriptor, i, offset);

        renderer.add_sprite(organism_entity, {
            .color = color,
            .size = {TILE_SIZE, TILE_SIZE},
            .position = {
                position.x + offset.x * TILE_SIZE,
                position.y + offset.y * TILE_SIZE,
            },
            .z_order = 0.0f,
        });
    }

    // Spawn eyes
    spawn_eyes(renderer, organism_entity, descriptor.eyes, TILE_SIZE);

    // Spawn limbs
    spawn_limbs(renderer, organism_entity, descriptor.limb_offsets, descriptor.base_color, TILE_SIZE);
}

void spawn_eyes(Renderer& renderer, EntityID entity, uint8_t eye_count, float tile_size) {
    float eye_size = tile_size * 0.3f;

    for (uint8_t i = 0; i < eye_count; ++i) {
        float angle = (static_cast<float>(i) / eye_count) * 2.0f * M_PI;
        float radius = tile_size * 0.3f;
        float x = std::cos(angle) * radius;
        float y = std::sin(angle) * radius;

        renderer.add_sprite(entity, {
            .color = {1.0f, 1.0f, 1.0f},  // White
            .size = {eye_size, eye_size},
            .position = {x, y},
            .z_order = 1.0f,
        });
    }
}

void spawn_limbs(
    Renderer& renderer,
    EntityID entity,
    const std::vector<Vec2i>& limb_offsets,
    Color base_color,
    float tile_size
) {
    Vec2 limb_size = {tile_size * 0.4f, tile_size * 0.15f};

    for (const auto& offset : limb_offsets) {
        float x = offset.x * tile_size;
        float y = offset.y * tile_size;

        // Darken base color for limbs
        Color limb_color = {
            base_color.r * 0.7f,
            base_color.g * 0.7f,
            base_color.b * 0.7f,
        };

        renderer.add_sprite(entity, {
            .color = limb_color,
            .size = limb_size,
            .position = {x, y},
            .z_order = 0.5f,
        });
    }
}
```

### Dynamic Update System

```cpp
void update_organism_sprites(Registry& registry, Renderer& renderer) {
    for (auto [entity, organism] : registry.view_changed<Organism>()) {
        // Regenerate descriptor if genome mutated
        auto descriptor = sprite_descriptor_from_genome(organism.genome);

        // Update sprite colors
        auto& sprites = renderer.get_sprites(entity);
        for (size_t i = 0; i < descriptor.footprint.size() && i < sprites.size(); ++i) {
            sprites[i].color = tile_color_for_pattern(
                descriptor, i, descriptor.footprint[i]
            );
        }
    }
}
```

---

## Observable Evolution Examples

### Example 1: Toxin Resistance Evolution

**Initial Population (Generation 0):**
- 80% red organisms (low toxin resistance)
- 15% orange organisms (medium resistance)
- 5% yellow organisms (high resistance)

**After 50 Generations in Toxic Biome:**
- 10% red (dead ends)
- 30% orange (intermediate)
- 60% **green** (high resistance evolved)

**Visual Impact:** The entire screen shifts from predominantly red → predominantly green.

### Example 2: Intelligence Arms Race

**Initial Population:**
- 90% one-eyed organisms (brain depth 0)
- 10% two-eyed organisms (brain depth 1)

**After Resource Scarcity Event:**
- 40% one-eyed (simple foragers)
- 50% two-eyed (gradient followers)
- 10% **three-eyed** (complex planners)

**Visual Impact:** Three-eyed organisms become noticeably common in resource-poor regions.

### Example 3: Size Optimization

**Initial Population in Rainforest:**
- Random sizes (0.5 - 5.5)
- Average footprint: 13 tiles

**After 100 Generations:**
- Convergence to medium-large (3.0 - 4.5)
- Average footprint: 21 tiles

**Visual Impact:** Organisms appear uniformly larger, occupying more space.

### Example 4: Speed-Pattern Correlation

**Initial Population:**
- Random speeds (0.5 - 2.0)
- Mostly solid patterns

**After Competition for Sparse Food:**
- Fast organisms (>1.6 speed) evolve
- **Striped pattern** becomes dominant

**Visual Impact:** Population shifts from solid colors → striped patterns, indicating speed-optimized genomes.

---

## Advanced Features

### Genetic Lineage Tracking

```cpp
struct LineageMarker {
    uint64_t ancestor_id;
    uint32_t generation;
};

Color accent_color_from_lineage(const LineageMarker& lineage) {
    // Lineages from same ancestor share similar accent colors
    float hue = static_cast<float>(lineage.ancestor_id % 360);
    return hsv_to_rgb(hue, 0.7f, 0.8f);
}
```

**Visual Impact:** Family trees are color-coded, making speciation events visible.

### Sexual Dimorphism (Future)

```cpp
enum class Sex { Male, Female };

Pattern pattern_from_sex_and_genome(Sex sex, const Genome& genome) {
    if (sex == Sex::Male) {
        return Pattern::Striped;  // Males always striped
    }
    return pattern_from_behavior(genome.max_speed(), genome.learning_rate());
}
```

**Visual Impact:** Males and females visually distinguishable, enabling mate-choice observation.

### Energy-Based Brightness

```cpp
Color adjust_brightness_by_energy(Color color, float energy, float max_energy) {
    float energy_fraction = std::clamp(energy / max_energy, 0.3f, 1.0f);
    return Color{
        color.r * energy_fraction,
        color.g * energy_fraction,
        color.b * energy_fraction,
    };
}
```

**Visual Impact:** Starving organisms appear dim; well-fed organisms are vibrant.

---

## Performance Considerations

### Sprite Batching

```cpp
// Group organisms by descriptor for efficient batching
std::unordered_map<SpriteDescriptor, std::vector<Vec2>>
batch_organisms_by_appearance(Registry& registry) {
    std::unordered_map<SpriteDescriptor, std::vector<Vec2>> batches;

    for (auto [entity, organism, position] : registry.view<Organism, Position>()) {
        auto descriptor = sprite_descriptor_from_genome(organism.genome);
        batches[descriptor].push_back(position);
    }

    return batches;
}
```

**Performance Impact:**
- Reduces draw calls from 1000 organisms × 13 tiles = 13,000 → ~50 batches
- **260× reduction in draw calls**

### Descriptor Caching

```cpp
struct CachedDescriptor {
    SpriteDescriptor descriptor;
    uint64_t genome_hash;
};

bool update_sprite_if_changed(const Organism& organism, CachedDescriptor& cached) {
    uint64_t new_hash = hash_genome(organism.genome);
    if (new_hash != cached.genome_hash) {
        cached.descriptor = sprite_descriptor_from_genome(organism.genome);
        cached.genome_hash = new_hash;
        return true;
    }
    return false;
}
```

**Performance Impact:** Only regenerate sprites when genome actually changes (during mutation/birth).

---

## Integration Points

### With Neural Evolution

Brain complexity directly affects visual complexity:

```cpp
uint8_t eyes = organism.brain_depth() + 1;
size_t limbs = organism.brain_width() / 2;
float color_saturation = organism.metabolism_efficiency;
```

Smarter organisms **look** smarter.

See: [Neural Evolution](NEURAL_EVOLUTION.md)

### With Planetary Model

Environmental stress affects color:

```cpp
Color color_from_environment(const Organism& organism, const Climate& climate) {
    Color base_color = color_from_traits(
        organism.toxin_resistance(),
        organism.metabolism_efficiency
    );

    // Dim color in harsh climates
    float stress_factor = climate.habitability_factor;
    return adjust_brightness(base_color, stress_factor);
}
```

See: [Planetary Model](PLANETARY_MODEL.md)

### With World Generation

Biome-specific camouflage patterns (future feature):

```cpp
Pattern optimal_pattern_for_biome(Biome biome) {
    switch (biome) {
        case Biome::Rainforest: return Pattern::Spotted;
        case Biome::Grassland:  return Pattern::Striped;
        default:                return Pattern::Solid;
    }
}
```

See: [World Generation](WORLD_GENERATION.md)

---

## Testing & Validation

### Determinism Test

```cpp
void test_sprite_determinism() {
    Genome genome = Genome::random(152);

    auto descriptor1 = sprite_descriptor_from_genome(genome);
    auto descriptor2 = sprite_descriptor_from_genome(genome);

    assert(descriptor1.footprint == descriptor2.footprint);
    assert(descriptor1.base_color == descriptor2.base_color);
    assert(descriptor1.pattern == descriptor2.pattern);
}
```

### Visual Trait Coverage Test

```cpp
void test_all_visual_traits_used() {
    Genome genome = Genome::random(152);
    auto descriptor = sprite_descriptor_from_genome(genome);

    // Ensure all genome traits affect appearance
    assert(descriptor.footprint.size() >= 5);  // Body size used
    assert(descriptor.eyes >= 1 && descriptor.eyes <= 3);  // Brain depth used
    assert(!descriptor.limb_offsets.empty());  // Brain width used
    // Color derived from metabolism + toxin resistance
}
```

### Observable Evolution Test

```cpp
void test_toxin_resistance_color_shift() {
    Genome low_resistance_genome;
    low_resistance_genome.genes.assign(152, 0.0f);

    Genome high_resistance_genome;
    high_resistance_genome.genes.assign(152, 1.0f);

    auto low_color = sprite_descriptor_from_genome(low_resistance_genome).base_color;
    auto high_color = sprite_descriptor_from_genome(high_resistance_genome).base_color;

    // Red (low) vs Green (high)
    assert(low_color.r > 0.8f);   // Red channel high
    assert(high_color.g > 0.8f);  // Green channel high
}
```

---

## Implementation Checklist

- [ ] SpriteDescriptor struct
- [ ] genome_to_footprint (size → tile count)
- [ ] color_from_traits (toxin/metabolism → hue/saturation)
- [ ] accent_color_from_genome (deterministic variation)
- [ ] pattern_from_behavior (speed/learning → pattern)
- [ ] eye_count (brain depth → eyes)
- [ ] generate_limbs (brain width → limb count)
- [ ] spawn_organism_sprite (rendering integration)
- [ ] spawn_eyes (white circles)
- [ ] spawn_limbs (dark protrusions)
- [ ] tile_color_for_pattern (pattern rendering)
- [ ] update_organism_sprites (dynamic updates)
- [ ] Determinism tests
- [ ] Visual trait coverage tests
- [ ] Observable evolution tests

---

## References

- HSV color space: Hue, saturation, value
- Procedural generation: Deterministic randomness
- Visual feedback in simulations: Making evolution observable

**Related Documentation:**
- [Neural Evolution](NEURAL_EVOLUTION.md)
- [Planetary Model](PLANETARY_MODEL.md)
- [World Generation](WORLD_GENERATION.md)
- [Nutrient Regeneration](NUTRIENT_REGENERATION.md)
