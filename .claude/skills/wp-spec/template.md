# WP-<id>: <title>

Size: S | L · Player-facing: yes | no · Certainty: settled | uncertain · Base commit: <hash> · Branch: wp/<id>

## 1. Goal [S]
<2–4 lines: what changes for the player, and why.>
- Pillar served: <pillar from docs/design/pillars.md>.
- Decisions it rests on: <lines from docs/design/decisions.md>, or "none".

## 2. Player experience [player-facing]
- **Decision added or changed**: the options the player chooses between and the trade-off of each. No option should beat the others in every situation; say which situations favour which.
- **What the player sees before committing**: every number, preview, or telegraph the choice needs, and where it appears on screen.
- **Playtest hypothesis**: expect `<what the player will do/feel>` · observe `<signal in a playtest or run log>` · kill-if `<result that sends this back to redesign>`.

## 3. Behaviors [S]
One row per observable behavior. EARS form: `WHEN <trigger> [IF <condition>] THEN <system> SHALL <response>`.
| ID | Behavior | Proving test |
|---|---|---|
| F1 | WHEN … THEN … SHALL … | `camp_tests::CaseName` |
Proving-test grammar: `<ctest test name>::<Case>` (a C++ feature-test case registered as `{"<Case>", …}` in `tests/features/<ctest test name>.cpp`), `ctest::<ctest test name>` (a whole ctest entry), or `node:<file>::<test name>` (a `node:test` test). The same IDs go into `features.json` (`{"wp", "files", "allowTimeoutChange", "allowCheckDrop", "features": [{"id", "behavior", "test", "passes": false}]}`); the gate script, not the implementer, derives `passes`.

## 4. Numbers
Every gameplay value this WP introduces: name, value, unit, data file + JSON path. Mark balance-tunable values `knob: [min, max]` and name the balance metric each knob moves. New modifiers name their stage in `docs/design/combat-math.md`.

## 5. File map [S]
- Create: `<path>` — purpose.
- Modify: `<path>` — which function/section, what changes.
- Exact signatures for every new or changed public function/type.
- Do not touch: `<paths>` (other systems, shared files the WP must leave alone).
Mirror the create/modify paths in `features.json` → `"files": [...]`; the gate rejects changes outside it.

## 6. Tasks [S]
Ordered; interface-first; every task leaves the build green and the tests runnable. Each task:
1. **T1 <name>** — files, steps. Done: `<command>` → `<expected result>`.

## 7. Tests
Per behavior ID: fixture, setup, the assertion that captures the design intent (not just "no crash"). Tests are committed first and fail before the implementation (red), then pass (green). Existing assertions keep their meaning; timeouts stay unchanged unless this section says otherwise.

## 8. Decisions
- Resolved: <decision> — because <reason/source>.
- Alternatives considered [L]: <option> — rejected because <concrete reason>.
- Defaults for anything unspecified: <rule>.
- Out of scope: <list>.

## 9. Pitfalls
Known traps that apply (from `.claude/CLAUDE.md` gotchas or prior WPs), each with the safe approach.

## 10. Open questions
Implementation questions: must be empty at handoff. Design unknowns that only play can answer belong in §2's playtest hypothesis, not here.
