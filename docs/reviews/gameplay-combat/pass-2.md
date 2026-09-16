# Pillar 2 — Structure & Changeability (slice 2: 텍스트 전투 코어, pass 2)

Commit: `9751ab5` — "fix(gameplay): consume FTurnEndRequestedEvent in-frame; add the R3
event-order rule" (author date 2026-09-16 +0900). Branch: `feature/gameplay-combat-text`
(base `a9b149e` = slice 1 `gameplay-foundation`). Write-up date: 2026-09-16.

Scope (`contract.md:39-62`): `src/gameplay/**`, the `src/core/` integration points
(`src/core/Engine.cpp`, `src/core/data/FContentRow.h`, `src/core/InputState.h`, `src/main.cpp`,
`src/debug/DebugOverlay.cpp`), `assets/data/*.json`, `src/gameplay/data/*`,
`tests/changeability/fixtures/gameplay/**`.

Provenance: the findings below were established by a completed read-only review run of `9751ab5`;
this file transcribes them and re-verified every `file:line` against the working tree before
writing. Three wording corrections are marked **verified correction** — a claim the run stated one
way that the source states another. Reviewer of record: read-only pass-2 run; `implements: no`
(the session writing this file did not author `9751ab5`). Every line number below is a line number
**inside `9751ab5`**; the working tree has since gained uncommitted edits to
`src/gameplay/factories/FBattleFactory.cpp` and `src/gameplay/systems/FInitiativeSystem.h`
(+24 lines) that shift them — reproduce with `git show 9751ab5:<path>`.

Harness: **clean on this commit per `pass-1.md:9-27`** — `ok R1 clean / R2 clean / … / R33 clean`,
`changeability bundle: OK (repo)`, `--calibrate` → `OK (calibrate)`. The run recorded there covers
`a9b149e..6b432cd` (`pass-1.md:3`); `9751ab5` adds the R3 event-order pass, calibrated on
`tests/changeability/fixtures/zz_event_order/**` with exactly one expected violation
(`tooling-notes.md:61-90`). The script was not re-run in this session (read-only), so no `OK (repo)`
line is used below as PASS evidence for a rule the script itself lists `NOT CHECKED (manual)`.

> Guided question: when a pass carries a harness result forward, does the record say *which commit*
> that result describes — or does a green line silently migrate onto a commit it never covered?

## 1. Verification of the two fixes carried into `9751ab5` — both CORRECT

**(a) The mangled include in `FBattleOutcomeSystem.h` — CORRECT.**
`9751ab5` previously had one line joining
`#include "gameplay/components/FBattleVictory.h"#include "gameplay/components/FDowned.h"`.
It is now two lines: `src/gameplay/systems/FBattleOutcomeSystem.h:9`
(`#include "gameplay/components/FBattleVictory.h"`) and
`src/gameplay/systems/FBattleOutcomeSystem.h:10`
(`#include "gameplay/components/FDowned.h"`). `FDowned` is now directly declared for its only use,
`src/gameplay/systems/FBattleOutcomeSystem.h:75`
(`registry.view<TTeamTag>(entt::exclude<FDowned>)`), instead of arriving transitively through
`FBattleVictory`'s include graph. No mangled join survives anywhere in the directory
(`grep -c '\.h"#include' src/gameplay/systems/*.h` → 0 hits). The fix is correct: the file
compiles by inspection, and the header now names the component it excludes.

**(b) Frame semantics of `FTurnEndRequestedEvent` — CORRECT.**
The bus clears the frame queues once per frame: `src/core/Engine.cpp:248` (`bus->beginFrame();`),
called before `processInput()`/`update(dt)` in `Engine::run()`. Producers queue the request with
`queueFrame<FTurnEndRequestedEvent>`: `src/gameplay/systems/FPlayerCommandSystem.h:59` and
`:89` (Esc, and the no-skills path), `src/gameplay/systems/FEnemyTurnSystem.h:44` and `:53`
(nothing to do, and after the skill request). The old reader was `FTurnStartSystem`, which runs
*before* both producers, so the request was read a frame late and deleted by the next `beginFrame()`
(recorded in `tooling-notes.md:68-70`). `9751ab5` moves the read into
`src/gameplay/systems/FTurnEndSystem.h:49-56`, registered after every producer:
`src/gameplay/GameplayServices.cpp:83` (`FPlayerCommandSystem`), `:84` (`FEnemyTurnSystem`),
`:88` (`FTurnEndSystem`), inside the walk order the file documents at
`src/gameplay/GameplayServices.cpp:73-79`.

No double-consume and no dropped request, verified against the tree:
`FTurnEndSystem` is the **only** reader of `FTurnEndRequestedEvent` (`grep -rl` over `src/` → the
event header plus the two producers plus `FTurnEndSystem.h`); `FTurnStartSystem.h` no longer touches
the frame queue at all (`grep -c frameEvents src/gameplay/systems/FTurnStartSystem.h` → 0) and uses
its bus only to queue `FTurnStartedEvent` at `FTurnStartSystem.h:82`. The close is one writer
(`FBattleFactory::closeTurn`, `src/gameplay/factories/FBattleFactory.cpp:154`) and one narration
event (`FTurnEndedEvent`, `FTurnEndSystem.h:55` → consumed at
`src/gameplay/systems/FBattleLogSystem.h:46`). The in-loop copy at `FTurnEndSystem.h:49` is
load-bearing and documented at `FTurnEndSystem.h:46-48`: `queueFrame<FTurnEndedEvent>` (line 55)
inserts into the bus's frame map, so iterating the `any_cast` reference would dangle. The invariant
is now stated where a reader needs it (`src/gameplay/events/FBattleEvents.h:17-22`).

> Guided question: when the same fact can be read as either a per-frame event or durable state, does
> the code say which one owns it — or does a registration order have to be memorised for it to work?

## 2. Per-rule verdicts

| Rule | Verdict | Evidence |
| ------ | --------- | ---------- |
| R1 | **FAIL** (3 items) | see items 1-3 below; `docs/guidelines/game-code-changeability.md:87` |
| R3 / R21 | **FAIL** | `src/gameplay/run/FBattleState.h:19-20` + `src/gameplay/systems/FInitiativeSystem.h:65-66` + `src/gameplay/systems/FTurnStartSystem.h:55-56` |
| R11 | **FAIL** | `src/gameplay/events/FBattleEvents.h:29-35` + `src/gameplay/events/FBattleEvents.h:17-20` + `src/gameplay/systems/FTurnEndSystem.h:49-55` |
| R19 | **FAIL** | `src/gameplay/systems/FSkillResolveSystem.h:103,108` + `src/gameplay/data/FSkillContent.cpp:9` + `src/core/data/FContentRow.h:38-39` |
| R23 / R24 / R25 / R26 / R27 | **PARTIAL** | `src/gameplay/rules/FTargetSelection.h:19,43` vs `src/gameplay/events/FBattleEvents.h:57-62` + `src/gameplay/systems/FDamageSystem.h:49` |
| R9 | PASS | `src/gameplay/systems/FTurnEndSystem.h` is one file + `src/gameplay/GameplayServices.cpp:20,88` (1 include, 1 registration) |
| R13 / R14 | PASS | `CMakeLists.txt:15` `GLOB_RECURSE … CONFIGURE_DEPENDS` (0 build edits); no measured hot path exists, so R13 stays a measurement gate, debt in `pass-3.md` |
| R29 / R30 | PASS | no `*Import/*Export/*API` struct and no DLL handoff in `src/`; no two-path duplicate basenames of the `src/core/Logger.h` vs `src/debug/Logger.h` class |
| R31 | PASS | logs sit on state/failure paths with no `#if` guard: `FTurnStartSystem.h:64,70,80`, `FSkillResolveSystem.h:52`, `FDamageSystem.h:44,52,68` |
| R32 | PASS | `ZoneScopedN("F…")` on every system, name matching `name()`: `FTurnEndSystem.h:31/59`, `FTurnStartSystem.h:34/88`, `FInitiativeSystem.h:31/79`, `FGridMoveSystem.h:29/87`, `FPlayerCommandSystem.h:33/93`, `FEnemyTurnSystem.h:28/56`, `FSkillResolveSystem.h:33/46`, `FDamageSystem.h:26/38`, `FBattleOutcomeSystem.h:31/69`, `FBattleLogSystem.h:29/84` |
| R33 | PASS | role comment + markers: `FTurnEndSystem.h:20-28` (`why`, and why the split exists), `FTurnEndSystem.h:46-48`, `FSkillResolveSystem.h:29-30` (`invariant:`), `FTurnStartSystem.h:74-77` (`why` + `fallback:`), `FDamageSystem.h:22-23` (`미결(design §4):`) |
| R2, R4, R5, R6, R7, R8, R12, R15, R16, R17, R18, R20, R22, R28 | PASS / N-A | harness lines quoted in `pass-1.md:10-16`; R28 `N-A` (no save layer), R18 `N-A` (no threads) — debt table in `pass-2` §4 |

### R1 — FAIL, three items

1. **`FTurnEndSystem` is one file with two real jobs.** `FTurnEndSystem.h:29-57` gates on the
   terminal phase (`:42-44`) *and* consumes the end request, closes the turn and publishes the
   narration event (`:49-56`). The type is the only holder of both jobs, and the second job exists
   only because the first reader (`FTurnStartSystem`) lost it in this commit.
2. **`FTurnStartSystem` is slimmer than before but keeps the same kind of pile.**
   `FTurnStartSystem.h:32-89` (91 lines) still opens the turn (`:79`), refills AP (`:76-77`), walks
   and advances the order cursor (`:55-56`), resolves content (`:61-66`), skips downed units (`:57`)
   and narrates (`:80`). The commit removed the end-request branch; it did not reduce what the file
   knows.
3. **The split moved lines rather than responsibilities.** At `6b432cd` the end request was read
   inside `FTurnStartSystem`; at `9751ab5` the same work lives in `FTurnEndSystem` plus a new
   terminal gate. The turn loop grew from two files to three with the same total responsibility and
   a new ordering contract (`GameplayServices.cpp:73-79`) that only registration order keeps true.

**verified correction:** the run described `FTurnEndSystem` as mutating "three component groups".
Against the source it mutates exactly one — `FTurnActive`, removed through
`src/gameplay/factories/FBattleFactory.cpp:154`. Neighbouring counts, for the record:
`FTurnStartSystem` 2 (`FApPool` at `FTurnStartSystem.h:76-77`, `FTurnActive` via
`FBattleFactory.cpp:147`), `FDamageSystem` 2 (`FHealth` `FDamageSystem.h:49`, `FDowned` `:67`), and
the largest system file is `FTurnStartSystem.h` at 91 lines. So the rule's *count* thresholds
(§5 R1④, `game-code-changeability.md:87`) are **not** exceeded; the FAIL is recorded on the
"one file = one responsibility" leg, which is the leg the run's three items actually describe.

### R3 / R21 — FAIL (turn order is a shared mutable array, not an event)

`src/gameplay/run/FBattleState.h:19-20` holds `std::vector<entt::entity> order;` and
`std::size_t cursor{0};` as a battle-scope service. `src/gameplay/systems/FInitiativeSystem.h:65-66`
writes both **in place** on the initiative system's path
(`state->order = std::move(candidates); state->cursor = 0;`) and
`src/gameplay/systems/FTurnStartSystem.h:55-56` consumes them
(`while (state->cursor < state->order.size()) { const entt::entity candidate = state->order[state->cursor++]; … }`).

The actor/holder relationship is therefore not guaranteed total and stable:

- the actor list is rebuilt only at a round boundary (`FInitiativeSystem.h:42-44` returns while the
  cursor has not reached the end), and it is built from a view that excludes downed units
  (`FInitiativeSystem.h:47`);
- going down does **not** prune the vector: `FDamageSystem.h:67` calls
  `FBattleFactory::markDowned` (`FBattleFactory.cpp:161,164` — emplaces `FDowned`, removes
  `FTurnActive`), while the `order` entry survives;
- so the `order` entries are kept consistent with the living units only by the readers' skip check
  (`FTurnStartSystem.h:57`), and the holder of `FTurnActive` is decided by a *different* system
  (`FTurnStartSystem.h:79` → `FBattleFactory.cpp:147`) than the one that wrote the order.
- the handoff itself is state, not an event: `docs/guidelines/game-code-changeability.md:105` (R3)
  forbids systems communicating outside typed events, and the `order` array is exactly such a
  channel between two systems that never publish it.

**Rule-text note:** `game-code-changeability.md:285` scopes R21 to registration sites
("등록은 한 지점, 자기등록 매크로·static 금지"), and per file that leg is clean (harness
`ok R21 clean`, `pass-1.md:12`; single registration site `GameplayServices.cpp:80-89`). The
`FBattleState` claim above is recorded under the id the pass-2 run used; its **R3** leg is the one
the guideline text at `:105` supports.

### R11 — FAIL (the end request is a second, frame-local representation of the turn phase)

`src/gameplay/events/FBattleEvents.h:29-35` is a component/flag-like gate: `FTurnEndRequestedEvent`
is a 4-byte entity handle in the frame map that means "this turn is over", while the same fact is
already carried by the phase tag `FTurnActive` (`FTurnEndSystem.h:54` → `FBattleFactory.cpp:154`).
"End requested" is therefore representable in two places, and the event copy is the fragile one —
`FBattleEvents.h:17-20` states the rule in the code's own words ("the event has to be consumed in
the same frame it is queued, or it must become state with a durable owner (R11)"). The failure is
reachable, not theoretical: the consumers can return before reading —
`FTurnEndSystem.h:39-41` (no battle), `:42-44` (terminal phase) — and the queued request then
disappears at the next `beginFrame()`. The type's own doc claims the opposite guarantee
(`FTurnEndSystem.h:22-28`), which is only true while the registration order holds.

### R19 — FAIL (the AP cost is unconstrained; the pool guard covers only one sign)

`src/gameplay/systems/FSkillResolveSystem.h:103` guards the subtraction
(`if (pool->skill < skill->apCost) { reject(...); return; }`) and `:108` then spends
(`pool->skill -= skill->apCost;`); the move path mirrors it
(`src/gameplay/systems/FGridMoveSystem.h:63` `if (pool->move <= 0)`, `:80` `pool->move -= 1;`).
**verified correction:** the pool therefore cannot go *negative* through a non-negative cost — the
unconstrained input is the cost value. `skill.apCost` is decoded with no lower bound at
`src/gameplay/data/FSkillContent.cpp:9` (`row.integer(kSkillFieldApCost, 1)`) through
`src/core/data/FContentRow.h:38-39` (`static_cast<int>(number(...))`), and the loader validates
presence and type only — `src/core/data/FContentLoader.cpp` contains no `min`/`max`/`negative`/
`clamp` check at all. `ap_cost` is a *required* field (`src/gameplay/data/FSkillContent.h:31`), so
a negative value passes loading, the guard at `FSkillResolveSystem.h:103` is false for
`pool->skill < skill->apCost` with a negative cost, and `:108` **adds** AP. Nothing asserts the
invariant either; the only stated one is the neighbouring claim at
`FSkillResolveSystem.h:29-30` ("a resolution is emitted only after the actor's skill AP was
charged"), which says nothing about the cost's sign. The damage pool is clamped
(`FDamageSystem.h:49` `std::max(0.0, before - std::max(0.0, event.power))`), so HP — unlike skill
AP — cannot go below zero. The test seam leg (R19③) is absent: no test target exists
(`pass-1.md:74-78`, debt row).

### R23 / R24 / R25 / R26 / R27 — PARTIAL (content growth is data-driven for numbers, not for effects)

A new *target shape* is a one-file entry: both selection rules are free functions in one rules file
(`src/gameplay/rules/FTargetSelection.h:19` `livingOpponents`, `:43` `nearestLivingOpponent`), and
the two-branch team check lives inside them (`:24-34`), so a third shape is a new function plus one
caller. A new skill *effect* is not content: the resolution event carries only `power`
(`src/gameplay/events/FBattleEvents.h:57-62`) and the only application of it is
`src/gameplay/systems/FDamageSystem.h:49` — a subtraction on `FHealth`. A heal, buff, shove or
multi-target effect therefore needs a new event field plus a new C++ branch (or a new system file
plus an include and an `addSystem` line, `GameplayServices.cpp:20,80-89`), i.e. code growth per
content item. That asymmetry is the PARTIAL leg, and it is the guideline's
`game-code-changeability.md:297` (R22, 콘텐츠·밸런스는 코드 밖) heading rather than the five ids the
run listed: read against its own heading, each of R23 (`:310`), R24 (`:322`), R25 (`:335`),
R26 (`:345`) and R27 (`:356`) is PASS on this commit (`pass-1.md:12`: `ok R23 clean … R27 clean`).

### R9, R13 / R14, R29 / R30, R31 / R32, R33 — PASS

- **R9 (deletability).** `FTurnEndSystem` is exactly one feature file plus one include
  (`GameplayServices.cpp:20`) and one registration (`:88`); reverting the commit is that deletion,
  restoring the `FTurnStartSystem` read, and no `#ifdef`/flag/dead code anywhere. The same shape
  holds for the other nine systems (`GameplayServices.cpp:80-89`).
- **R13.** No measured hot path was touched, so R13 is a gate, not a verdict: the spans exist
  (`FTurnEndSystem.h:31`, `FInitiativeSystem.h:31`) but carry no before/after numbers; debt goes to
  `pass-3.md` (guideline §3 / `pass-3.md`).
- **R14.** `CMakeLists.txt:15` `file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS …)` — adding
  `FTurnEndSystem.h` needed 0 build-script edits (comment at `CMakeLists.txt:14` states why).
- **R29 / R30.** No `*Import/*Export/*API` struct, no `DLL_*`/`GetProcAddress` handoff exists in
  `src/`, so nothing can be version-less (guideline §1 adoption table records R29 `N-A` + debt);
  R30 finds no duplicated basenames over the 40-line/60%-overlap thresholds — the only shim pair,
  `src/core/Logger.h` vs `src/debug/Logger.h`, is a 3-line forwarder, which the rule exempts.
- **R31.** Logs stay in the code and are level-controlled, never `#if`-guarded:
  `FTurnStartSystem.h:64,70,80`, `FSkillResolveSystem.h:52`, `FDamageSystem.h:44,52,68`,
  `FEnemyTurnSystem.h:43,48`, `FBattleOutcomeSystem.h:55,63`. One gap, reported not failed:
  `FTurnEndSystem.h` is the only new system with **0** `LOG_*` calls (`contract.md:85-87` asked for
  one on the state path); its transition is narrated through `FTurnEndedEvent`
  (`FTurnEndSystem.h:55` → `FBattleLogSystem.h:46`), so the frame is still observable.
- **R32.** Every system carries a span whose name equals `name()` (list in the table above), so a
  profile, a log line and the overlay agree on the system's name. Debug-view wiring
  (`src/debug/DebugOverlay.cpp`, `contract.md:60`) is not exercised in this session — see §3.
- **R33.** Every new file opens with a role comment and carries `why`/`invariant`/`fallback`/
  `미결` markers where they earn their place (`FTurnEndSystem.h:20-28,46-48`,
  `FSkillResolveSystem.h:29-30`, `FTurnStartSystem.h:74-77`, `FDamageSystem.h:22-23`); the harness
  reports `ok R33 clean` (`pass-1.md:14`), and the `미결(` marker is a sanctioned one
  (`tooling-notes.md:44-49`).

> Guided question: is a rule "satisfied" when a detector stops firing, or when the *cost the rule
> was written to protect* — here, adding one more piece of content — actually stays at data-edit size?

## 3. Not verifiable in this session

- **Build and runtime gates are owner-run.** `contract.md:117-120` and `DEV_WORKFLOW.md`
  (Builds Are Human-Run) put `scripts/check-windows.ps1 -Configuration Debug` in the owner's hands;
  agents do not run cmake/ctest here. Build gate: `PENDING (owner-run)`. Compile-dependent rules are
  therefore `NOT-CHECKED`, not PASS: R20③ include resolution (the `FBattleOutcomeSystem.h:9-10` fix
  is verified by inspection, not by a compiler), R1 line/size thresholds (counted, not built),
  R19③ test seam (no test target exists at all), R13 measured numbers, R16 runtime determinism.
- **Carried forward from `pass-1.md:74-78`** and answered or still open: R1 "≥3 component groups"
  → answered above (counts not exceeded; FAIL stands on the responsibility leg), R9 revert-diff →
  estimated from the registration sites, not executed, R13/R14/R19/R21/R31 per-file → answered by
  reading (this section 2), **R32 debug-view wiring → still `NOT-CHECKED`** (`src/debug/` was not
  exercised).
- **Harness re-run** on `9751ab5` itself: not performed here; the R3 event-order pass added by this
  commit is verified from its calibration fixtures (`tooling-notes.md:61-90`), not from a fresh
  `OK (repo)`.
- **Sibling artifact not used as evidence.** `docs/reviews/gameplay-combat/pass-3-correctness.md`
  is another session's untracked file; it is not treated as evidence here, and one of its citations
  (`FPlayerCommandSystem.h:104`) does not resolve — the file is 96 lines.

> Guided question: which rules of R1-R33 can *only* be settled by a human-run build, and does the
> review record mark those `NOT-CHECKED` instead of letting a green harness imply them?

## 4. Change costs of this slice (numbers)

- **add**: a new system = 1 new file + 2 lines (`GameplayServices.cpp:20`-style include, `:88`-style
  registration) + 0 build edits (`CMakeLists.txt:15`); a new numeric content row = `assets/data/*.json`
  only; a new skill *effect* = a new event field plus a C++ branch/file (the PARTIAL row).
- **delete**: `FTurnEndSystem` = 1 file + `GameplayServices.cpp:20,88`; the whole slice = the ~24
  in-scope files (`contract.md:41-52`) plus `src/main.cpp` and `src/debug/DebugOverlay.cpp`.
- **tune**: yes — `assets/data/{units,skills,encounters}.json`, no recompile (`schema_version` in
  all three); the one exception is the *sign* of `ap_cost` (R19).
- **understand**: 10 files for one frame; the walk order at `GameplayServices.cpp:73-79` is the
  mitigation, but initiative → turn-open still forces `FInitiativeSystem.h` → `FBattleState.h` →
  `FTurnStartSystem.h`.

## 5. Top 5 changeability-costly items, with the cheapest restructuring

1. **R3 — turn order lives as a shared mutable array** (`FBattleState.h:19-20`,
   `FInitiativeSystem.h:65-66`, `FTurnStartSystem.h:55-56`). Cheapest fix: keep `order`/`cursor` in
   one `FTurnOrder` service whose only two entry points (`rebuild`, `takeNext`) are in that one
   file, so the two systems stop writing the service directly; or publish an
   `FOrderRebuiltEvent` carrying the vector and delete the cursor from the service.
2. **R1 — the close/open split kept the phase gate in two files**
   (`FTurnEndSystem.h:42-56`, `FTurnStartSystem.h:36-56`). Cheapest fix: make the request durable
   instead of frame-local — a `FTurnEndRequested` tag component written by the requester and
   cleared by the closer — so one system owns "whose turn it is" end to end and the
   registration-order contract (`GameplayServices.cpp:73-79`) stops being load-bearing.
3. **R19 — `ap_cost` has no lower bound** (`FSkillContent.cpp:9`, `FContentRow.h:38-39`,
   `FSkillResolveSystem.h:103,108`). Cheapest fix: one range check at the decode/loader seam (a
   `[,0)`-style bound on the field), plus `pool->skill = std::max(0, pool->skill - skill->apCost)`
   at `FSkillResolveSystem.h:108` and the same clamp at `FGridMoveSystem.h:80`.
4. **R11 — "end requested" is representable twice** (`FBattleEvents.h:29-35` vs `FTurnActive`).
   Cheapest fix: the same tag component as item 2 — the request becomes state with a durable owner
   and the frame-local event disappears, removing the "consumed in the same frame or lost" sentence
   from `FBattleEvents.h:17-20`.
5. **R22 leg of R23-R27 PARTIAL — effects are code, not data**
   (`FBattleEvents.h:57-62`, `FDamageSystem.h:49`). Cheapest fix: add an `effect` key to the
   already schema-driven skill row (`kSkillFields`, `src/gameplay/data/FSkillContent.h:28-35`) and
   dispatch it once where damage is applied, so a new effect is a data row plus one registry
   entry — the shape `FTargetSelection.h:19,43` already demonstrates for target shapes.

> Guided question: for each costly item, is there a restructuring whose diff is *smaller* than the
> next feature that will hit the same wall — and if not, is the item really a finding?

## 6. Status

`build gate: PENDING (owner-run)` — `scripts/check-windows.ps1 -Configuration Debug`
(`contract.md:117-120`). Structure verdict of this pass: **Blocker 0 / High 2 (R3-R21 ordering,
R1 responsibility split) / Medium 3 (R11, R19, R23-R27 PARTIAL) / Low 0**, all Medium+ items
recorded with a concrete restructuring in §5. Judgement per `game-code-changeability.md:485`
(Blocker = 0, High > 0) → **`REWORK`** at the level of the two High items, i.e. the slice may be
retuned and extended by data, but the next change to the turn loop will pay the ordering contract
again.

STATUS: COMPLETE
