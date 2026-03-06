# Gameplay Feature Development

## When to use this skill
Use this skill whenever implementing or refactoring a gameplay mechanic
(movement, combat, building/mining, AI, world interactions) in this game.

## Procedure
1. Clarify the mechanic in terms of:
   - Inputs (player, world, systems).
   - State changes.
   - Visual/audio feedback.
   - Performance constraints (per-frame cost).
2. Design the system:
   - Identify which modules/files are involved.
   - Define data structures and ownership clearly.
   - Keep simulation deterministic with a fixed timestep.
3. Implementation steps:
   - Implement core simulation logic first (headless-friendly).
   - Add minimal debug or test harness (e.g., a test scene or unit tests).
   - Only then integrate with rendering and input.
4. Validation:
   - Describe how to manually test the mechanic.
   - Note any edge cases, future extensions, or exploit risks.

## Output format
- Step-by-step plan.
- Proposed file and API changes.
- Example code snippets.
- Manual test checklist.
