# GPT-Assisted UI Performance Consulting

## When to Use

Use this skill when you need expert feedback on **user-facing performance** — frame rates, input latency, load times, simulation tick throughput, or perceived smoothness. This is the performance counterpart to `gpt-ui-review` (visual quality) and `gpt-behavior-tuning` (simulation correctness).

**Trigger**: When performance is degraded or when implementing changes to hot paths, rendering loops, or loading pipelines where you need to decide on metrics, benchmarks, and optimization strategy.

**Do NOT use for**: Visual quality of overlays (use `gpt-ui-review`). Simulation behavior tuning (use `gpt-behavior-tuning`). Simple code review (the perf-ux-reviewer agent handles that internally).

## The Loop

```
┌─ 1. DESCRIBE ──────────────────────────────────────┐
│  System architecture, hot paths, grid sizes,       │
│  current perf characteristics, target platform     │
└────────────────────────┬───────────────────────────-┘
                         ▼
┌─ 2. FOCUS ─────────────────────────────────────────┐
│  What is slow? What does the user experience?      │
│  What changed? What are the constraints?           │
└────────────────────────┬───────────────────────────-┘
                         ▼
┌─ 3. ASK: WHAT METRICS? ─────────────────────────── ┐
│  GPT recommends telemetry, benchmarks, and         │
│  profiling approaches to diagnose the issue.       │
└────────────────────────┬───────────────────────────-┘
                         ▼
┌─ 4. INSTRUMENT ───────────────────────────────────-┐
│  Add timing instrumentation, run benchmarks,       │
│  capture profiling data.                           │
└────────────────────────┬───────────────────────────-┘
                         ▼
┌─ 5. SEND TO GPT ──────────────────────────────────-┐
│  Perf data + source code → GPT analyzes and        │
│  suggests optimizations with expected speedup.     │
└────────────────────────┬───────────────────────────-┘
                         ▼
┌─ 6. IMPLEMENT + MEASURE ──────────────────────────-┐
│  Apply fixes, re-run benchmarks, compare.          │
└────────────────────────┬───────────────────────────-┘
                         ▼
              ┌──────────┴──────────┐
              │ Meets benchmarks?   │
              │ YES → Ship it       │
              │ NO  → Go to 5       │
              └─────────────────────┘
```

## Step 1: Describe the System

Provide GPT with:

- **Architecture**: Language, frameworks, rendering API, ECS, grid sizes
- **Hot paths**: What runs per-frame, per-tick, per-tile, per-entity
- **Current performance**: Rough frame times, tick durations, load times
- **Hardware context**: Target platform, typical GPU/CPU class
- **Existing instrumentation**: What timings/stats already exist

## Step 2: Define the Performance Problem

Be specific about the user experience:

- "Zooming hangs for 2-3 seconds" (not "zoom is slow")
- "Frame rate drops from 60 to 15 fps when temperature overlay is active"
- "Terrain generation takes 8 seconds on startup"
- "Simulation tick stutters every ~10 frames"

Include what changed and what the user sees.

## Step 3: Ask GPT What to Measure

GPT will recommend:

- **Micro-benchmarks**: Specific functions to time via Google Benchmark
- **Frame timing**: Where to add `SDL_GetPerformanceCounter` instrumentation
- **Memory profiling**: Allocation patterns to check
- **Cache analysis**: Data layout / access pattern concerns
- **Bottleneck isolation**: How to narrow down the hot spot

## Step 4: Instrument and Measure

Add timing code following existing project patterns (`GenerationTimings`, `AtmosphereStats`). Run benchmarks and capture results.

```bash
# Run with timing output
./build-release/sandboxes/weather/sandbox_weather --headless 30

# Run micro-benchmarks
./build-release/benches/darwin_benchmarks --benchmark_filter=TerrainRender
```

## Step 5: Send to GPT for Analysis

Use the helper script:

```bash
python3 .claude/skills/gpt-perf-consult/gpt_perf_consult.py \
    --name "terrain-bake-perf" \
    --system "SDL2 sandbox, 256x128 terrain grid, two-level clipmap cache..." \
    --problem "Baking terrain textures takes 4 seconds at startup" \
    --context sandboxes/weather/tile_texture.cpp sandboxes/weather/renderer.cpp \
    --telemetry perf_output.txt
```

### Follow-up rounds:

```bash
python3 .claude/skills/gpt-perf-consult/gpt_perf_consult.py \
    --session last \
    --telemetry-inline "Before: 4200ms. After SIMD: 1800ms. After LOD: 900ms" \
    --context sandboxes/weather/tile_texture.cpp \
    --question "Applied vectorization + LOD skip. What next?"
```

## Step 6: Implement and Verify

After applying GPT's suggestions:

1. Re-run the same benchmarks
2. Compare before/after numbers
3. Check for correctness regressions (visual output unchanged)
4. Send updated numbers to GPT for confirmation

## Performance Benchmarks (Project-Specific)

These are the performance targets for Darwin Chronicles sandboxes:

| Metric | Target | Unacceptable |
|--------|--------|-------------|
| Frame render time | < 16ms (60fps) | > 33ms (30fps) |
| Overlay switch | < 50ms | > 200ms |
| Terrain bake (startup) | < 3s | > 8s |
| Simulation tick | < 5ms | > 16ms |
| Camera pan/zoom | 0ms (no regen) | > 100ms |
| Input-to-visual latency | < 50ms | > 150ms |
| Memory per frame | 0 allocations | > 1MB/frame |

## Script Usage

```bash
# New analysis session
python3 gpt_perf_consult.py \
    --name "overlay-perf" \
    --system "Description of architecture..." \
    --problem "Temperature overlay drops to 20fps" \
    --context renderer.cpp \
    --telemetry timing_log.txt

# Continue session with new data
python3 gpt_perf_consult.py \
    --session last \
    --telemetry new_timing.txt \
    --question "Applied batch rendering, here are new numbers"

# List sessions
python3 gpt_perf_consult.py --list

# JSON output
python3 gpt_perf_consult.py --session last --telemetry data.txt --json
```

## Script Requirements

- Python 3.11+
- `openai` package (`pip install openai`)
- `OPENAI_API_KEY` in project `.env` file or environment
