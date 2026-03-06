# Game Code Review

## When to use this skill
Use this skill when reviewing diffs, pull requests, or major refactors
in this C++ game project.

## Checklist
- Correctness and clarity.
- Determinism and fixed timestep safety.
- Memory safety and ownership.
- Performance in hot paths.
- Alignment with architecture in `CLAUDE.md`.

## Procedure
1. Scan the diff for:
   - New loops in the main update/render path.
   - New allocations and virtual calls.
   - Changes to core data structures.
2. For each issue:
   - Describe the problem.
   - Explain potential impact (bugs, perf, maintainability).
   - Propose specific, concrete fixes.

## Output format
Respond with:
- Summary.
- Strengths.
- Issues grouped by severity: High / Medium / Low.
- Recommended follow-ups.
