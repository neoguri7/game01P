# Pass 3 — Correctness / state-machine adversarial review

- Repo: `D:\repos\game01P`, branch `feature/gameplay-combat-text`, HEAD `9751ab5` (working tree clean).
- Scope: the slice-2 text battle core under `src/gameplay/**` as it stands at `9751ab5`, plus
  `docs/design/battle-state-transitions.md` and `docs/reviews/gameplay-combat/contract.md`.
- Method: READ-ONLY source reading. No cmake/ctest was run (owner-run build gate).
- Verdict: **the known FTurnEndRequestedEvent fix is sound**, but the state machine has two
  reproducible phase/turn-ownership holes and a few low-severity guards missing. None is unrecoverable
  (no crash/UB found); the two MEDIUM findings are observable-desync bugs.

---

## 0. Verification of the already-known fix (turn-end event in-frame)

Mechanism checked end to end:

- `FEventBus::beginFrame()` clears `frameEvents_` (`src/core/events/FEventBus.h:87`), called once per frame
  at the top of the loop before `update()` (`src/core/Engine.cpp:248`).
- Registration order (`src/gameplay/GameplayServices.cpp:78-87`):
  `FInitiative → FTurnStart → FGridMove → FPlayerCommand → FEnemyTurn → FSkillResolve → FDamage →
  FBattleOutcome → FTurnEnd → FBattleLog`.
- Producers of `FTurnEndRequestedEvent` are `FPlayerCommandSystem` (`FPlayerCommandSystem.h:104`) and
  `FEnemyTurnSystem` (`FEnemyTurnSystem.h:54`), both before `FTurnEndSystem`. `FTurnEndSystem` reads the
  queue in the same frame (`FTurnEndSystem.h:52-63`) and calls `closeTurn` (`FTurnEndSystem.h:59`).

Result: the request is no longer dropped; `FTurnActive` is cleared and `FTurnEndedEvent` is queued the same
frame. No double-consume (no other reader consumes the request; `FBattleLogSystem` narrates
`FTurnEndedEvent`, not the request), and `FTurnStartSystem` now only opens
(`FTurnStartSystem.h:79`). **Fix is sound.** Details of the residual holes in §§F1-F3.

---

## F1 — MEDIUM — the round is rebuilt one turn early, while the last actor still holds `FTurnActive`

- File:line: `src/gameplay/systems/FInitiativeSystem.h:42` (boundary test) + `:55-66` (rebuild/reset);
  interacting with `src/gameplay/systems/FTurnStartSystem.h:56` (`state->order[state->cursor++]`).
- Guard that fails: `FInitiativeSystem` treats `state->cursor >= state->order.size()` as the round boundary
  but **never checks `activeUnit(registry)`**. `FTurnStartSystem` increments `cursor` past the last entry at
  the moment it *opens* the last unit's turn ("`candidate = state->order[state->cursor++]`",
  `FTurnStartSystem.h:56`), so `cursor == size` while that unit is still mid-turn.

Trigger sequence (encounter whose lowest-speed unit is a player unit — e.g. the existing data with the
enemy faster than the last party member):

1. Round 1 rebuilds order `[A, B, C]`, `cursor=0`, `FBattleRound=1`.
2. A and B act; each closes in its own frame; `cursor` advances per open.
3. Frame M: `FInitiativeSystem.h:42` sees `cursor(2) < 3` → returns; `FTurnStartSystem` opens C,
   `cursor=3`, `FTurnActive{C}`, `FTurnStartedEvent`. C is a **player**, so it holds the turn waiting for input.
4. Frame M+1: `FInitiativeSystem.h:42` sees `cursor(3) < size(3)` false → **rebuilds**: `order=[A,B,C]`,
   `cursor=0`, `FBattleRound → 2`, queues `FRoundStartedEvent{2}`, while `activeUnit == C` (still round 1's turn).

Expected: the "라운드 2" header and `FBattleRound=2` only when C's turn has closed.
Actual: the header/counter advance one turn early; `FTurnStartSystem.h:46` then returns because
`activeUnit != null`, so C keeps playing "round 1" under a "round 2" banner.
Also `FBattleLogSystem::orderText` prints from `state->cursor` (`FBattleLogSystem.h:112-121`), now reset to 0,
so the predicted order shown *during C's own action* lists the next round as if upcoming.

Observable symptom: the round counter/narration is off by one turn whenever the last unit of a round is a
player. Not cosmically fatal but it is a wrong state transition, and it breaks the design doc's claim that
`FBattleRound` increments "each time the order is rebuilt (0 → 1 on the first round)" as a round boundary.

Minimal fix: before the rebuild, require no open turn —
`if (activeUnit(registry) != entt::null) return;` at `FInitiativeSystem.h:42` (add
`#include "gameplay/rules/FTurnActor.h"`). `FTurnEndSystem` has already cleared the tag by end of the prior
frame, so the rebuild happens on the frame after the last actor closes. (Alternative: do not advance `cursor`
past the final slot until the turn closes, but the guard is smaller and keeps `FTurnStartSystem` a pure opener.)

---

## F2 — MEDIUM — terminal phase leaks `FTurnActive`; `FGridMoveSystem` keeps mutating and logging after victory

- File:line: `src/gameplay/factories/FBattleFactory.cpp:167-181` (`endBattleAsVictory`/`endBattleAsDefeat`
  only touch the battle entity), `src/gameplay/systems/FTurnEndSystem.h:42` (terminal early-return drops the
  close), `src/gameplay/systems/FGridMoveSystem.h:37-83` (no terminal guard).

Trigger sequence (victory variant — reachable with the shipped data):

1. Player P is the active unit (`FTurnActive{P}`). P presses Confirm to use its skill on the last living enemy E.
2. Same frame: `FSkillResolveSystem` resolves, `FDamageSystem` drops E to 0 and `markDowned` runs
   (`FDamageSystem.h:61`). `FBattleOutcomeSystem` sees `livingEnemies == 0` → `endBattleAsVictory` +
   `FBattleEndedEvent{true}` (`FBattleOutcomeSystem.h:53-58`). `FBattleLogSystem` prints "전투 승리".
3. `FTurnEndSystem` runs next and returns at `FTurnEndSystem.h:42` because the battle now has
   `FBattleVictory`; nothing clears `FTurnActive{P}` (P never queued an end request — it used a skill).
4. Frame M+1 (and onward): `FInitiative`/`FTurnStart`/`FBattleOutcome` all early-return on the terminal tag,
   but `FGridMoveSystem` has no terminal check. `activePlayerUnit(registry)` still returns P
   (`FGridMoveSystem.h:37`), so pressing W/A/S/D **moves P**, decrements its move AP, mutates the grid and
   queues `FUnitMovedEvent`, which `FBattleLogSystem` appends ("P 이동 → ...") **after "전투 승리"**.

Expected: the battle is terminal — no unit state, grid position, or narration output changes.
Actual: the still-tagged active unit remains commandable for movement until its move AP is exhausted; the log
grows after the outcome line; `FTurnActive` persists on a dead phase.

Defeat variant is milder but shows the same root: the enemy held the turn in the killing frame; `FTurnEnd`
skips, so `FTurnActive{enemy}` also leaks (see F3).

Root cause: the terminal transition is a battle-entity-only structural change; no turn owner clears the unit
tag, and the acting systems (`FGridMoveSystem`, `FPlayerCommandSystem`, `FEnemyTurnSystem`) gate only on
`FTurnActive` + team, not on the battle phase.

Minimal fix (pick one; the first is the single-writer fix R5 prefers):

- In `FBattleFactory::endBattleAsVictory`/`endBattleAsDefeat`, clear `FTurnActive` from every unit
  (`for (auto e : registry.view<FTurnActive>()) registry.remove<FTurnActive>(e);`) before/after attaching the
  phase tag; or
- Add `if (battle is terminal) return;` to `FGridMoveSystem` (and keep it in the other acting systems).

---

## F3 — LOW — after defeat the enemy turn never closes; `FEnemyTurnSystem` re-fires its "nothing to do" warning every frame

- File:line: `src/gameplay/systems/FEnemyTurnSystem.h:37-55`; terminal skip at `FTurnEndSystem.h:42`.
- Trigger: enemy E is active and lands the killing blow on the last party member. Same frame `FBattleOutcome`
  sets `FBattleDefeat`; `FTurnEnd` skips E's queued end request (`FEnemyTurnSystem.h:54`). E keeps
  `FTurnActive`.
- Next frames: `FEnemyTurnSystem.h:37` (`activeEnemyUnit`) still returns E; `livingOpponents` is now empty so
  `target == entt::null`, and the system logs `LOG_WARN("enemy turn: entity {} has nothing to do; its turn is
  closed.")` and re-queues `FTurnEndRequestedEvent` every frame, which `FTurnEnd` discards (terminal).
- Symptom: unbounded per-frame warning log spam and a permanently "active" enemy in a lost battle. No state
  mutation beyond logging. Folded into F2's fix.

---

## F4 — LOW — content validation does not bound `apCost`/`maxHealth`/`speed`; negative values invert or bypass rules

- File:line: `src/gameplay/data/FSkillContent.cpp:9` (`apCost = row.integer(..., 1)`), `:13` (`range`), `:16`
  (`power`); validation in `src/gameplay/data/FContentRegistry.cpp` (`load`) checks cross-table references,
  non-empty sides, cell-count and positive grid size, but **not numeric sign**.
- Cases:
  - `apCost < 0`: `FSkillResolveSystem.h:103` (`pool->skill < skill->apCost`) passes and
    `FSkillResolveSystem.h:108` (`pool->skill -= skill->apCost`) **increases** the pool — a negative-cost
    skill grants AP. Unbounded AP, but not a crash.
  - `maxHealth <= 0`: `spawnUnit` emplaces `FHealth{maxHealth, maxHealth}` (`FBattleFactory.cpp`) with a
    non-positive current; the unit is not `FDowned`, so `countLiving` (`FBattleOutcomeSystem.h:73-81`) counts
    it as living, `FInitiativeSystem` gives it turns, and it can only ever be downed once it is damaged
    (`FDamageSystem.h:61`). An encounter can therefore stall with an un-killable-yet-dead-looking unit.
  - `speed` sign only changes ordering (harmless).
- Observable symptom: requires malformed content (not reachable from the shipped JSON), so LOW; but the
  contract's "values are data (R4/R22)" implies data must be validated. Minimal fix: assert/clamp
  `apCost >= 0`, `maxHealth > 0`, `range >= 0`, `power >= 0` in `decodeSkill`/`decodeUnit` or the
  `FContentRegistry::load` cross-checks, returning `std::nullopt` on violation.

---

## F5 — LOW — resolved-but-dropped damage silently burns skill AP

- File:line: `src/gameplay/systems/FSkillResolveSystem.h:63-108` (charges AP before emitting
  `FSkillResolvedEvent`) vs `src/gameplay/systems/FDamageSystem.h:47-50` (drops the resolution if the target
  has no `FHealth`).
- `FSkillResolveSystem` verifies the target is valid, alive, has `FGridPosition`, is in range and the actor
  has AP — but not that the target has `FHealth`. If it does not, `FDamageSystem` logs
  `LOG_ERROR("damage: target entity {} has no FHealth; the resolution is dropped.")` and returns; the skill AP
  was already spent (`FSkillResolveSystem.h:108`) and no `FSkillRejectedEvent` reaches the player.
- Reachability: normally every spawned unit has `FHealth`, so LOW; it is a defense-in-depth gap. Minimal fix:
  add an `FHealth` presence check to the reject path in `FSkillResolveSystem::resolveOne` (before
  `pool->skill -= ...`) so the AP is not charged when the hit cannot land.

---

## Areas checked and found safe (with the mechanism that makes them safe)

- **Cross-frame event loss / consumed twice.** `beginFrame()` clears every frame and every consumer is
  registered after its producer(s); no two systems consume the same event (`FSkillResolvedEvent` →
  `FDamageSystem` only; `FSkillRequestedEvent` → `FSkillResolveSystem` only; `FTurnEndRequestedEvent` →
  `FTurnEndSystem` only). SAFE.
- **Stale actor on a queued request (Q1).** `FSkillRequestedEvent` is consumed in the producer's own frame
  (`FSkillResolveSystem` after `FPlayerCommand`/`FEnemyTurn`), and the resolver re-checks
  `activeUnit(registry) != request.actor` (`FSkillResolveSystem.h:63`); `markDowned` removes `FTurnActive`
  (`FBattleFactory.cpp:164`), so a downed actor no longer equals `activeUnit`. SAFE — the only per-frame
  writer between producer and resolver is the other side's turn system, which no-ops because only one unit
  can hold `FTurnActive`.
- **Target dies / is displaced / missing skill row / AP over budget (Q3).** All re-validated at resolution:
  valid+not-downed (`FSkillResolveSystem.h:67`), row present (`:72`), in `FSkillSet` (`:78-82`), range
  recomputed (`:96-100`), AP (`:103`). `FCommandSelection` stores indices, not ids, and re-clamps each frame
  (`FPlayerCommandSystem.h:74-75`). SAFE.
- **Simultaneous wipe / coexisting terminal tags (Q2).** Only one side can act per frame (unique
  `activeUnit`), so both sides cannot be damaged in the same frame in this slice; even if both reach 0,
  `FBattleOutcomeSystem` returns early whenever either tag is present (`FBattleOutcomeSystem.h:39`) and
  `livingEnemies == 0` wins (`:53`), so only `FBattleVictory` is attached. `endBattleAsVictory/Defeat` remove
  `FBattleOngoing` first (`FBattleFactory.cpp:171-172,179-180`) and are the sole writers. SAFE — mutual wipe
  resolves to victory; the two tags cannot coexist.
- **End event once vs per frame.** Guaranteed by the terminal early-return (`FBattleOutcomeSystem.h:39`):
  `FBattleEndedEvent` is queued exactly once, in the terminating frame. SAFE.
- **`FBattleLog` trim (Q5).** `FBattleLog::push` (`src/gameplay/run/FBattleLog.h:19-24`) erases
  `size-capacity+1` from the front when at capacity, i.e. exactly one element at capacity, then pushes —
  net size stays `capacity`; the `capacity > 0` guard prevents a zero/negative erase; `push(std::string)`
  takes by value, so no dangling reference to an erased element. No reset path exists. SAFE.
- **Turn order: added/removed unit, ties, dangling order entries (Q4).** No system creates or destroys a unit
  mid-battle (only `spawnUnit` in `FBattleFactory` at build); `FInitiativeSystem` uses a total order
  (speed desc, then `left < right` on the entity value = entity-id/spawn order, `FInitiativeSystem.h:56-62`),
  and `FTurnStartSystem` skips destroyed (`!registry.valid`) or `FDowned` candidates
  (`FTurnStartSystem.h:57-60`) — so a stale/duplicate order entry can never be acted on. SAFE. (The one
  ordering defect is the *timing* of the rebuild, F1.)
- **Frame with nobody holding `FTurnActive`.** After `FTurnEndSystem` clears the tag at end of frame M, the
  next frame's `FInitiative` and `FTurnStart` are the first two systems and `FTurnStart` opens before any
  `activeUnit` reader (`FGridMove`/`FPlayerCommand`/`FEnemyTurn`/`FSkillResolve`); `FBattleLog` after
  `FTurnEnd` does not read `activeUnit`. No system assumes a holder in the gap. SAFE.
- **Event-bus reference invalidation during consumption.** `frameEvents<T>()` returns a `const vector<T>&`
  into the map node (`src/core/events/FEventBus.h:80-85`). `FEventBus` stores `std::any` by value in an
  `unordered_map` node; `queueFrame` of a *different* type only inserts a node (rehash keeps node addresses
  and references valid). No consumer queues the same type it is iterating over (Resolve emits
  `FSkillResolved`/`Rejected`, Damage emits `UnitDamaged`/`UnitDowned`, TurnEnd emits `TurnEnded`). SAFE —
  `FTurnEndSystem`'s defensive copy (`FTurnEndSystem.h:51`) is harmless but its stated rehash rationale is
  technically unnecessary.
- **`FBattleRound` double-increment.** After a rebuild `cursor` resets to 0 (`FInitiativeSystem.h:66`), so
  the boundary test cannot fire twice for one cycle. SAFE (the only defect is the premature boundary, F1).

---

## Q6 — 미결 (undecided) items: interpretations silently chosen

All 미결 sites are single-writer and marked `// 미결(design §N):` + `// fallback:`, so each is cheap to
change. The interpretations picked by the implementation:

- **AP recovery**: turn start refills `move`/`skill` from content and AP otherwise only decreases
  (`FTurnStartSystem.h:73-75`). One site; changing recovery means editing those two lines. SAFE to change.
- **Damage formula**: raw `power` is the damage, floored at 0 (`FDamageSystem.h:56`). One site. SAFE.
- **Monster AI**: first skill in `FSkillSet` on the nearest living opponent
  (`FEnemyTurnSystem.h:44-51`). One producer system. SAFE to replace.
- **Range metric**: Chebyshev, declared in `contract.md` as a non-design implementation choice and lived in
  exactly one function (`FGridDistance.h`). SAFE.
- **Downed units block cells**: `FGridOccupancy.h` currently blocks any occupied cell (including downed).
  One function. SAFE to change.
- **Starting encounter**: first row of `encounters.rows` (`GameplayServices.cpp`). 미결(§2/§3); one site.
  SAFE.
- **Mutual wipe = victory**: this is *documented* as a decision in `battle-state-transitions.md` §1 and
  encoded at `FBattleOutcomeSystem.h:53`; a single branch. Changing it later is a one-line reorder. SAFE.

No 미결 site leaked into a second file or into a component contract, so none of these choices is load-bearing
for a later change.

---

## Latent hazard (not reachable in this slice, recorded for the next one)

Because the terminal transition does not clear `FTurnActive` (F2) and does not destroy units, calling
`FBattleFactory::buildBattle` a second time against the same registry (e.g. a future "retry battle") would
leave the old battle's units and their `FTurnActive` tags in the registry; a rebuilt order would then include
them and `activeUnit` could return a stale unit. There is currently no restart path (only
`FGameplayServices::Initialize` builds once), so this is latent, not a live bug. The F2 fix applied below clears
`FTurnActive` on the terminal transition, which closes the stale-holder part; a second `buildBattle` would still
duplicate units until a restart path destroys them.

---

## Fix round (applied on top of `9751ab5`)

All five findings were fixed in one round; the Mediums first because they are wrong state transitions, the Lows
because each is a small guard. Fix sites:

- **F1** — `src/gameplay/systems/FInitiativeSystem.h:48-54`: the rebuild now requires
  `activeUnit(registry) == entt::null`, so the round boundary is the holder releasing the turn, not
  `cursor` reaching the end (`#include "gameplay/rules/FTurnActor.h"` added).
- **F2 / F3 (same root)** — `src/gameplay/factories/FBattleFactory.cpp`: a new file-local
  `releaseTurnHolder(entt::registry&)` clears every `FTurnActive`, and both `endBattleAsVictory` and
  `endBattleAsDefeat` call it before attaching the phase tag. The factory stays the single writer of turn/phase
  structure (R5), so no acting system needed its own terminal check, and the defeated enemy no longer re-warns
  every frame.
- **F4** — `src/gameplay/data/FContentRegistry.cpp`: numeric range checks added next to the existing cross-table
  checks — skills reject `ap_cost < 0`, `range < 0`, `power < 0`; units reject `max_health <= 0`, `speed <= 0`,
  `move_ap < 0`, `skill_ap < 0` — each with `LOG_ERROR` and `return std::nullopt`, so a malformed row fails the
  boot instead of inverting a rule at runtime. This also answers the pass-2 R19 finding: the unconstrained input
  was the *cost value*, not the pool.
- **F5** — `src/gameplay/systems/FSkillResolveSystem.h:69-75`: the reject path now requires the target to have
  `FHealth` *before* `pool->skill -= skill->apCost`, so a hit that `FDamageSystem` would drop can no longer burn
  the actor's AP silently.

Evidence: `scripts/verify-changeability.sh` → `changeability bundle: OK (repo)` after the round. Build gate is
still owner-run and PENDING.

Deliberately NOT changed here: the two High items in `pass-2.md` §5 (durable turn-end state instead of a
frame-local event; one owner for the turn order). Both change the turn loop's contract, so they are proposed
rather than applied inside a correctness fix round.

Guided question: *when a review round fixes the logic gaps it found, do the structural findings from the same
round get silently deferred — and if so, is the deferral written down?*

---

## Build gate

Review pass build gate: **PENDING (owner-run)** per `contract.md` §"Build gate" / `DEV_WORKFLOW.md`. No
cmake/ctest was executed. All findings above are from source reading; claims that would depend on compilation
are marked NOT-CHECKED, and none of the findings depends on a build (they are logic/guard gaps).

STATUS: COMPLETE
