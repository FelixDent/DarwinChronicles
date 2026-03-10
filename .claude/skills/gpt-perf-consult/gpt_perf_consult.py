#!/usr/bin/env python3
"""GPT-assisted UI performance consulting via iterative benchmarking feedback.

Usage:
    # Start: describe system + performance problem
    python3 gpt_perf_consult.py \
        --name "terrain-bake" \
        --system "SDL2 sandbox, 256x128 grid, two-level clipmap cache..." \
        --problem "Texture baking takes 4 seconds at startup" \
        --context sandboxes/weather/tile_texture.cpp sandboxes/weather/renderer.cpp

    # Send benchmark data GPT requested
    python3 gpt_perf_consult.py \
        --session last \
        --telemetry perf_output.txt \
        --question "Here's the timing data you asked for"

    # Send inline metrics + updated code
    python3 gpt_perf_consult.py \
        --session last \
        --telemetry-inline "Before: 4200ms. After: 1800ms." \
        --context renderer.cpp \
        --question "Applied your fix. What next?"

    # List sessions
    python3 gpt_perf_consult.py --list

Requires: OPENAI_API_KEY in .env (project root) or environment.
Dependencies: openai (pip install openai)
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import time
from pathlib import Path

SESSION_DIR = Path(__file__).parent / ".sessions"

SYSTEM_PROMPT = """\
You are an expert performance engineer specializing in real-time interactive \
applications — frame rates, input latency, load times, simulation throughput, \
and perceived smoothness. You work iteratively: analyzing benchmark data, \
identifying bottlenecks, and suggesting targeted optimizations with expected \
speedup estimates.

## Your Role in the Loop

1. **When given a system description + performance problem**: Request specific \
benchmarks, timing instrumentation, and profiling data you need to diagnose the \
issue. Be precise — ask for per-function timings, allocation counts, cache miss \
rates, or specific micro-benchmarks.

2. **When given benchmark/timing data**: Analyze it thoroughly. Identify:
   - Where time is spent (hotspot analysis)
   - Whether the bottleneck is CPU, GPU, memory bandwidth, or I/O
   - Algorithmic complexity issues (O(n²) where O(n) exists)
   - Memory allocation patterns (per-frame allocs, large temporaries)
   - Cache-unfriendly access patterns
   - Opportunities for batching, LOD, caching, or lazy evaluation

3. **When suggesting optimizations**: Be concrete:
   - "Use SDL_TEXTUREACCESS_STREAMING with lock/unlock instead of UpdateTexture" \
not "reduce texture overhead"
   - "Replace per-pixel noise eval with lookup table" not "cache results"
   - Give the specific code change with estimated speedup
   - Classify as easy (parameter/flag change) vs medium (algorithm change) vs \
hard (architecture change)
   - Warn about correctness risks

4. **After an optimization is applied**: Compare new vs old benchmarks. Confirm \
improvement, identify remaining bottlenecks, suggest next optimization if target \
not yet met.

## Key Principles

- **Profile first**: Never guess at bottlenecks. Request measurements.
- **Amdahl's Law**: 50% speedup on 10% of runtime = 5% total. Focus on the \
biggest contributors first.
- **User perception**: 60fps→120fps matters less than 15fps→30fps. Prioritize \
by user impact.
- **Correctness over speed**: Never sacrifice correctness. Flag all tradeoffs.
- **Platform awareness**: SDL2 on Linux/Windows, C++20, typical desktop GPU. \
Consider both debug and release build behavior.
- **Batch over per-item**: One draw call beats N draw calls. One texture update \
beats N small updates. One allocation beats N small allocations.

## Performance Targets (Darwin Chronicles)

| Metric | Target | Unacceptable |
|--------|--------|-------------|
| Frame render | < 16ms | > 33ms |
| Overlay switch | < 50ms | > 200ms |
| Terrain bake | < 3s | > 8s |
| Simulation tick | < 5ms | > 16ms |
| Camera pan/zoom | 0ms | > 100ms |
| Per-frame allocs | 0 | > 1MB |

## Context

Darwin Chronicles: C++20 evolution simulation with SDL2 rendering, EnTT ECS, \
FastNoiseLite procedural generation. Sandboxes prototype subsystems on ~256x128 \
grids. Two-level terrain clipmap cache, atmospheric simulation, per-tile dynamics.
"""


def load_env():
    """Load OPENAI_API_KEY from .env files or environment."""
    if os.environ.get("OPENAI_API_KEY"):
        return
    cwd = Path.cwd()
    for d in [cwd, *cwd.parents]:
        env_path = d / ".env"
        if env_path.exists():
            for line in env_path.read_text().splitlines():
                line = line.strip()
                if line.startswith("#") or "=" not in line:
                    continue
                key, _, value = line.partition("=")
                key, value = key.strip(), value.strip()
                if key == "OPENAI_API_KEY" and value:
                    os.environ["OPENAI_API_KEY"] = value
                    return
    print("ERROR: OPENAI_API_KEY not found in .env or environment", file=sys.stderr)
    sys.exit(1)


def read_source_file(path: str) -> str:
    """Read a source file and format as a code block."""
    p = Path(path)
    if not p.exists():
        return f"[File not found: {path}]"
    content = p.read_text()
    lines = content.splitlines()
    if len(lines) > 500:
        content = "\n".join(lines[:500]) + f"\n\n[... truncated, {len(lines)} total lines ...]"
    ext = p.suffix.lstrip(".")
    return f"### {p.name}\n```{ext}\n{content}\n```"


def read_telemetry(path: str) -> str:
    """Read telemetry/benchmark output file, truncating if too long."""
    p = Path(path)
    if not p.exists():
        return f"[File not found: {path}]"
    content = p.read_text()
    lines = content.splitlines()
    if len(lines) > 200:
        content = "\n".join(lines[:50]) + \
            f"\n\n[... {len(lines) - 200} lines omitted ...]\n\n" + \
            "\n".join(lines[-150:])
    return content


def build_user_message(
    system_desc: str | None,
    problem: str | None,
    question: str | None,
    context_files: list[str] | None,
    telemetry_file: str | None,
    telemetry_inline: str | None,
    is_first: bool,
) -> str:
    """Build the user message from all provided inputs."""
    parts = []

    if is_first:
        if system_desc:
            parts.append(f"## System Architecture\n{system_desc}")
        if problem:
            parts.append(
                f"## Performance Problem\n{problem}\n\n"
                "Please tell me what benchmarks, timing instrumentation, or profiling data "
                "you need to diagnose this. Be specific about what to measure and how."
            )

    if context_files:
        parts.append("## Source Code")
        for f in context_files:
            parts.append(read_source_file(f))

    if telemetry_file:
        parts.append(f"## Benchmark/Timing Data\n```\n{read_telemetry(telemetry_file)}\n```")

    if telemetry_inline:
        parts.append(f"## Benchmark/Timing Data\n```\n{telemetry_inline}\n```")

    if question:
        parts.append(f"## Question\n{question}")

    return "\n\n".join(parts)


# ── Session management ─────────────────────────────────────────────────────

def get_session_path(name: str) -> Path:
    SESSION_DIR.mkdir(parents=True, exist_ok=True)
    return SESSION_DIR / f"{name}.json"


def get_latest_session() -> Path | None:
    SESSION_DIR.mkdir(parents=True, exist_ok=True)
    sessions = sorted(SESSION_DIR.glob("*.json"), key=lambda p: p.stat().st_mtime, reverse=True)
    return sessions[0] if sessions else None


def load_session(name: str) -> list[dict]:
    if name == "last":
        path = get_latest_session()
        if not path:
            print("ERROR: No previous sessions found", file=sys.stderr)
            sys.exit(1)
        print(f"Continuing session: {path.stem}", file=sys.stderr)
    else:
        path = get_session_path(name)
        if not path.exists():
            print(f"ERROR: Session not found: {name}", file=sys.stderr)
            sys.exit(1)
    return json.loads(path.read_text())


def save_session(name: str, messages: list[dict]):
    path = get_session_path(name)
    path.write_text(json.dumps(messages, indent=2))
    print(f"Session saved: {path.stem}", file=sys.stderr)


def list_sessions():
    SESSION_DIR.mkdir(parents=True, exist_ok=True)
    sessions = sorted(SESSION_DIR.glob("*.json"), key=lambda p: p.stat().st_mtime, reverse=True)
    if not sessions:
        print("No sessions found.")
        return
    for s in sessions:
        data = json.loads(s.read_text())
        preview = ""
        for msg in data:
            if msg["role"] == "user":
                preview = msg["content"][:100].replace("\n", " ")
                break
        mtime = time.strftime("%Y-%m-%d %H:%M", time.localtime(s.stat().st_mtime))
        turns = sum(1 for m in data if m["role"] == "assistant")
        print(f"  {s.stem}  ({turns} replies, {mtime})  {preview}...")


# ── GPT call ────────────────────────────────────────────────────────────────

def consult(messages: list[dict], model: str = "gpt-5.2") -> str:
    load_env()
    from openai import OpenAI
    client = OpenAI()

    response = client.chat.completions.create(
        model=model,
        messages=messages,
        max_completion_tokens=4000,
    )
    return response.choices[0].message.content


# ── Main ────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="GPT-assisted UI performance consulting"
    )
    parser.add_argument("--system", help="Description of the system architecture")
    parser.add_argument("--problem", help="What performance problem the user experiences")
    parser.add_argument("--context", nargs="+", help="Source files to include")
    parser.add_argument("--telemetry", help="Path to benchmark/timing output file")
    parser.add_argument("--telemetry-inline", help="Inline benchmark data (short)")
    parser.add_argument("--question", help="Specific question for GPT")
    parser.add_argument("--session", help="Session to continue ('last' for most recent)")
    parser.add_argument("--name", help="Name for new session (default: auto)")
    parser.add_argument("--list", action="store_true", help="List saved sessions")
    parser.add_argument("--model", default="gpt-5.2", help="GPT model (default: gpt-5.2)")
    parser.add_argument("--json", action="store_true", help="Output as JSON")
    args = parser.parse_args()

    if args.list:
        list_sessions()
        return

    if not any([args.system, args.problem, args.question, args.telemetry, args.telemetry_inline]):
        parser.error("Provide at least one of: --system, --problem, --question, --telemetry")

    # Build or load conversation
    if args.session:
        messages = load_session(args.session)
        is_first = False
    else:
        messages = [{"role": "system", "content": SYSTEM_PROMPT}]
        is_first = True

    # Build user message
    user_msg = build_user_message(
        system_desc=args.system,
        problem=args.problem,
        question=args.question,
        context_files=args.context,
        telemetry_file=args.telemetry,
        telemetry_inline=args.telemetry_inline,
        is_first=is_first,
    )
    messages.append({"role": "user", "content": user_msg})

    # Send to GPT
    print("Consulting GPT on performance...", file=sys.stderr)
    result = consult(messages, args.model)
    messages.append({"role": "assistant", "content": result})

    # Session name
    if args.session and args.session != "last":
        session_name = args.session
    elif args.session == "last":
        latest = get_latest_session()
        session_name = latest.stem if latest else f"perf_{int(time.time())}"
    else:
        session_name = args.name or f"perf_{int(time.time())}"

    save_session(session_name, messages)

    # Output
    if args.json:
        print(json.dumps({"response": result, "session": session_name, "model": args.model}))
    else:
        print(result)


if __name__ == "__main__":
    main()
