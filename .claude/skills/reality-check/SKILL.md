# Reality Check: Verifying That Generated Systems Look Real

## When to Use

Use this skill EVERY TIME you modify or create ANY system that produces emergent or procedural output — weather simulation, terrain generation, creature behavior, sprite generation, vegetation growth, neural networks, evolution, or anything else where the output should resemble something real. This is NOT optional.

**The core problem this solves**: Aggregate stats (mean, stddev, min/max) can look healthy while the system produces completely unrealistic output. A procedural tree with correct average branch count and height can still look nothing like a tree. A weather system with "good" temperature variability can have wind blowing the same direction everywhere. A terrain generator with correct elevation statistics can produce shapes no geologist would recognize.

**You cannot verify realism by checking that code compiles, that values don't crash, or that aggregate statistics fall in expected ranges.** You must verify the specific observable properties that make a human say "that looks/feels real."

## The Process

### Step 1: Define Reality Characteristics

Before writing ANY code or running ANY test, explicitly enumerate what makes this system's output look/feel real. These are **observable properties that a human would notice**, NOT implementation details or code metrics.

For each characteristic, specify:
- **What it is** (qualitative description a human would use)
- **What it looks like when real** (specific, measurable criteria)
- **What it looks like when fake** (the failure mode you're watching for)

Think like a domain expert examining the output. What would immediately strike them as wrong?

**Examples across different systems:**

Weather:
| Characteristic | Real | Fake |
|---|---|---|
| Wind changes direction | At any point, wind direction varies over days. Neighbors can differ. | Same direction across a hemisphere all year. |
| Rain is patchy and intermittent | Rain patches 3-10 cells wide, any cell alternates wet/dry. | Uniform rain everywhere, or rain locked to coastlines. |

Terrain:
| Characteristic | Real | Fake |
|---|---|---|
| Mountain ranges have structure | Linear ridges with peaks and saddles, flanked by foothills. | Uniform bumpy noise, or perfectly smooth cones. |
| Coastlines are fractal | Bays within bays, headlands, varying scales of detail. | Smooth curves, or uniformly jagged at one frequency. |

Sprites/Creatures:
| Characteristic | Real | Fake |
|---|---|---|
| Bilateral symmetry with variation | Left/right mirror with slight asymmetry. | Perfect pixel-identical symmetry, or no symmetry at all. |
| Proportions follow allometry | Head-to-body ratio scales with size; limbs proportional. | All creatures same proportions regardless of size. |

Vegetation:
| Characteristic | Real | Fake |
|---|---|---|
| Spatial competition | Trees near each other are smaller/sparser; gaps exist. | Uniform grid spacing, or random placement ignoring neighbors. |
| Stress response | Drought → brown, cold → dieback, in spatial patches. | All plants same health, or random per-plant health. |

### Step 2: Design Verification Metrics

For EACH characteristic from Step 1, design a specific measurement that would **PASS if the characteristic is real and FAIL if it is fake**.

Key principle: **If your metric cannot distinguish real from fake, it is useless.** Most aggregate statistics (mean, stddev, range) are useless for this purpose because they hide spatial and temporal structure.

Good metrics are:
- **Distributions** not averages (histograms, percentiles)
- **Spatial** — autocorrelation length, neighbor differences, patch size distribution
- **Temporal** — autocorrelation at lag, decorrelation time, time-series at sample points
- **Structural** — connectivity, symmetry scores, proportion ratios
- **Visual** — rendered images sent to GPT for domain-expert assessment

Bad metrics are:
- Global mean/stddev/min/max of any single quantity
- Counts without spatial context (e.g., "500 rainy cells" without knowing if they're clustered or scattered)
- Any metric that would give the same value for a realistic and unrealistic system

### Step 3: Implement and Run

Add the metrics from Step 2 to headless mode, a diagnostic function, or image output. Run long enough to observe the patterns (varies by system — seconds for sprites, days-to-years for weather, one generation for terrain).

**Output format**: Print structured data that can be analyzed:
- Distributions (histograms or percentiles), not just means
- Spatial breakdowns (by region, terrain type, organism class)
- Temporal evolution where applicable (how metrics change over the run)
- Sample point time-series (pick 3-6 representative locations and track them)
- Rendered images for visual assessment where applicable

### Step 4: Diagnose and Fix

Compare metrics against the "Real" column from Step 1. For each characteristic:
- **PASS**: The measurement shows the real pattern. Move on.
- **FAIL**: The measurement shows the fake pattern. Diagnose WHY.

Root causes, in order of likelihood:
1. **Missing mechanism**: The system doesn't contain whatever produces this pattern in reality. Fix: add the mechanism.
2. **Wrong scale**: The mechanism exists but operates at the wrong spatial or temporal scale. Fix: adjust scale parameters.
3. **Numerical artifact**: The mechanism exists but is suppressed by numerical diffusion, over-damping, over-clamping, or insufficient resolution. Fix: change the numerical approach.
4. **Parameter tuning**: The mechanism exists at the right scale but its strength is wrong. Fix: tune parameters.

**Fix in that order.** Don't tune parameters when the physics is missing. Don't add noise when you need structure.

### Step 5: Re-verify

After fixing, re-run the SAME metrics. Check that:
- The fixed characteristic now passes
- Previously passing characteristics still pass (no regressions)
- The system remains stable over long runs / many seeds

## Anti-Patterns

1. **Don't validate with aggregate statistics only.** They hide the structure that makes output look real or fake.

2. **Don't declare victory after compilation.** "It compiles and doesn't crash" tells you nothing about realism.

3. **Don't compare against your own expectations.** Compare against reality. If you think a number "looks good" but can't point to a real-world reference, you're guessing.

4. **Don't add noise as a fix.** If the system lacks a characteristic, the fix is usually a missing mechanism, not random perturbation. Noise without structure looks noisy, not real.

5. **Don't skip Step 1.** If you can't articulate what "real" looks like before you start, you can't verify it after.

## Template

```
## Reality Check: [System Name]

### Characteristics

| # | Characteristic | Real Behavior | Fake Behavior | Metric |
|---|---|---|---|---|
| 1 | ... | ... | ... | ... |
| 2 | ... | ... | ... | ... |

### Verification Output

[Describe what you'll add — headless diagnostics, image renders, test harness]

### Results

| # | Status | Evidence |
|---|---|---|
| 1 | PASS/FAIL | [specific numbers or image assessment] |
| 2 | PASS/FAIL | [specific numbers or image assessment] |

### Diagnosis (for FAILs)

[Root cause analysis and fix plan for each failure]
```

## Integration with Other Skills

- Use `gpt-behavior-tuning` for parameter-level fixes AFTER this skill identifies what's wrong
- Use `procgen-visual-review` for visual verification alongside numeric metrics
- Use `gpt-design-consult` if Step 1 reveals you need mechanisms you don't know how to implement
