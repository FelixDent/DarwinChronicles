# Docs Maintainer Skill

This skill defines how to keep project documentation in sync after code changes. It is designed to be invoked by a subagent after every major change (new feature, new sandbox, new system, refactor, or architectural shift).

## Trigger

Run this after ANY of:
- Adding or removing a sandbox
- Adding or significantly changing a simulation system (terrain, weather, hydrology, vegetation, etc.)
- Adding or changing overlays, controls, or CLI modes
- Changing build system or dependencies
- Adding new files/modules to the project structure
- Refactoring data structures that affect external-facing behavior

## Files to Keep in Sync

### Tier 1 — Always update (authoritative sources)

| File | What it covers | Update when |
|---|---|---|
| `CLAUDE.md` | Sandboxes section, build commands, architecture, code style | Any sandbox or module change |
| `docs/CHANGELOG.md` | Chronological list of notable changes | Every significant change |

### Tier 2 — Update when structurally relevant

| File | What it covers | Update when |
|---|---|---|
| `README.md` | Project overview, repo structure tree, core systems, project status checklist | New sandboxes, new systems, status changes |
| `docs/ARCHITECTURE.md` | Module graph, ECS patterns, sandboxes section, file conventions | New modules, new sandboxes, dependency changes |
| `docs/README.md` | Index table of all doc files | New doc files added |

### Tier 3 — Update when system-specific changes are made

| File | What it covers | Update when |
|---|---|---|
| `docs/systems/TERRAIN_GENERATION.md` | Terrain generation pipeline (worldgen sandbox) | Terrain gen changes |
| `docs/systems/WEATHER_SYSTEM.md` | Weather/atmosphere system (weather sandbox) | Weather, atmosphere, or dynamics changes |
| `docs/systems/PLANT_GENERATION.md` | Vegetation simulation and plant sprites (veggen/spritetest sandboxes) | Plant simulation or sprite changes |
| Other `docs/systems/*.md` | System-specific design docs | Respective system changes |
| `docs/decisions/*.md` | ADRs for significant design choices | New architectural decisions |

## Procedure

### Step 1 — Identify what changed

Read the recent git diff or conversation context to determine:
- What files were added/modified
- What systems were affected
- What user-facing behavior changed (controls, overlays, CLI flags, etc.)

### Step 2 — Read current doc state

Read all Tier 1 files and any relevant Tier 2/3 files. Compare against actual code state.

### Step 3 — Update each file

For each file that needs updating:

#### CLAUDE.md — Sandboxes section
- Each sandbox gets a full description: purpose, key features, data model, controls, overlays, CLI modes
- Include file lists for sandboxes that share sources across directories
- Keep the description factual and dense — this is the primary reference for the AI assistant
- Update build commands if any changed

#### docs/CHANGELOG.md
- Add entries under `## [Unreleased]` with `### Added` / `### Changed` / `### Fixed` subsections
- Each entry is one line, present tense, describing the change concisely
- Group related changes (e.g., "Dynamic weather simulation with terrain interaction" not 5 separate bullet points)

#### README.md
- Update the repository structure tree if new directories were added
- Update the "Project Status" checklist (check off implemented items, add new in-progress items)
- Update the "Core Systems" section if a new major system was added
- Keep the Quick Start section accurate

#### docs/ARCHITECTURE.md
- Update the sandboxes section with new sandbox descriptions
- Update the module graph if dependencies changed
- Keep file conventions current

#### docs/README.md
- Add rows for any new documentation files

### Step 4 — Verify consistency

After all edits, check that:
- Sandbox names in CLAUDE.md match actual directory names
- Key bindings listed in docs match actual code
- CLI flags listed in docs match actual argument parsing
- Overlay mode names match the enum values
- File paths referenced in docs exist

## Style Rules

- **No code snippets in guide-type docs** (README, ARCHITECTURE, system docs) unless absolutely necessary for clarity. Describe *how things work and why* in plain language. If a reader needs to see code, link to the relevant source file/line instead of inlining it. CLAUDE.md is the exception — it serves the AI and can include terse technical detail.
- Keep CLAUDE.md entries dense and technical — it is for the AI, not end users
- Keep README.md entries readable for humans — explain the "what" and "why", not the "how" at code level
- Keep ARCHITECTURE.md focused on system relationships, data flow, and design rationale — not implementation mechanics
- Keep CHANGELOG.md entries concise — one line per change, grouped logically
- Use present tense in CHANGELOG.md ("Add X", "Fix Y", not "Added X")
- Use consistent formatting: backticks for code/paths, bold for emphasis, tables for structured data
- When referencing code, use relative file paths (e.g., `sandboxes/weather/dynamics.cpp`) rather than pasting snippets
- Never invent features that don't exist in the code — read before writing
- Do not rewrite sections that are already accurate — only update what changed

## What NOT to do

- Do not create new documentation files unless the user explicitly asks
- Do not update module READMEs (`src/*/README.md`) unless the module code changed
- Do not touch `docs/ideation/` — those are design notes, not maintained docs
- Do not add speculative "planned" features — only document what exists
- Do not bloat CLAUDE.md with implementation details that belong in system docs
