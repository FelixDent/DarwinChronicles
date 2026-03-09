#!/usr/bin/env python3
"""GPT-assisted simulation behavior tuning via iterative telemetry feedback.

Usage:
    # Start: describe system + problematic behavior
    python3 gpt_behavior.py \
        --name "discharge-rivers" \
        --system "Per-tile hydrology on 256x256 terrain..." \
        --behavior "Discharge should show river channels, shows diffuse clumps" \
        --context sandboxes/weather/dynamics.h sandboxes/weather/dynamics.cpp

    # Send telemetry GPT requested
    python3 gpt_behavior.py \
        --session last \
        --telemetry telemetry_output.txt \
        --question "Here's the data you asked for"

    # Send inline telemetry + updated code
    python3 gpt_behavior.py \
        --session last \
        --telemetry-inline "discharge p50=0.001 p90=0.05 max=4.5" \
        --context dynamics.cpp \
        --question "Applied your fix. Better?"

    # List sessions
    python3 gpt_behavior.py --list

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
You are an expert simulation engineer specializing in diagnosing and fixing \
the runtime behavior of physics-based simulation systems. You work iteratively: \
analyzing telemetry data, identifying root causes, and suggesting targeted fixes.

## Your Role in the Loop

1. **When given a system description + problematic behavior**: Request specific \
telemetry/metrics you need to diagnose the issue. Be precise — ask for distributions \
(percentiles, not just means), time series, spatial breakdowns, and conservation budgets.

2. **When given telemetry data**: Analyze it thoroughly. Identify:
   - Where mass/energy/water is going (budget analysis)
   - Whether values have the right order of magnitude
   - Whether distributions show expected structure (power-law for rivers, etc.)
   - Whether time series show convergence, drift, or oscillation
   - Root cause: is this a rate problem, algorithm problem, or missing physics?

3. **When suggesting fixes**: Be concrete:
   - "Change EVAP_RATE from 0.06 to 0.02" not "reduce evaporation"
   - "Replace proportional multi-neighbor flow with D8 steepest descent" not "improve routing"
   - Give the specific code change or algorithm with pseudocode
   - Classify as parametric (easy) vs structural (harder but more impactful)

4. **After a fix is applied**: Compare new vs old telemetry. Confirm improvement, \
identify remaining issues, request additional metrics if needed.

## Key Principles

- **Conservation first**: If total_in != total_out, find the leak before tuning rates.
- **Order of magnitude**: A rate that's 100x too large can't be fixed by tuning — it needs rethinking.
- **Distributions matter**: mean=0.05 with max=4.5 is very different from mean=0.05 with max=0.06.
- **Spatial structure**: Uniform values across space usually means diffusion is too strong or \
forcing is too weak. Sharp gradients mean the opposite.
- **Emergent behavior needs accumulation**: Rivers need flow accumulation, not local rates. \
Weather patterns need advection, not per-cell noise. Check that the algorithm allows emergence.

## Context

This is Darwin Chronicles, a C++20 evolution simulation with procedural terrain, \
atmospheric weather, hydrology, and vegetation systems. Sandboxes prototype subsystems \
in isolation on ~256x256 grids. Headless mode prints diagnostic telemetry.
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
    """Read telemetry output file, truncating if too long."""
    p = Path(path)
    if not p.exists():
        return f"[Telemetry file not found: {path}]"
    content = p.read_text()
    lines = content.splitlines()
    if len(lines) > 200:
        # Keep first 50 and last 150 lines (header + recent data)
        content = "\n".join(lines[:50]) + \
            f"\n\n[... {len(lines) - 200} lines omitted ...]\n\n" + \
            "\n".join(lines[-150:])
    return content


def build_user_message(
    system_desc: str | None,
    behavior: str | None,
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
            parts.append(f"## System Description\n{system_desc}")
        if behavior:
            parts.append(
                f"## Problematic Behavior\n{behavior}\n\n"
                "Please tell me what telemetry/metrics you need to diagnose this. "
                "Be specific about distributions, time series, budgets, and spatial breakdowns."
            )

    if context_files:
        parts.append("## Source Code")
        for f in context_files:
            parts.append(read_source_file(f))

    if telemetry_file:
        parts.append(f"## Telemetry Data\n```\n{read_telemetry(telemetry_file)}\n```")

    if telemetry_inline:
        parts.append(f"## Telemetry Data\n```\n{telemetry_inline}\n```")

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
        description="GPT-assisted simulation behavior tuning"
    )
    parser.add_argument("--system", help="Description of the simulation system")
    parser.add_argument("--behavior", help="What behavior is wrong and what you expect")
    parser.add_argument("--context", nargs="+", help="Source files to include")
    parser.add_argument("--telemetry", help="Path to telemetry output file")
    parser.add_argument("--telemetry-inline", help="Inline telemetry data (short)")
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

    if not any([args.system, args.behavior, args.question, args.telemetry, args.telemetry_inline]):
        parser.error("Provide at least one of: --system, --behavior, --question, --telemetry")

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
        behavior=args.behavior,
        question=args.question,
        context_files=args.context,
        telemetry_file=args.telemetry,
        telemetry_inline=args.telemetry_inline,
        is_first=is_first,
    )
    messages.append({"role": "user", "content": user_msg})

    # Send to GPT
    print("Consulting GPT...", file=sys.stderr)
    result = consult(messages, args.model)
    messages.append({"role": "assistant", "content": result})

    # Session name
    if args.session and args.session != "last":
        session_name = args.session
    elif args.session == "last":
        latest = get_latest_session()
        session_name = latest.stem if latest else f"behavior_{int(time.time())}"
    else:
        session_name = args.name or f"behavior_{int(time.time())}"

    save_session(session_name, messages)

    # Output
    if args.json:
        print(json.dumps({"response": result, "session": session_name, "model": args.model}))
    else:
        print(result)


if __name__ == "__main__":
    main()
