# GPT Design Consultation

## When to Use

Use this skill when you need to **design, architect, or plan a new feature or system change** and want expert feedback before implementation. This includes:

- Proposing a new simulation system (atmosphere layers, erosion model, neural architecture)
- Planning a major refactor or architectural change
- Deciding between algorithmic approaches for a specific problem
- Getting parameter recommendations for a specific grid resolution / timestep
- Reviewing an implementation for correctness, stability, or missing physics

**Trigger**: Before implementing a complex feature, or when stuck on a design decision.

**Do NOT use for**: Visual review of generated images (use `procgen-visual-review` instead).

## Workflow

### 1. Identify Relevant Context

Determine which source files GPT needs to understand the current state. Include:
- The header file(s) defining the data structures being changed
- The implementation file(s) containing the logic being modified
- Any related files that the change interacts with

Keep context focused — 2-5 files maximum. For large files, the script auto-truncates at 500 lines.

### 2. Write the Description

Prepare a clear description covering:
- **What** you want to add or change (1-2 sentences)
- **Why** — what limitation or goal motivates this
- **Current state** — brief summary of how things work now
- **Constraints** — what must be preserved (API compatibility, performance, existing behavior)

### 3. Send to GPT

```bash
python3 .claude/skills/gpt-design-consult/gpt_consult.py \
  --description "Add two-layer atmosphere (boundary layer + free troposphere) to enable vertical instability, inversions, and jet streams. Currently single-layer with semi-Lagrangian advection." \
  --context sandboxes/weather/atmosphere.h sandboxes/weather/atmosphere.cpp \
  --question "What fields should the upper layer carry? How should vertical exchange work?"
```

GPT-5.2 receives:
- Embedded project context (Darwin Chronicles architecture, sandbox pattern, design principles)
- Your source files as code blocks
- Your description and question

### 4. Analyze the Response

GPT will provide:
- Specific algorithmic suggestions with pseudocode
- Concrete parameter ranges for your grid resolution
- Interaction concerns with other systems
- Quick wins vs deep changes classification

### 5. Follow Up (Optional)

Ask clarifying questions or send updated code for review:

```bash
# Ask a follow-up question
python3 .claude/skills/gpt-design-consult/gpt_consult.py \
  --session last \
  --question "How should I handle energy conservation across the two layers?"

# Send implementation for review
python3 .claude/skills/gpt-design-consult/gpt_consult.py \
  --session last \
  --context sandboxes/weather/atmosphere.cpp \
  --question "I implemented vertical exchange. Does this look correct? Any stability concerns?"
```

The `--session last` flag continues the previous conversation so GPT has full context of the discussion.

### 6. Implement

Take GPT's suggestions and implement them. For complex implementations, send the code back for review before committing.

## Multi-Turn Example Session

```bash
# Round 1: Initial design question
python3 gpt_consult.py \
  --description "Replace noise-based terrain ridges with tectonic spine system" \
  --context sandboxes/worldgen/terrain_gen.cpp \
  --question "How should continental spines interact with plate boundaries?"

# Round 2: Clarify a suggestion
python3 gpt_consult.py \
  --session last \
  --question "You mentioned along-strike segmentation — what noise frequency and amplitude work at 256x256?"

# Round 3: Code review
python3 gpt_consult.py \
  --session last \
  --context sandboxes/worldgen/terrain_gen.cpp \
  --question "Here's my spine implementation. Does the Gaussian envelope look right?"
```

## Session Management

```bash
# List all saved sessions
python3 gpt_consult.py --list

# Continue a specific session by name
python3 gpt_consult.py --session consult_1709123456 --question "One more thing..."

# Name a session explicitly
python3 gpt_consult.py --name "two-layer-atmo" \
  --description "..." --question "..."
```

Sessions are saved in `.claude/skills/gpt-design-consult/.sessions/`.

## Script Requirements

- Python 3.11+
- `openai` package (`pip install openai`)
- `OPENAI_API_KEY` in project `.env` file or environment

## Tips

- **Include headers, not just .cpp** — GPT needs to see the data structures to suggest field additions
- **Be specific in questions** — "How should I do X?" gets better answers than "What do you think?"
- **Send code back for review** — GPT catches numerical stability issues, conservation violations, and order-of-operations bugs that are easy to miss
- **Use follow-ups** — multi-turn conversations give better results than cramming everything into one prompt
- **Combine with procgen-visual-review** — use this skill for design, then visual review for tuning
