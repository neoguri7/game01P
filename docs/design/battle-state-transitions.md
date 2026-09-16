# Battle State Transitions (slice 2 — 텍스트 전투 코어)

R11 requires gameplay state to be **exclusive tag components** with a transition table that lives outside the
code. This document is that table for `src/gameplay/`. It is written against the implementation as of
`feature/gameplay-combat-text` and is the thing to update when a phase is added or removed.

Design source: `docs/design/game-design.md` §4 (전투, 확정). 미결 items stay as `// 미결(design §N):` stubs in
code (damage formula, AP recovery, final AP/range numbers, monster AI, room/run model).

## 1. Battle phases — on the battle entity (`FBattleState::battle`)

Exactly one phase tag is present at any time.

| From | To | Trigger | Decision (writer of the entity change) |
| --- | --- | --- | --- |
| (none) | `FBattleOngoing` + `FBattleRound{0}` | `FBattleFactory::buildBattle` spawned every unit successfully | `FBattleFactory::buildBattle` |
| `FBattleOngoing` | `FBattleVictory` | no living `FTeamEnemy` unit remains (`FBattleOutcomeSystem`) | `FBattleFactory::endBattleAsVictory` |
| `FBattleOngoing` | `FBattleDefeat` | no living `FTeamPlayer` unit remains (`FBattleOutcomeSystem`) | `FBattleFactory::endBattleAsDefeat` |
| `FBattleVictory` | — | terminal; a new battle is a new entity, never a revived one | — |
| `FBattleDefeat` | — | terminal (design §2: 파티 전멸 = 런 종료 — the run model itself is not built yet) | — |

Rules that follow from the table:

- The phase tag is removed from the battle only by leaving `FBattleOngoing` (`Registry::remove<FBattleOngoing>`),
  so two phases can never coexist.
- `FBattleRound{value}` is a counter, not a phase: it increments in `FInitiativeSystem` each time the order is
  rebuilt (0 → 1 on the first round).
- Mutual wipe resolves to **victory** ("no living enemies" is checked first): the run's investment is the party,
  so a draw must not be reported as a loss.

## 2. Unit states — on a unit entity

| From | To | Trigger | Decision (writer of the entity change) |
| --- | --- | --- | --- |
| (none) | `FTurnActive` | `FTurnStartSystem` walks `FBattleState::order` and refills AP from content | `FBattleFactory::openTurn` |
| `FTurnActive` | (none) | `FTurnEndRequestedEvent` consumed at the start of the next `FTurnStartSystem` pass (player Esc / enemy finished) | `FBattleFactory::closeTurn` |
| `FTurnActive` | (none) | the active unit reaches HP 0 and is marked `FDowned` | `FBattleFactory::markDowned` |
| (none) | `FDowned` | `FHealth::current` reaches 0 (`FDamageSystem`) | `FBattleFactory::markDowned` |
| `FDowned` | — | not reachable in this slice: design §4 확정 — 캐릭터 부상·사망 없음, and a battle never returns a downed unit to play | — |

Every row's entity change is performed by `src/gameplay/factories/FBattleFactory.{h,cpp}`, not by the system that
notices the condition: a system decides *when*, the factory decides *what the entity becomes*. That is the single
place to read to answer "which components can a live entity gain or lose?" (R5), and it is why the table above can
list a decision per row at all.

Team identity is a tag as well (`FTeamPlayer` / `FTeamEnemy`) and is **set once at spawn** by the factory; no
system moves a unit between sides, so it is not a transition.

AP has no state machine of its own: `FApPool{move, skill}` is refilled from the unit's content row when the turn
opens and only ever decreases inside a turn (미결(design §4): AP 회복·추가 AP 부여 수단 — when one is designed, it
becomes a row in this table).

## 3. What is deliberately NOT state here

- **Selection** (`skillIndex`, `targetIndex`) lives in the `FCommandSelection` context service, not on the unit:
  it is between-frames UI state, and putting it on the entity would let it survive a turn boundary (R12).
- **Order/cursor** (`FBattleState::order`, `cursor`) is a prediction, not entity state: it is rebuilt from a total
  order (speed, then spawn order) every round, so it can never dangle on a destroyed entity (R16/R11).
