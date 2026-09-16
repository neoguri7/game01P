# Contract — gameplay-combat (slice 2: 텍스트 전투 코어)

Branch: `feature/gameplay-combat-text` (base = `feature/gameplay` @ `a9b149e`, which
carries slice 1 / `gameplay-foundation`: data layer + `FGameplayServices`).
Design oracle: `docs/design/game-design.md` — **only 확정 items may be implemented**;
미결 items stay as `// 미결(design §N):` stubs with a same-line `// fallback:` value
(marker convention inherited from slice 1, R4/R33).

## Goal of this slice (사용자 요청 m00657)

"기획을 바탕으로 간단한 텍스트 게임으로 구현" — a playable text battle that walks the
**process** of design §4 (전투) end-to-end: initiative order → AP 2종 → grid range →
skill use → damage → downed → victory/defeat. Art/animation is deliberately replaced by
a text log; the later sprite/animation slice swaps only the presentation layer.

## What changes

1. **Content data** — `assets/data/encounters.json` (new table), plus new rows in
   `assets/data/units.json` (enemy templates) and a `power` field in
   `assets/data/skills.json`. All files keep `schema_version` (R17).
2. **Schema/loader reach** — `src/core/data/FContentRow.h` gains a `numberArray()`
   accessor (the `NumberArray` field type was already declared by the loader; no table
   could read it). `src/core/InputState.h` gains three actions so grid movement, skill
   cycling and target cycling are distinguishable.
3. **New gameplay layers** under `src/gameplay/` (extending the slice-1 layout):
   - `components/` — battle components and exclusive state tags (R2/R11)
   - `events/` — typed battle events (R27)
   - `rules/` — grid distance + deterministic target selection (pure queries)
   - `run/` — battle services in `registry.ctx()` (battle state, battle log, command selection)
   - `factories/` — the only entity-creation site (R5)
   - `systems/` — 8 battle systems (R1/R20/R32)
   - `view/` — text formatting only (no SDL, no ImGui): the swappable presentation seam
4. **State machine** — battle phases as exclusive tag components + transition table in
   `docs/design/battle-state-transitions.md` (R11).
5. **Harness** — `scripts/verify-changeability.sh` + the guideline adoption table cover
   the new `gameplay/` subdirectories instead of skipping them (R7/R26/R32 for gameplay),
   with new calibration fixtures in `tests/changeability/fixtures/gameplay/`.

## In-scope files (R12② — a diff outside this list is a finding)

New: `assets/data/encounters.json`; `src/gameplay/data/FEncounterContent.{h,cpp}`;
`src/gameplay/components/{FGridPosition,FHealth,FApPool,FInitiative,FSkillSet,FUnitRef,
FDisplayName,FTeamPlayer,FTeamEnemy,FDowned,FBattleRound,FTurnActive,FBattleOngoing,
FBattleVictory,FBattleDefeat}.h`; `src/gameplay/events/FBattleEvents.h`;
`src/gameplay/rules/{FGridDistance,FGridOccupancy,FTargetSelection,FTurnActor}.h`;
`src/gameplay/run/{FBattleState,FBattleLog,FCommandSelection}.h`;
`src/gameplay/factories/FBattleFactory.{h,cpp}`;
`src/gameplay/view/FBattleTextView.{h,cpp}`;
`src/gameplay/systems/{FInitiativeSystem,FTurnStartSystem,FGridMoveSystem,
FPlayerCommandSystem,FEnemyTurnSystem,FSkillResolveSystem,FDamageSystem,
FBattleOutcomeSystem,FBattleLogSystem}.h`; `docs/design/battle-state-transitions.md`;
`docs/reviews/gameplay-combat/*`; `tests/changeability/fixtures/gameplay/**`.

Note (naming): the phase tag is `FBattleOngoing`, not `FBattlePlayerPhase` — the acting side is already
carried by `FTeamPlayer`/`FTeamEnemy` plus `FTurnActive`, so a separate player-phase tag would be a second
representation of the same fact (R11).

Modified: `assets/data/units.json`, `assets/data/skills.json`,
`src/core/data/FContentRow.h`, `src/core/InputState.h`, `src/main.cpp`,
`src/debug/DebugOverlay.cpp`, `src/gameplay/GameplayServices.{h,cpp}`,
`src/gameplay/data/FSkillContent.{h,cpp}`, `src/gameplay/data/FContentRegistry.{h,cpp}`,
`scripts/verify-changeability.sh`, `docs/guidelines/game-code-changeability.md`.

## What must NOT change (oracle)

- **Dependency direction**: `core/` ← `ecs/` ← `gameplay/` (R7a). No `gameplay/` include
  from `core/` or `ecs/`. No `Player`/`Enemy` identifier in `core/`/`ecs/` code (R7b) —
  team identity is a gameplay concept, and it is expressed by **tag components**, not by
  a `Player`/`Enemy` enum in core.
- **Platform boundary**: `gameplay/` stays SDL-free and ImGui-free (R26 applied to the
  new layer). Text rendering happens in `src/debug/DebugOverlay.cpp`, which reads the
  strings produced by `gameplay/view/FBattleTextView.*`.
- **Creation** (R5): entities are created only in `src/gameplay/factories/`. Systems
  publish events; they never `create()`.
- **Communication** (R3/R27): systems talk through typed `FEventBus` events only. No
  direct system-to-system calls, no string dispatch.
- **Values** (R4/R22): HP, speed, AP, range, power, grid size, spawn composition and
  spawn cells are data. Code holds one fallback per value, same line, `// fallback:`.
- **미결 stubs** (R33 + design "미결이 남아 있으면 관련 코드를 쓰지 않는다"): the damage
  formula, AP recovery/additional-AP sources, final AP/range/move-distance numbers, the
  tag set size, runes, equipment, 수치노드, drops, and the room/run model are **not**
  implemented. Each touched site carries `// 미결(design §N):`.
- **State** (R11): gameplay state never enters `FBaseState`; battle phases are exclusive
  tag components on the battle entity, and a transition table exists.
- **Observation** (R31/R32): every new system has `ZoneScopedN(name())` and a `LOG_*` on
  its state/failure path; no `#if`-guarded logs. Every new `src/` file has a role comment
  plus `why`/`invariant`/`fallback`/`boundary` markers where they earn their place (R33).
- **Existing behavior**: title/hub/scene flow, engine ability pipeline, the 5 engine
  systems, and slice-1 content loading/validation keep working unchanged.

## Implementation choices that are NOT design decisions (declared so reviews can check them)

- **Input mapping** (UI mapping is not a game-design item; it is a slice-local choice):
  `W/A/S/D` = move the active unit one cell (MoveUp/MoveLeft/MoveDown/MoveRight),
  `Q`/`E` = previous/next skill, `Tab` = next target, `Enter` = use the selected skill on
  the selected target, `Esc` = end the turn.
- **Range metric**: `미결(design §4): 사거리 수치` is a data value; the *metric* (Chebyshev
  = 칸 단위 그리드 거리) is declared here and lives in exactly one function
  (`gameplay/rules/FGridDistance.h`), so switching to Manhattan distance touches one file.
- **Turn advance**: the order is rebuilt per round (speed desc, tie = entity id asc) and
  consumed from a vector held in `FBattleState` — never from container iteration order,
  so determinism does not depend on EnTT storage layout (R16).
- **Enemy behaviour**: an enemy uses the first skill in its `FSkillSet` on its nearest
  living party member. A real monster AI is out of scope; the same event path as the
  player is used, so the AI slice later replaces one system.
- **Pacing**: no delay/animation timers yet — a turn resolves on the frame it is taken.
  Timing values are presentation decisions and are not invented here (R4).

## Non-goals (out of scope)

- No run/dungeon/room/reward/loot/rune/equipment/meta systems (§2, §3, §5, §6, §7).
- No hub, no save/resume, no boss multi-stage objectives (design §4 보스 목표).
- No battle-board rendering, sprites, or animation — the text view is the whole output.
- No rebalance of slice-1 data; HP/speed values stay prototype values.
- No new third-party dependencies.

## Build gate

Builds are owner-run (`DEV_WORKFLOW.md` → Builds Are Human-Run). Review passes record
`build gate: PENDING (owner-run)` with `scripts/check-windows.ps1 -Configuration Debug`.
