# Modern C++ Game Development

## When to use this skill
Use this skill whenever you are writing or refactoring C++ code in this repository.

## Objectives
- Follow C++20 best practices suited to real-time games.
- Keep the main loop and core systems performant and deterministic.
- Minimize dynamic allocations and virtual calls in hot paths.

## Procedure
1. Before making changes, skim `CLAUDE.md` to understand the architecture and constraints.
2. Prefer:
   - RAII for resource management.
   - `std::unique_ptr` at ownership edges, value types by default.
   - `std::vector` and contiguous data layouts in performance-critical code.
3. Avoid:
   - Raw `new`/`delete`.
   - Unnecessary `std::shared_ptr` and `std::function` in hot loops.
   - Logic mixed into rendering functions.
4. For each change:
   - Justify design choices in terms of determinism, performance, and readability.
   - Point out any future refactors you recommend.

## Output format
When using this skill, respond with:
- Summary of the change.
- Explanation of how it follows the Modern C++ Game principles.
- Any potential trade-offs or open questions.
