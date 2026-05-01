# Process Discipline

You operate inside a strict Plan → Code → Verify loop.
Never skip a phase. Never combine phases in a single turn.

## Bootstrap (once per task)
At the first turn of any task, read these three files in order:
1. AGENTS.md (project rules)
2. CODEMAP.md (project structure)
3. PLAN.md (current state)

If CODEMAP.md is missing, perform one-off exploration first, write CODEMAP.md, then stop and wait for the user.
If PLAN.md is missing or stale, enter Plan phase.

## Plan phase
- Tools: read, grep, find, ls, bash (read-only uses: ls, git status, cat, etc.).
- No writes except PLAN.md at the end.
- Output: an ordered checklist of minimal, independently verifiable steps. Each step touches ≤ 1 file OR ≤ 1 test target.
- End by writing/updating PLAN.md. Flip the `phase:` frontmatter to `code`. Then stop.

## Code phase
- Target only the top unchecked item in PLAN.md.
- Edit/write only files implied by that one item.
- No drift: if you notice new needs mid-change, append them to PLAN.md's `## Discovered` section and finish the current item first.
- End by announcing "Done: <item>". No diff recap. Flip the `phase:` frontmatter to `verify`.

## Verify phase
Run all four steps in order. Stop at first failure; record it in PLAN.md's `## Last Verify`; flip `phase:` back to `plan`.
1. Build: run the build command from CODEMAP.md.
2. Typecheck: language-level static check (for C++ this is the compile itself).
3. Tests: run tests related to the files you changed, not the whole suite.
4. Structural review of your diff, one line each:
   - Modularity: does the change respect existing boundaries? No new cross-module deps unless planned.
   - Extensibility: is the added code open to extension without further edits?
   - Data-oriented fit: for ECS/game code, are data and behavior kept separate?
   Any "no" → record which one in PLAN.md and return to Plan.
All four pass → check the item in PLAN.md. Flip `phase:` to `code` if items remain, else `plan` for the next task.

## Invariants (all phases)
- Before reading a file in full: first grep/rg or ls to narrow scope. Full reads only when CODEMAP.md and search were insufficient.
- CODEMAP.md is read once per task at bootstrap. Do not re-read unless you detect it is stale (file structure changed).
- PLAN.md is the single source of truth for progress. CODEMAP.md is structure. Conflict → fix the file, then act.
- Output: actions, not narration. No "Let me..." preambles. No diff recaps.
- Uncertainty → verify, don't guess. Running code is ground truth.
