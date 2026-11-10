# WP-<id>: <title>

Size: S | L · Base commit: <hash> · Branch: wp/<id>

## 1. Goal [S]
<2–4 lines: what changes for the player, and why.>

## 2. Behaviors [S]
One row per observable behavior. EARS form: `WHEN <trigger> [IF <condition>] THEN <system> SHALL <response>`.
| ID | Behavior | Proving test (`suite::Name`, file) |
|---|---|---|
| F1 | WHEN … THEN … SHALL … | `camp_tests::…` in `tests/features/camp_tests.cpp` |
The same IDs go into `features.json`; the implementer may only flip `passes`.

## 3. Numbers
Every gameplay value this WP introduces: name, value, unit, data file + JSON path. Mark balance-tunable values `knob: [min, max]` so the optimizer can move them later; the value here is the starting point.

## 4. File map [S]
- Create: `<path>` — purpose.
- Modify: `<path>` — which function/section, what changes.
- Exact signatures for every new or changed public function/type.
- Do not touch: `<paths>` (other systems, shared files the WP must leave alone).

## 5. Tasks [S]
Ordered; interface-first; every task leaves the build green and the tests runnable. Each task:
1. **T1 <name>** — files, steps. Done: `<command>` → `<expected result>`.

## 6. Tests
Per behavior ID: fixture, setup, the assertion that captures the design intent (not just "no crash"). Tests are written first and fail before the implementation (red), then pass (green). Existing assertions keep their meaning.

## 7. Decisions
- Resolved: <decision> — because <reason/source>.
- Alternatives considered [L]: <option> — rejected because <concrete reason>.
- Defaults for anything unspecified: <rule>.
- Out of scope: <list>.

## 8. Pitfalls
Known traps that apply (from `.claude/CLAUDE.md` gotchas or prior WPs), each with the safe approach.

## 9. Open questions
Must be empty at handoff.
