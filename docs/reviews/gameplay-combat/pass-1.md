# Pillar 1 — Correctness & Exceptions (slice 2, self-pass)

Range: `a9b149e..6b432cd` on `feature/gameplay-combat-text`.
Rules: `docs/guidelines/game-code-changeability.md` R1–R33. Contract: `contract.md`.

## Automated evidence (harness output, not memory)

```text
$ bash scripts/verify-changeability.sh
ok    R1 clean / R2 clean / R3 clean / R4 clean / R5 clean / R6 clean / R7 clean / R8 clean
ok    R9 clean / R11 clean / R12 clean / R15 clean / R16 clean / R18 clean / R19 clean
ok    R20 clean / R21 clean / R23 clean / R24 clean / R25 clean / R26 clean / R27 clean
ok    R28 clean / R29 clean / R30 clean / R31 clean / R32 clean
ok    R33 clean
ok    R17 clean (schema_version declared on every asset)
ok    R22 clean (FContentLoader validates assets against declared schema)
changeability bundle: OK (repo)

$ bash scripts/verify-changeability.sh --calibrate
ok    R2 gameplay/components/FZzBadGameplayComponent.h = 3 (want 3)
ok    R1 gameplay/systems/ZzMismatchSystem.h = 1 (want 1)
ok    R1 gameplay fixture: filename truly != type name (detectable)
ok    R26 gameplay/ZzSdlGameplay.h = 1 (want 1)
ok    R32 flags a silent gameplay system (no LOG_*, no span)
ok    R32 negative control: a ZoneScopedN gameplay system is not flagged
changeability bundle: OK (calibrate)
```

## Findings raised and closed during implementation

- **[High, fixed] `src/gameplay/systems/FTurnStartSystem.h:90` — R5.** `registry.emplace<FTurnActive>(candidate)`
  attached a component outside a factory; the harness's R5 pattern matches a bare-identifier argument, so this was
  the only hit. Fix: `FBattleFactory::{openTurn,closeTurn,markDowned,endBattleAsVictory,endBattleAsDefeat}` now own
  every structural change on a live entity, and the three systems call them. Re-run: `R5 clean`.
  *Guided question:* can "which components can a live entity gain or lose?" be answered from one file? Now yes —
  `src/gameplay/factories/FBattleFactory.{h,cpp}`.
- **[High, fixed] Harness discovery gap (R1/R20/R32).** The system-file greps used the literal `'public ISystem'`,
  which does not match the gameplay layer's required qualification `: public ecs::ISystem`. Every gameplay system
  was invisible to R1/R20/R32 while the bundle still printed `clean`. Fix: pattern `'public +(ecs::)?ISystem'` in
  the R32 detector and both discovery loops, plus the `assert_hits R1 gameplay/systems/ZzMismatchSystem.h 1` and
  `R32 ... ZzQuietGameplaySystem.h = 1` calibrations (with the `ZzObservedGameplaySystem.h` control).
  *Guided question:* would a rule have caught this class of bug? Only repo-mode `clean` would have hidden it —
  which is why each new target list got a fixture with a negative control.
- **[Medium, fixed] R33 false positive on the repo's own decision marker.** The two `// 미결(design §4): …` lines
  (`src/gameplay/data/FEncounterContent.cpp:14`, `src/gameplay/data/FSkillContent.cpp:14`) were reported as
  restatements: their Korean words are dropped by the word extractor, leaving ASCII tokens (`design`, `power`,
  `encounter`, `8x8`) that also occur in the next code line. Fix: `미결(` is now a sanctioned R33 marker in both the
  harness and the guideline, calibrated by `tests/changeability/fixtures/core/ZzMideokComment.h` (the marked line
  is skipped, an identical unmarked line is still flagged).
- **[Low, fixed] Contract drift.** `contract.md` listed a `FBattlePlayerPhase` component that the implementation
  does not have (the active side is already expressed by `FTeamPlayer`/`FTeamEnemy` + `FTurnActive`); the phase tag
  is `FBattleOngoing`. Contract corrected, with the reason recorded there.
- **[Info] `docs/reviews/gameplay-combat/tooling-notes.md`.** The editor-side C/C++ lens check reports phantom
  `bit fields should not be used` findings (0 bitfield declarations in the flagged files; the lines it points at
  are `EContentFieldType::Text`, a `std::variant`, and `[[nodiscard]]` accessors) and cannot resolve include roots
  (`'entt/entt.hpp' file not found`). The flagged files are untouched by this branch (`git status` count 0). It is
  reported as `NOT USABLE`, not as a pass — no already-reviewed file was edited to satisfy it.

## Correctness review of the new turn loop (manual, no harness rule covers these)

- Actor/turn guard: `FSkillResolveSystem` rejects any request whose actor is not the holder of `FTurnActive`, which
  is the single defence against a queued request landing after the turn moved on.
- Downed-during-own-turn: `FBattleFactory::markDowned` removes `FTurnActive`, so a unit dropped by damage cannot
  act again; `FInitiativeSystem` and `FTurnStartSystem` both skip `FDowned` units.
- Mutual wipe: `FBattleOutcomeSystem` checks "no living enemies" first and documents why (party = run investment),
  so a draw is never reported as a loss.
- Terminal-phase idempotence: both the outcome system and the two turn systems return early once
  `FBattleVictory`/`FBattleDefeat` is present, so the end event is published once, not per frame.
- Empty/None paths: no battle, no active unit, no skills, no opponents, missing content row, missing `FApPool` —
  each has an explicit branch with a log line, and none of them silently continues the battle.
- Determinism: initiative ties break on entity id, target lists are sorted, and the order is rebuilt from a total
  order, so nothing depends on registry storage layout (R16).

## Not verifiable in this session

- Compilation and runtime: the MSVC build + `scripts/check-windows.ps1` are the owner's gate; no compiler ran here.
- R1 "a system must not mutate ≥3 component groups", R9 revert-diff, R13/R14/R19/R21/R31 applicability per file and
  R32 debug-view wiring are on the harness's `NOT CHECKED (manual)` list — pass-2 is expected to answer them.
