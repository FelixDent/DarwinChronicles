# Evolving Neural Controllers with Metabolic Costs

**Part of:** [Darwin Chronicles](../../README.md)
**Implementation:** [`src/organisms/`](../../src/organisms/)

## Overview

The Neural Evolution system implements genome-encoded neural networks where every trait—brain architecture, body size, metabolic efficiency—has realistic energy costs. Organisms inherit a 152-float genome from parents, which determines their neural network structure, physical morphology, and physiological parameters. Energy tradeoffs create evolutionary pressure: larger brains think better but cost more; faster movement reaches food quicker but burns more energy; bigger bodies store more reserves but need more fuel. Within-lifetime Hebbian learning allows organisms to adapt to their environment, while genetic mutations enable long-term evolutionary change.

## Genome Structure

### Complete Genome Layout

```cpp
class Genome {
public:
    std::vector<float> genes;  // Length: 152 floats

    // Gene regions:
    // [0..3]     Architecture genes (depth, width_h1, width_h2)
    // [3..5]     Morphology genes (body_size, max_speed)
    // [5..8]     Physiology genes (metabolism_eff, mutation_rate, learning_rate)
    // [8..152]   Neural network weights (144 floats)
};
```

### Architecture Genes [0..3]

```cpp
size_t Genome::brain_depth() const {
    // genes[0] ∈ [0.0, 1.0] → 0, 1, or 2 hidden layers
    if (genes[0] < 0.33f) {
        return 0;  // Direct input→output (8 synapses)
    } else if (genes[0] < 0.67f) {
        return 1;  // One hidden layer
    } else {
        return 2;  // Two hidden layers (deepest)
    }
}

size_t Genome::brain_width() const {
    // genes[1], genes[2] → neurons per hidden layer
    // Each gene ∈ [0.0, 1.0] → 2, 4, or 8 neurons
    auto decode_width = [](float gene) -> size_t {
        if (gene < 0.33f) return 2;
        else if (gene < 0.67f) return 4;
        else return 8;
    };

    // Both genes used for consistency, returns typical width
    return decode_width(genes[1]);
}
```

**Synapse Count Examples:**
- Depth 0: 8 inputs × 2 outputs = **16 synapses**
- Depth 1, width 4: (8×4) + (4×2) = 32 + 8 = **40 synapses**
- Depth 2, width 8: (8×8) + (8×8) + (8×2) = 64 + 64 + 16 = **144 synapses**

### Morphology Genes [3..5]

```cpp
float Genome::body_size() const {
    // genes[3] ∈ [0.0, 1.0] → [0.5, 5.5]
    // Logarithmic scaling for wide dynamic range
    float normalized = genes[3];
    return 0.5f + normalized * 5.0f;
}

float Genome::max_speed() const {
    // genes[4] ∈ [0.0, 1.0] → [0.5, 2.0]
    float normalized = genes[4];
    return 0.5f + normalized * 1.5f;
}
```

**Physical Traits:**
- Small organism: body_size = 0.5, max_speed = 2.0 (fast and nimble)
- Large organism: body_size = 5.5, max_speed = 0.5 (slow but sturdy)

### Physiology Genes [5..8]

```cpp
float Genome::metabolism_efficiency() const {
    // genes[5] ∈ [0.0, 1.0] → [0.6, 1.0]
    // Fraction of consumed nutrients converted to energy
    return 0.6f + genes[5] * 0.4f;
}

float Genome::mutation_rate() const {
    // genes[6] ∈ [0.0, 1.0] → [0.01, 0.1]
    // Standard deviation for Gaussian mutation
    return 0.01f + genes[6] * 0.09f;
}

float Genome::learning_rate() const {
    // genes[7] ∈ [0.0, 1.0] → [0.0, 0.01]
    // Hebbian plasticity strength
    return genes[7] * 0.01f;
}
```

### Neural Network Weights [8..152]

```cpp
std::vector<float> Genome::extract_weights(size_t num_weights) const {
    // Take first num_weights from weight region
    // Unused weights (for smaller brains) are ignored
    return std::vector<float>(genes.begin() + 8, genes.begin() + 8 + num_weights);
}
```

**Weight Initialization:**
- Random: Uniform [-1.0, 1.0]
- Inherited: Copied from parent with Gaussian noise

---

## Neural Network Architecture

### Variable-Depth Networks

```cpp
struct Layer {
    std::vector<std::vector<float>> weights;  // [output_neurons][input_neurons]
    std::vector<float> biases;
    std::vector<float> activations;           // Cached for learning
    std::vector<float> input_activations;     // Cached for learning
};

class Brain {
public:
    std::vector<Layer> layers;
    float learning_rate;

    static Brain from_genome(const Genome& genome) {
        size_t depth = genome.brain_depth();
        size_t width = genome.brain_width();
        float lr = genome.learning_rate();

        constexpr size_t INPUT_SIZE = 8;
        constexpr size_t OUTPUT_SIZE = 2;

        std::vector<Layer> layers;
        size_t weight_idx = 0;
        auto weights_flat = genome.extract_weights(144);

        switch (depth) {
            case 0:
                // Direct input → output
                layers.push_back(Layer::create(INPUT_SIZE, OUTPUT_SIZE, weights_flat, weight_idx));
                break;
            case 1:
                // Input → hidden → output
                layers.push_back(Layer::create(INPUT_SIZE, width, weights_flat, weight_idx));
                layers.push_back(Layer::create(width, OUTPUT_SIZE, weights_flat, weight_idx));
                break;
            case 2:
                // Input → hidden1 → hidden2 → output
                layers.push_back(Layer::create(INPUT_SIZE, width, weights_flat, weight_idx));
                layers.push_back(Layer::create(width, width, weights_flat, weight_idx));
                layers.push_back(Layer::create(width, OUTPUT_SIZE, weights_flat, weight_idx));
                break;
            default:
                throw std::runtime_error("Invalid brain depth");
        }

        return Brain{std::move(layers), lr};
    }
};
```

### Forward Pass

```cpp
float tanh_activation(float x) {
    return std::tanh(x);
}

std::vector<float> Layer::forward(const std::vector<float>& inputs) {
    std::vector<float> outputs(weights.size(), 0.0f);

    for (size_t i = 0; i < weights.size(); ++i) {
        float sum = biases[i];
        for (size_t j = 0; j < inputs.size(); ++j) {
            sum += weights[i][j] * inputs[j];
        }
        outputs[i] = tanh_activation(sum);  // tanh ∈ [-1, 1]
    }

    input_activations = inputs;
    activations = outputs;
    return outputs;
}

std::vector<float> Brain::forward(const std::vector<float>& sensors) {
    std::vector<float> current_input = sensors;

    for (auto& layer : layers) {
        current_input = layer.forward(current_input);
    }

    return current_input;  // Final output layer activations
}
```

### Sensor Inputs (8 total)

```cpp
std::vector<float> get_sensor_inputs(
    const Organism& organism,
    const WorldGrid& world,
    std::pair<uint32_t, uint32_t> position
) {
    auto [x, y] = position;

    // Gradient sensing (3x3 neighborhood)
    auto nutrient_grad = world.nutrient_gradient(x, y);
    auto toxin_grad = world.toxin_gradient(x, y);

    return {
        nutrient_grad.x,                       // [0] Nutrient gradient X
        nutrient_grad.y,                       // [1] Nutrient gradient Y
        toxin_grad.x,                          // [2] Toxin gradient X
        toxin_grad.y,                          // [3] Toxin gradient Y
        organism.energy / 100.0f,              // [4] Energy level (normalized)
        organism.hunger(),                     // [5] Hunger (time since last meal)
        world.tile(x, y).temperature_K / 300.0f,  // [6] Temperature stress
        world.tile(x, y).toxins,               // [7] Local toxicity
    };
}
```

**Gradient Calculation:**
```cpp
Vec2 WorldGrid::nutrient_gradient(uint32_t x, uint32_t y) const {
    float center = tile(x, y).nutrients;
    float left   = tile(x > 0 ? x - 1 : 0, y).nutrients;
    float right  = tile(std::min(x + 1, width - 1), y).nutrients;
    float down   = tile(x, y > 0 ? y - 1 : 0).nutrients;
    float up     = tile(x, std::min(y + 1, height - 1)).nutrients;

    return Vec2{
        (right - left) / 2.0f,   // Central difference X
        (up - down) / 2.0f,      // Central difference Y
    };
}

Vec2 WorldGrid::toxin_gradient(uint32_t x, uint32_t y) const {
    // Same as nutrient gradient but for toxins
    // ...
}
```

### Motor Outputs (2 total)

```cpp
std::pair<float, float> decode_movement_outputs(const std::vector<float>& outputs, float max_speed) {
    // outputs[0] and outputs[1] are in [-1, 1] from tanh
    float dx = outputs[0] * max_speed;
    float dy = outputs[1] * max_speed;
    return {dx, dy};
}
```

---

## Metabolic Costs

### Brain Energy Cost

```cpp
float Brain::compute_energy_cost() const {
    constexpr float ENERGY_PER_SYNAPSE = 0.001f;
    size_t synapse_count = count_synapses();
    return static_cast<float>(synapse_count) * ENERGY_PER_SYNAPSE;
}

size_t Brain::count_synapses() const {
    size_t total = 0;
    for (const auto& layer : layers) {
        total += layer.weights.size() * layer.weights[0].size();
    }
    return total;
}
```

**Energy Cost Table:**

| Architecture | Synapses | Cost per Step |
|--------------|----------|---------------|
| Depth 0 (8→2) | 16 | 0.016 |
| Depth 1, Width 2 (8→2→2) | 20 | 0.020 |
| Depth 1, Width 4 (8→4→2) | 40 | 0.040 |
| Depth 1, Width 8 (8→8→2) | 80 | 0.080 |
| Depth 2, Width 4 (8→4→4→2) | 48 | 0.048 |
| Depth 2, Width 8 (8→8→8→2) | 144 | 0.144 |

**Key Insight:** Largest brain costs **9× more** than smallest brain.

### Movement Energy Cost

```cpp
float movement_cost(float body_size, float dx, float dy) {
    constexpr float K_MOVE = 0.002f;
    float speed = std::sqrt(dx * dx + dy * dy);
    return K_MOVE * body_size * speed * speed;  // Kinetic energy: KE = ½mv²
}
```

**Example Costs:**
- Small (0.5) moving slow (0.5): 0.002 × 0.5 × 0.25 = **0.00025**
- Medium (3.0) moving medium (1.0): 0.002 × 3.0 × 1.0 = **0.006**
- Large (5.5) moving fast (2.0): 0.002 × 5.5 × 4.0 = **0.044**

### Basal Metabolism

```cpp
float basal_metabolic_cost(float body_size) {
    constexpr float K_BASAL = 0.01f;
    return K_BASAL * std::pow(body_size, 0.75f);  // Kleiber's law
}
```

**Allometric Scaling:**
- Small (0.5): 0.01 × 0.59 = **0.0059**
- Medium (3.0): 0.01 × 2.28 = **0.0228**
- Large (5.5): 0.01 × 3.80 = **0.0380**

**Kleiber's Law:** Metabolic rate ∝ mass^0.75 (sub-linear scaling favors larger organisms)

### Reproduction Cost

```cpp
float replication_threshold(float body_size, size_t brain_synapses) {
    constexpr float K_REPRO_BODY = 20.0f;
    constexpr float K_REPRO_BRAIN = 0.1f;

    return (K_REPRO_BODY * body_size) + (K_REPRO_BRAIN * static_cast<float>(brain_synapses));
}
```

**Reproduction Thresholds:**
- Tiny Sprinter (size 0.5, 16 synapses): 20×0.5 + 0.1×16 = **11.6 energy**
- Smart Forager (size 2.0, 80 synapses): 20×2.0 + 0.1×80 = **48.0 energy**
- Tank (size 5.5, 40 synapses): 20×5.5 + 0.1×40 = **114.0 energy**

---

## Energy Balance

### Complete Organism Step

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

    // 5. LEARN (Hebbian update)
    float net_energy_delta = /* compute from above */;
    organism.brain.hebbian_update(net_energy_delta);

    // 6. REPRODUCE or DIE
    if (organism.energy >= replication_threshold(organism.body_size, organism.brain.count_synapses())) {
        spawn_offspring(organism, world, new_pos);
    }
    if (organism.energy <= 0.0f) {
        despawn(organism);
    }
}
```

### Energy Flow Diagram

```
Energy IN:
  ├─ Consumed nutrients × metabolism_efficiency
  └─ (Stored in organism.energy)

Energy OUT:
  ├─ Brain cost (per synapse)
  ├─ Movement cost (kinetic energy)
  ├─ Basal metabolism (Kleiber's law)
  ├─ Toxin damage
  └─ Reproduction cost (when threshold reached)

Net Energy = IN - OUT
```

---

## Hebbian Learning

### Within-Lifetime Plasticity

```cpp
void Brain::hebbian_update(float energy_delta) {
    // Strengthen connections that led to energy gain
    // Weaken connections that led to energy loss

    for (auto& layer : layers) {
        for (size_t i = 0; i < layer.weights.size(); ++i) {
            float output_activation = layer.activations[i];

            for (size_t j = 0; j < layer.weights[i].size(); ++j) {
                // Hebbian rule: Δw = η × input × output × reward
                float delta_weight = learning_rate
                    * layer.input_activations[j]  // Pre-synaptic
                    * output_activation            // Post-synaptic
                    * energy_delta;                // Reward signal

                layer.weights[i][j] += delta_weight;

                // Clamp weights to prevent explosion
                layer.weights[i][j] = std::clamp(layer.weights[i][j], -2.0f, 2.0f);
            }
        }
    }
}
```

**Learning Examples:**
- Energy gain (+5.0): Strengthen pathway that led to food
- Energy loss (-2.0): Weaken pathway that led to toxins
- No learning (learning_rate = 0.0): Pure genetic behavior

### Learning vs Genetic Evolution

| Mechanism | Timescale | Scope | Heritability |
|-----------|-----------|-------|--------------|
| **Hebbian Learning** | Within lifetime | Individual adaptation | No (weights not inherited) |
| **Genetic Mutation** | Across generations | Population evolution | Yes (genome inherited) |

**Synergy:** Learning allows organisms to adapt to local conditions, while evolution optimizes the learning rate and initial weights.

---

## Reproduction & Mutation

### Asexual Reproduction

```cpp
void spawn_offspring(
    Organism& parent,
    WorldGrid& world,
    std::pair<uint32_t, uint32_t> position
) {
    // Deduct reproduction cost
    float cost = replication_threshold(parent.body_size, parent.brain.count_synapses());
    parent.energy -= cost;

    // Create offspring genome (mutated copy)
    Genome offspring_genome = parent.genome;
    offspring_genome.mutate(parent.mutation_rate());

    // Spawn new organism nearby
    auto spawn_pos = find_empty_adjacent(world, position);
    if (spawn_pos.has_value()) {
        Organism offspring = Organism::from_genome(offspring_genome, START_ENERGY);
        world.spawn_organism(std::move(offspring), spawn_pos.value());
    }
}
```

### Gaussian Mutation

```cpp
void Genome::mutate(float mutation_rate) {
    std::mt19937 rng(std::random_device{}());
    std::normal_distribution<float> normal(0.0f, mutation_rate);

    for (auto& gene : genes) {
        float perturbation = normal(rng);
        gene = std::clamp(gene + perturbation, 0.0f, 1.0f);
    }
}
```

**Mutation Impact:**
- Low rate (0.01): Small tweaks, stable lineages
- Medium rate (0.05): Moderate variation, adaptive
- High rate (0.1): Large changes, high evolvability

### Evolvability Tradeoff

Self-encoded mutation rate creates meta-evolution:
- **Low mutation:** Preserves successful traits, but slow to adapt
- **High mutation:** Explores widely, but disrupts good solutions

Optimal mutation rate evolves based on environmental stability.

---

## Emergent Archetypes

### 1. Tiny Sprinter (r-selection)

```cpp
// Genome traits:
//   body_size: 0.5           // Minimal
//   max_speed: 2.0           // Maximum
//   brain_depth: 0           // No hidden layers
//   brain_width: 2           // Minimal
//   metabolism_efficiency: 0.9  // High conversion
//   mutation_rate: 0.08      // High evolvability

// Energy budget:
// - Brain: 0.016/step (cheap)
// - Movement: 0.044/step (expensive at max speed)
// - Basal: 0.0059/step (cheap)
// - Reproduction: 11.6 threshold (fast)
// → High reproduction rate, short lifespan
```

**Strategy:** Move fast, eat quickly, reproduce often, die young.

### 2. Smart Forager (K-selection)

```cpp
// Genome traits:
//   body_size: 2.0           // Medium
//   max_speed: 1.2           // Moderate
//   brain_depth: 1           // One hidden layer
//   brain_width: 8           // Large
//   metabolism_efficiency: 0.85
//   learning_rate: 0.008     // High plasticity
//   mutation_rate: 0.03      // Moderate

// Energy budget:
// - Brain: 0.080/step (expensive)
// - Movement: 0.0058/step (moderate)
// - Basal: 0.0228/step (moderate)
// - Reproduction: 48.0 threshold (slow)
// → Learn gradients, efficient foraging, stable population
```

**Strategy:** Learn environment, optimize paths, long-lived specialists.

### 3. Tank

```cpp
// Genome traits:
//   body_size: 5.5           // Maximum
//   max_speed: 0.6           // Slow
//   brain_depth: 1           // Moderate brain
//   brain_width: 4           // Moderate
//   metabolism_efficiency: 0.95  // Highly efficient
//   mutation_rate: 0.02      // Conservative

// Energy budget:
// - Brain: 0.040/step (moderate)
// - Movement: 0.004/step (slow = cheap)
// - Basal: 0.0380/step (expensive)
// - Reproduction: 114.0 threshold (very slow)
// → Slow, steady, survives harsh conditions
```

**Strategy:** Outlast competition, dominate stable niches, resist toxins.

### 4. Adaptive Generalist

```cpp
// Genome traits:
//   body_size: 2.5           // Balanced
//   max_speed: 1.5           // Balanced
//   brain_depth: 1           // Moderate brain
//   brain_width: 4           // Moderate
//   metabolism_efficiency: 0.8
//   learning_rate: 0.005     // Moderate
//   mutation_rate: 0.05      // High evolvability

// Energy budget:
// - Brain: 0.040/step
// - Movement: 0.011/step
// - Basal: 0.025/step
// - Reproduction: 54.0 threshold
// → Balanced, adaptable, no strong weaknesses
```

**Strategy:** Jack-of-all-trades, survives diverse conditions.

---

## Integration Points

### With World Generation

Temperature stress affects survival:

```cpp
float temperature_stress_cost(const Organism& organism, const Tile& tile) {
    constexpr float optimal_temp = 295.0f;  // Kelvin
    float deviation = std::abs(tile.temperature_K - optimal_temp);
    float stress = std::pow(deviation / 20.0f, 2);  // Quadratic penalty
    return stress * 0.01f * organism.body_size;
}
```

See: [World Generation](WORLD_GENERATION.md)

### With Nutrient Regeneration

Metabolism directly couples to nutrient availability:

```cpp
float consumed_nutrients = world.consume_nutrients(position, organism.max_food_intake);
organism.energy += consumed_nutrients * organism.metabolism_efficiency;

// Organism.max_food_intake is genome-determined
// World regeneration rate determines sustainable population
```

See: [Nutrient Regeneration](NUTRIENT_REGENERATION.md)

### With Sprite Generation

Brain architecture affects visual complexity:

```cpp
size_t sprite_complexity(const Genome& genome) {
    size_t brain_synapses = compute_synapse_count(genome.brain_depth(), genome.brain_width());
    return 5 + (brain_synapses / 20);  // More synapses = more sprite tiles
}
```

See: [Sprite Generation](SPRITE_GENERATION.md)

---

## Balancing Guidelines

### Brain Cost Tuning

```cpp
// If organisms evolve away from intelligence:
constexpr float ENERGY_PER_SYNAPSE = 0.0005f;  // Reduce cost

// If all organisms have max brains:
constexpr float ENERGY_PER_SYNAPSE = 0.002f;   // Increase cost
```

### Reproduction Threshold Tuning

```cpp
// Population growing too fast:
constexpr float K_REPRO_BODY = 30.0f;  // Increase threshold

// Population collapsing:
constexpr float K_REPRO_BODY = 15.0f;  // Decrease threshold
```

### Mutation Rate Constraints

```cpp
// Prevent runaway mutation:
float Genome::mutation_rate() const {
    return std::min(0.01f + genes[6] * 0.05f, 0.06f);  // Cap at 6%
}
```

---

## Implementation Checklist

- [ ] Genome class with 152 floats
- [ ] Trait extraction methods (body_size, brain_depth, etc.)
- [ ] Variable-depth neural network builder
- [ ] Forward pass with tanh activation
- [ ] Sensor input computation (gradients, internal state)
- [ ] Motor output decoding
- [ ] Brain energy cost calculation
- [ ] Movement energy cost (kinetic)
- [ ] Basal metabolism (Kleiber's law)
- [ ] Reproduction threshold formula
- [ ] Hebbian learning update
- [ ] Gaussian mutation operator
- [ ] Asexual reproduction system
- [ ] Death condition (energy ≤ 0)
- [ ] Unit tests for energy balance
- [ ] Archetype emergence tests

---

## References

- Hebbian learning: "Neurons that fire together, wire together"
- Kleiber's law: Metabolic scaling in biology
- Genetic algorithms: Mutation and selection
- Neural network basics: Multi-layer perceptrons

**Related Documentation:**
- [Planetary Model](PLANETARY_MODEL.md)
- [World Generation](WORLD_GENERATION.md)
- [Nutrient Regeneration](NUTRIENT_REGENERATION.md)
- [Sprite Generation](SPRITE_GENERATION.md)
