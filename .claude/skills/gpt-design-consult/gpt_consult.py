#!/usr/bin/env python3
"""Multi-turn design consultation with GPT-5.2 for Darwin Chronicles.

Usage:
    # Start a new consultation
    python3 gpt_consult.py \
        --description "Add two-layer atmosphere with vertical instability" \
        --context sandboxes/weather/atmosphere.h sandboxes/weather/atmosphere.cpp \
        --question "What's the best approach for splitting into boundary layer + free troposphere?"

    # Continue the conversation (follow-up question)
    python3 gpt_consult.py \
        --session last \
        --question "How should I handle the vertical exchange step? Here's my current code: ..."

    # Include source files as context in follow-up
    python3 gpt_consult.py \
        --session last \
        --context sandboxes/weather/atmosphere.cpp \
        --question "I implemented your suggestion. Does this look correct?"

    # List previous sessions
    python3 gpt_consult.py --list

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
from typing import Optional

SESSION_DIR = Path(__file__).parent / ".sessions"

PROJECT_CONTEXT = """\
# Darwin Chronicles

Darwin Chronicles is a 2D evolution simulation in C++20 using EnTT (ECS). \
Organisms with genome-encoded neural networks sense, think, act, metabolize, \
learn, and reproduce in a procedurally generated world driven by planetary physics. \
All behavior is emergent — no hard-coded AI.

## Architecture (dependency order, lowest first)
- environment: planetary physics, climate
- world: terrain, biomes, nutrient fields (FastNoiseLite)
- organisms: genome, neural brain, metabolism, sensors
- evolution: reproduction, mutation, selection
- simulation: tick loop, system scheduling
- rendering: sprite gen, camera, debug viz, UI (SDL2)

## Sandboxes (standalone prototyping executables)
Sandboxes do NOT link darwin::* modules — only SDL2, FastNoiseLite, darwin_warnings. \
Code proven in a sandbox is later promoted into a darwin::* module.

Current sandboxes:
- worldgen: procedural terrain (tectonic plates, erosion, hydrology, coastline geomorphology)
- weather: atmospheric simulation (semi-Lagrangian advection, pressure/wind/moisture/clouds)
- veggen: vegetation/plant ecology simulation
- spritetest: procedural sprite rendering

## Key Design Principles
- Three-layer cascade: planetary physics -> climate -> evolutionary pressure
- Energy tradeoffs: brain complexity, body size, speed all have metabolic costs
- Emergent behavior only: no scripted AI, no hardcoded strategies
- Physical plausibility: systems should reflect real-world physics where practical
"""

SYSTEM_PROMPT = """\
You are an expert game systems architect and simulation engineer consulting on \
Darwin Chronicles, a C++20 evolution simulation. You provide detailed, actionable \
design suggestions for procedural generation, physics simulation, and game systems.

When suggesting implementations:
- Give specific algorithmic approaches with pseudocode when helpful
- Suggest concrete parameter ranges for the project's scale (typically 256x256 grids)
- Consider interaction with existing systems (terrain, weather, vegetation, organisms)
- Flag potential performance concerns for real-time simulation
- Prioritize physical plausibility over visual polish
- Note which suggestions are quick wins vs deep architectural changes

When reviewing code:
- Point out numerical stability issues, conservation law violations, or order-of-operations bugs
- Suggest parameter values based on the grid resolution and simulation timestep
- Identify missing physics that would most improve realism

Always be concrete and specific. Vague suggestions like "add more detail" are not helpful. \
Instead: "Add 3-octave ridged noise at frequency 0.04 with amplitude 0.08 for micro-terrain variation."
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
    """Read a source file and format it as a code block."""
    p = Path(path)
    if not p.exists():
        return f"[File not found: {path}]"
    content = p.read_text()
    # Truncate very large files
    lines = content.splitlines()
    if len(lines) > 500:
        content = "\n".join(lines[:500]) + f"\n\n[... truncated, {len(lines)} total lines ...]"
    ext = p.suffix.lstrip(".")
    return f"### {p.name}\n```{ext}\n{content}\n```"


def build_user_message(description: str | None, question: str | None,
                       context_files: list[str] | None, is_first: bool) -> str:
    """Build the user message with optional description, context files, and question."""
    parts = []

    if is_first and description:
        parts.append(f"## Proposed Change\n{description}")

    if context_files:
        parts.append("## Relevant Source Code")
        for f in context_files:
            parts.append(read_source_file(f))

    if question:
        parts.append(f"## Question\n{question}")

    return "\n\n".join(parts)


def get_session_path(name: str) -> Path:
    """Get the path for a session file."""
    SESSION_DIR.mkdir(parents=True, exist_ok=True)
    return SESSION_DIR / f"{name}.json"


def get_latest_session() -> Path | None:
    """Find the most recent session file."""
    SESSION_DIR.mkdir(parents=True, exist_ok=True)
    sessions = sorted(SESSION_DIR.glob("*.json"), key=lambda p: p.stat().st_mtime, reverse=True)
    return sessions[0] if sessions else None


def load_session(name: str) -> list[dict]:
    """Load conversation history from a session file."""
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
    """Save conversation history to a session file."""
    path = get_session_path(name)
    path.write_text(json.dumps(messages, indent=2))
    print(f"Session saved: {path.stem}", file=sys.stderr)


def list_sessions():
    """List all saved sessions."""
    SESSION_DIR.mkdir(parents=True, exist_ok=True)
    sessions = sorted(SESSION_DIR.glob("*.json"), key=lambda p: p.stat().st_mtime, reverse=True)
    if not sessions:
        print("No sessions found.")
        return
    for s in sessions:
        data = json.loads(s.read_text())
        # Find the first user message for a preview
        preview = ""
        for msg in data:
            if msg["role"] == "user":
                preview = msg["content"][:100].replace("\n", " ")
                break
        mtime = time.strftime("%Y-%m-%d %H:%M", time.localtime(s.stat().st_mtime))
        turns = sum(1 for m in data if m["role"] == "assistant")
        print(f"  {s.stem}  ({turns} replies, {mtime})  {preview}...")


def consult(messages: list[dict], model: str = "gpt-5.2") -> str:
    """Send messages to GPT and return the response."""
    load_env()
    from openai import OpenAI
    client = OpenAI()

    response = client.chat.completions.create(
        model=model,
        messages=messages,
        max_completion_tokens=4000,
    )
    return response.choices[0].message.content


def main():
    parser = argparse.ArgumentParser(description="GPT-5.2 design consultation for Darwin Chronicles")
    parser.add_argument("--description", help="Description of the proposed feature/change")
    parser.add_argument("--context", nargs="+", help="Source files to include as context")
    parser.add_argument("--question", help="Specific question to ask GPT")
    parser.add_argument("--session", help="Session name to continue ('last' for most recent)")
    parser.add_argument("--name", help="Name for this session (default: auto-generated)")
    parser.add_argument("--list", action="store_true", help="List saved sessions")
    parser.add_argument("--model", default="gpt-5.2", help="GPT model (default: gpt-5.2)")
    parser.add_argument("--json", action="store_true", help="Output as JSON")
    args = parser.parse_args()

    if args.list:
        list_sessions()
        return

    if not args.question and not args.description:
        parser.error("At least --question or --description is required")

    # Build or load conversation
    if args.session:
        messages = load_session(args.session)
        is_first = False
    else:
        messages = [
            {"role": "system", "content": SYSTEM_PROMPT},
            {"role": "system", "content": PROJECT_CONTEXT},
        ]
        is_first = True

    # Add user message
    user_msg = build_user_message(args.description, args.question, args.context, is_first)
    messages.append({"role": "user", "content": user_msg})

    # Send to GPT
    print("Consulting GPT-5.2...", file=sys.stderr)
    result = consult(messages, args.model)

    # Save response to conversation
    messages.append({"role": "assistant", "content": result})

    # Determine session name
    if args.session and args.session != "last":
        session_name = args.session
    elif args.session == "last":
        latest = get_latest_session()
        session_name = latest.stem if latest else f"consult_{int(time.time())}"
    else:
        session_name = args.name or f"consult_{int(time.time())}"

    save_session(session_name, messages)

    # Output
    if args.json:
        print(json.dumps({"response": result, "session": session_name, "model": args.model}))
    else:
        print(result)


if __name__ == "__main__":
    main()
