# Pillar 1 — Correctness & Exceptions (slice 3, self-pass)

Range: `37daea8..79800c6` on `feature/gameplay-dungeon-run`.
Rules: `docs/guidelines/game-code-changeability.md` R1–R33. Contract: `contract.md`.

Scope of this pass: the coordinator read the full diff (`git diff 37daea8..79800c6`) and the new files
(`FRunFactory.{h,cpp}`, `FRunProgressionSystem.h`, `FBehaviorContent.cpp`, `FDungeonContent.cpp`,
`FBehaviorTree.h`, `FGridStep.h`) before dispatching the independent passes. Two background delegates
(structure / correctness) and one oracle pass ran against the same range; their findings are recorded in
`pass-2.md`, `pass-3-correctness.md` and `verdict.md`.

## Automated evidence (harness output, not memory)

```text
$ bash scripts/verify-changeability.sh
ok    R1 clean / R2 clean / R3 clean / R3 event order clean / R4 clean / R5 clean
ok    R6 clean / R7 clean / R8 clean / R9 clean / R11 clean / R12 clean / R15 clean
ok    R16 clean / R18 clean / R19 clean / R20 clean / R21 clean / R23 clean / R24 clean
ok    R25 clean / R26 clean / R27 clean / R28 clean / R29 clean / R30 clean / R31 clean
ok    R32 clean / R33 clean
ok    R17 clean (schema_version declared on every asset)
ok    R22 clean (FContentLoader validates assets against declared schema)

NOT CHECKED (manual): R9 revert-diff deletability, R12 out-of-contract files, R13/R19 test seam,
R14/R21 registration sites, R24 debug surface, R30 variant by composition, R31/R32/R33 judgement calls.
changeability bundle: OK (repo)
```

## Findings raised and closed during implementation

- **[Medium, fixed] `src/gameplay/view/FBattleTextView.cpp:271` — run narration invisible in the hub.**
  `snapshot()` returned early when no battle entity exists (the hub and the frame right after 던전 클리어 /
  전멸), *before* `appendLog`. The transition narration ("던전 클리어: …", "전멸 — 런 종료") is queued by
  `FRunProgressionSystem` in exactly that frame, so the player could not read what had just ended until the
  next battle started. Fix: the no-battle path now draws the log as well (`FBattleTextView.cpp:273`).
  *Guided question:* is every narration line still visible in the state where it was produced?
- **[Low, open → see `verdict.md`] `assets/data/behaviors.json` — `proto_retreat` / `proto_wait_outnumbered`
  are not referenced by any unit.** They exist to cover the §2 vocabulary (`move_away_from_nearest_opponent`,
  `wait`, `opponent_count_at_or_above`) in data. Sample data that nothing references is the one way this slice
  can look speculative; the oracle pass had to decide whether R4/R9 makes that a defect.
- **[Low, open] `src/main.cpp:42` — stale comment** ("only after content and the battle spawned") now that the
  boot spawns a run, not a battle. Not in any worker's file list; left untouched so the diff stays inside the
  contract, and recorded here because the next slice's author will otherwise trust it.

## Verified during this pass (checked, not assumed)

- **Confirm cannot double-trigger a transition.** `FRunProgressionSystem` reads `EInputAction::Confirm` through
  `FInputState::isActionPressed` (`src/core/InputState.h:42`), which is edge-triggered (`keysJustPressed`), so
  the same press that ends a run cannot immediately re-enter a dungeon on the following frame.
- **Hub (no battle entity) is safe for the existing systems.** Every system that touches the battle guards
  `state == nullptr || state->battle == entt::null || !registry.valid(state->battle)`:
  `FTurnStartSystem.h:37`, `FBattleOutcomeSystem.h:34`, `FTurnEndSystem.h:39`, `FInitiativeSystem.h:35`;
  `FPlayerCommandSystem.h:45` requires an active unit. Boot no longer spawning a battle therefore does not
  crash the pipeline (`GameplayServices.cpp` sets `FRunState{phase = Hub}` only).
- **`FBattleFactory::destroyBattle` (`FGridStep`-adjacent teardown, `FBattleFactory.cpp:157`)** clears
  `battle`/`order`/`cursor` but intentionally leaves `gridWidth`/`gridHeight`; the next `buildBattle` writes
  both from the encounter row (`FBattleFactory.cpp:126-127`, fallback `8`), so a room with a different authored
  grid is not inherited from the previous room.
- **Movement unification keeps its observable behavior.** `applyStep` (`rules/FGridStep.h`) is the only writer
  of `FGridPosition` and still queues `FUnitMovedEvent` + `FMoveRejectedEvent` with the same Korean reasons the
  deleted copy in `FGridMoveSystem` produced, so the view and the log see the same lines as before.
- **`FBattleLog` (capacity 200, `run/FBattleLog.h:16`) is not cleared between rooms** — the log is run-scoped
  history on purpose; the view shows the tail, which is why the hub fix above matters.
