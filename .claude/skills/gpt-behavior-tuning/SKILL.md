# GPT-Assisted Simulation Behavior Tuning

## When to Use

Use this skill when a simulation system's **runtime behavior** is wrong and you need to iteratively diagnose and fix it. This includes:

- Discharge/hydrology not forming river patterns
- Weather system producing unrealistic temperature distributions
- Vegetation dying too fast or growing uniformly
- Energy budgets drifting, populations crashing, values collapsing
- Any emergent behavior that doesn't match physical expectations

**Trigger**: When you've built or modified a simulation system and the output behavior is wrong — not a code bug, but a *tuning/physics* problem where you need expert feedback on what the telemetry reveals.

**Do NOT use for**: Visual review of images (use `procgen-visual-review`). Pre-implementation design questions (use `gpt-design-consult`). This skill is for **runtime behavior** diagnosed through **numerical telemetry**.

## The Loop

```
┌─ 1. DESCRIBE ──────────────────────────────────────┐
│  System, simulation goals, parameters, grid scale  │
└────────────────────────┬───────────────────────────-┘
                         ▼
┌─ 2. FOCUS ─────────────────────────────────────────┐
│  Which specific behavior is wrong? What do you     │
│  expect vs what you observe?                       │
└────────────────────────┬───────────────────────────-┘
                         ▼
┌─ 3. ASK: WHAT TELEMETRY? ─────────────────────────┐
│  GPT recommends metrics/diagnostics it needs to    │
│  understand the behavior. You implement them.      │
└────────────────────────┬───────────────────────────-┘
                         ▼
┌─ 4. GATHER ────────────────────────────────────────┐
│  Run the simulation, capture telemetry output.     │
│  Headless mode preferred for reproducibility.      │
└────────────────────────┬───────────────────────────-┘
                         ▼
┌─ 5. SEND TO GPT ──────────────────────────────────-┐
│  Telemetry data + source code → GPT analyzes and   │
│  suggests parameter changes or algorithmic fixes.  │
└────────────────────────┬───────────────────────────-┘
                         ▼
┌─ 6. IMPLEMENT ─────────────────────────────────────┐
│  Apply GPT's suggestions. May be parameter tuning  │
│  or structural algorithm changes.                  │
└────────────────────────┬───────────────────────────-┘
                         ▼
              ┌──────────┴──────────┐
              │ Behavior correct?   │
              │   YES → Done        │
              │   NO  → Go to 4    │
              └─────────────────────┘
```

## Detailed Steps

### Step 1: Describe the System

Start a new session describing the simulation system. Include:

- **What it simulates**: physical process, scale, grid resolution
- **Key data structures**: what fields each cell/entity carries
- **Tick pipeline**: what happens in what order each timestep
- **Key constants**: rates, thresholds, damping factors
- **Timestep**: how time advances (dt_days, ticks per second, etc.)

```bash
python3 .claude/skills/gpt-behavior-tuning/gpt_behavior.py \
  --name "discharge-rivers" \
  --system "Weather sandbox dynamics: per-tile hydrology on 256x256 terrain. Each tile has surface_water, soil_moisture, groundwater, discharge. Tick pipeline: precipitation→snowmelt→infiltration→evaporation→WSE flow routing→D8 discharge accumulation. Timestep ~0.03 days. Grid is 1 tile ≈ 1km." \
  --context sandboxes/weather/dynamics.h sandboxes/weather/dynamics.cpp \
  --behavior "Discharge overlay should show persistent river channels flowing from highlands to coast. Currently shows diffuse clumps under precipitation areas."
```

### Step 2: GPT Requests Telemetry

GPT's first response will request specific metrics. Examples it might ask for:

- Per-tile discharge distribution (histogram or percentiles)
- Spatial pattern: how many tiles have discharge > threshold at various thresholds
- Water budget: total precip in vs total evap out vs total ocean drain
- Time series: how discharge_max evolves over 100+ days
- Correlation: discharge vs elevation, discharge vs slope
- Upstream area: how many tiles flow into the highest-discharge tile

### Step 3: Implement the Telemetry

Add the requested diagnostics to headless mode or as a separate stats function. Keep it minimal — print numbers, don't build UI.

```cpp
// Example: discharge percentiles
std::vector<float> discharges;
for (auto& dt : state.tiles)
    if (!world.tiles[&dt - &state.tiles[0]].is_ocean)
        discharges.push_back(dt.discharge);
std::sort(discharges.begin(), discharges.end());
printf("Discharge p50=%.4f p90=%.4f p99=%.4f max=%.4f\n",
    discharges[discharges.size()/2], discharges[discharges.size()*9/10],
    discharges[discharges.size()*99/100], discharges.back());
```

### Step 4: Run and Capture

```bash
./build/sandboxes/weather/sandbox_weather --headless 365 Continental > telemetry_output.txt 2>&1
```

### Step 5: Send Telemetry to GPT

```bash
python3 .claude/skills/gpt-behavior-tuning/gpt_behavior.py \
  --session last \
  --telemetry telemetry_output.txt \
  --question "Here's the telemetry you requested. What does it tell you about why rivers aren't forming?"
```

Or inline for short output:

```bash
python3 .claude/skills/gpt-behavior-tuning/gpt_behavior.py \
  --session last \
  --telemetry-inline "Discharge p50=0.001 p90=0.003 p99=0.02 max=4.5
Water budget: precip_total=1240 evap_total=1180 ocean_drain=45 stored=15
Tiles with discharge>0.1: 23 (0.05% of land)" \
  --question "Is the flow accumulation working? Why so few high-discharge tiles?"
```

### Step 6: Implement and Re-gather

Apply GPT's suggestions, rebuild, re-run, and send the new telemetry:

```bash
python3 .claude/skills/gpt-behavior-tuning/gpt_behavior.py \
  --session last \
  --context sandboxes/weather/dynamics.cpp \
  --telemetry telemetry_output_v2.txt \
  --question "Applied your suggestions (reduced evap, normalized ksat). Here's the new telemetry."
```

### Repeat Until Converged

Keep iterating. A typical session:

```
Round 1: GPT asks for discharge distribution + water budget
  → Implement, run, send data
Round 2: GPT says "evaporation consumes 95% of precip before it can flow"
  → Reduce evap rates, re-run
Round 3: GPT says "discharge accumulates but only at 23 tiles — D8 routing isn't concentrating"
  → Fix D8 to use steepest descent, re-run
Round 4: GPT says "discharge_p99 now 0.8, good concentration. But temporal EMA too aggressive"
  → Adjust EMA from 0.5 to 0.2, re-run
Round 5: GPT says "river network looks reasonable: 200 tiles > 0.1, max=4.5, good hierarchy"
  → Done
```

## Script Usage

```bash
# Start new session
python3 gpt_behavior.py \
  --name "session-name" \
  --system "System description..." \
  --behavior "What's wrong..." \
  --context file1.h file2.cpp \

# Continue with telemetry data
python3 gpt_behavior.py \
  --session last \
  --telemetry output.txt \
  --question "Analysis?"

# Continue with inline telemetry
python3 gpt_behavior.py \
  --session last \
  --telemetry-inline "p50=0.01 p90=0.05 max=4.5" \
  --question "Better?"

# Send updated code + telemetry
python3 gpt_behavior.py \
  --session last \
  --context dynamics.cpp \
  --telemetry output_v2.txt \
  --question "Applied fixes. New results."

# List sessions
python3 gpt_behavior.py --list
```

## What Makes Good Telemetry

GPT can't see your screen. Give it **numbers**, not descriptions:

| Bad | Good |
|-----|------|
| "Rivers don't show up" | "discharge_max=0.01 after 365 days" |
| "Too much evaporation" | "precip_total=1240, evap_total=1180 (95%)" |
| "Values collapse" | "T_stddev day 1=11.6, day 30=8.2, day 120=3.1" |
| "Looks uniform" | "Tiles with discharge>0.1: 23 of 45000 land tiles" |

Include:
- **Distributions** (min/max/mean/p50/p90/p99) not just averages
- **Time series** (how values evolve over days/ticks)
- **Spatial breakdowns** (by latitude band, elevation band, distance from coast)
- **Budget accounting** (total in vs total out — where does mass/energy go?)
- **Counts** (how many tiles/entities in each state)

## Script Requirements

- Python 3.11+
- `openai` package (`pip install openai`)
- `OPENAI_API_KEY` in project `.env` file or environment

## Tips

- **Start with Step 3**: Don't guess what metrics matter. Let GPT tell you what it needs.
- **Send code alongside telemetry**: GPT catches parameter bugs (e.g., ksat not normalized by soil_depth) that pure telemetry can't reveal.
- **Use the same seed/preset across iterations** for apples-to-apples comparison.
- **Keep telemetry output under 200 lines** — truncate or summarize long runs.
- **Don't skip the budget**: If total_in != total_out, there's a conservation bug. GPT will ask for this.
- **Multiple presets**: After fixing one preset, verify others haven't regressed.
- **Structural vs parametric**: If GPT says "reduce evap by 10%", that's parametric. If GPT says "your flow routing is fundamentally diffusive, use D8 steepest descent", that's structural. Don't be afraid of structural changes — they're usually what fixes the real problem.
