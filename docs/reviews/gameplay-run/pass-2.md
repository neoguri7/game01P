# Pillar 2 — Structure (slice 3, independent pass)

Range: `37daea8..79800c6` on `feature/gameplay-dungeon-run` (fix round applied after the findings below).
Pass owner: `reviewer` delegate (`del_mu4b4alg_hnz8`), read-only, against `docs/guidelines/game-code-changeability.md`.
Harness at review time: `changeability bundle: OK (repo)` — all R-blocks clean, 27 pre-existing baselined hits.

Disposition column: **FIXED** = changed in this slice, **ACCEPTED** = kept with the recorded rationale.

## Findings and dispositions

| # | Sev | Finding | Disposition |
| --- | --- | --- | --- |
| 1 | Blocker | `src/gameplay/systems/FSkillResolveSystem.h:103` declared `const int distance` *inside* the `withinSkillRange` rejection branch, but the success `LOG_INFO` at `:122` still printed `distance` → `error: 'distance' was not declared in this scope`. The harness cannot see this (R20 only resolves includes), so "harness OK" did not mean "compiles". | **FIXED** — `distance` is computed once before the branch (`FSkillResolveSystem.h:100-108`); the rejection and the success log both read it, and `withinSkillRange` stays the single range rule. |
| 2 | High | Deletion test (R9): removing the behaviour feature (`behaviors.json`, `rules/FBehaviorTree.h`, the decision half of `FEnemyTurnSystem.h`) also requires touching `FBehaviorContent.{h,cpp}`, `FContentRegistry.{h,cpp}`, the `behavior` field in `FUnitContent.{h,cpp}`, `FBattleEvents.h` (`FBehaviorDecidedEvent`), `FBattleLogSystem.h` (narrator) and `units.json` — ~7 non-feature files. | **ACCEPTED with rationale** — design §2 fixes the mapping `units.json behavior: [] → behaviors.json rows`, the rule/executor split, and the "decided → did" narration, so a revert must remove the design's own vocabulary from the shared tables. The full revert set is recorded in `verdict.md` so a future deletion is a checklist, not a discovery. |
| 3 | Medium | `assets/data/behaviors.json` — `proto_retreat` / `proto_wait_outnumbered` referenced by no `units.json` row, so `move_away_from_nearest_opponent`, `wait`-as-action and `opponent_count_at_or_above` were never reached in a live run. Ambiguous between "sample data" and "dead content". | **FIXED (documentation, no balance change)** — the file `_comment` now states which rows are vocabulary-coverage-only and which rows are live (`proto_grunt_*` / `proto_brute_*`). Wiring a count-based rule into the two existing units would either never fire (party = 2) or make a monster a permanent bystander, so live-wiring was rejected on balance grounds, not laziness. |
| 4 | Medium | R19 — when a matched rule chose `use_first_skill` and no opponent was in range, the executor only `LOG_WARN`ed: the player read "행동: 'proto_grunt_engage' → 첫 스킬" with no line saying nothing happened. | **FIXED** — the executor now queues `FSkillRejectedEvent{unit, skill->id, "사거리 안에 상대가 없다"}`, reusing the refusal channel the battle log already renders ("사용 불가: … (사거리 안에 상대가 없다)") instead of inventing an event (R12/R19). |
| 5 | Medium | R33 — `src/gameplay/GameplayServices.cpp:86` still described `FEnemyTurnSystem` as `미결(design §4) 몬스터 AI 자리` after this slice replaced the hardcoded AI. | **FIXED** — the line now says the choice comes from `behaviors.json` + `rules/FBehaviorTree.h` (design §2). A second stale comment of the same kind was found in `rules/FTargetSelection.h` ("미결(design §4) 몬스터 AI share this rule") and fixed in the same round. |
| 6 | Low | R4/R12 — `run.seed = 0` in `GameplayServices.cpp:68` duplicated the `FRunState` default (`run/FRunState.h:26`), putting one decision in two places. | **FIXED** — the assignment is gone; `FRunState`'s documented default keeps the design §1 seed-0 decision in one place. |
| 7 | Low | R12 borderline — `nearestLivingOpponent` and `nearestLivingOpponentWithinRange` repeated the same `best`/`bestDistance` tie loop. | **FIXED** — both now call `detail::nearestLivingOpponentWhere(registry, self, acceptPosition)`; the range query passes `withinSkillRange`, so the range rule and the tie rule each stay single-sourced. |
| 8 | Low | Adding one `then` action is a 4-code-file edit (enum, token table, executor switch, narrator switch); suggestion was to put the player-facing text in `behaviors.json`. | **ACCEPTED** — R8 counts 2 files for `EBehaviorAction` (under the FAIL threshold), and the narrator owning the phrasing is the deliberate R27 split (event carries data, presentation phrases it). Moving text into data would put presentation strings into a gameplay content table. |

## Architecture audit (as recorded by the pass)

| Area | Status | Evidence |
| --- | --- | --- |
| Layer direction `core/ <- ecs/ <- gameplay/` | OK | no `gameplay/`/`states/` include under `src/core/`, `src/ecs/` |
| One type per header (basename == type) | OK | `FRunProgressionSystem.h`, `FRunFactory.h`, `FBehaviorContent.h`, `FDungeonContent.h` |
| Single registration per new system (R21) | OK | `GameplayServices.cpp` is the only `FRunProgressionSystem` site |
| Entity create/destroy only in factories (R5) | OK | only `FRunFactory.cpp::spawnRoom` + `FBattleFactory.cpp::buildBattle/destroyBattle`; systems touch no `create`/`destroy`/`emplace` |
| Movement legality single site (R12) | OK | `rules/FGridStep.h::applyStep`, called from `FGridMoveSystem.h` and `FEnemyTurnSystem.h` |
| Skill range single site (R12) | OK | `rules/FTargetSelection.h::withinSkillRange`, used by `FSkillResolveSystem.h` and the AI fact |
| Behaviour choice single site (R12) | OK | `rules/FBehaviorTree.h::firstMatchingBehavior` |
| Content token tables single site (R12) | OK | `data/FBehaviorContent.cpp`, `data/FDungeonContent.cpp` |
| Presentation boundary (R26) | OK | the diff touches no `src/debug/` file; the run header stays in `view/FBattleTextView.cpp` |
| R16 determinism | OK | fixed seed, sorted opponent list, order-preserving behaviour list, no `unordered_*` iteration in gameplay paths |
| R22 content is data | OK | thresholds, room counts, grid sizes all in assets; the only in-code number is the design-fixed run seed |
| Build integrity | **ISSUE FIXED** | finding #1 |

## Gate after the fix round

```text
$ bash scripts/verify-changeability.sh
changeability bundle: OK (repo)
$ node -e JSON.parse(...)  # all four asset files
assets/data/behaviors.json OK / dungeons.json OK / units.json OK / encounters.json OK
```

Compilation itself remains owner-run (`DEV_WORKFLOW.md` §"Builds Are Human-Run"): finding #1 is the reason this
document states plainly that a green harness is **not** compile evidence, and it is why the fix round is committed
as its own commit rather than folded into `79800c6`.
