# Pillar 3 — Correctness & Exceptions (slice 3, independent pass)

Range reviewed: `37daea8..79800c6`; fix round: commit that follows `79800c6` on `feature/gameplay-dungeon-run`.
Pass owner: `reviewer` delegate (`del_mu4b5w1z_ku1d`), read-only, spec = `docs/design/dungeon-run.md` §1/§2 +
`contract.md` AC1~AC5. Two delegates died before producing a result on this range (`del_mu4b467u_6o09` — empty
`.out`, activity ended mid-read); the re-dispatched run delivered the findings below and was cut off by the
watchdog **before** its closing per-area summary and READY/REWORK line, so this document fixes that gap with the
coordinator's own verification of each raised item.

Harness (`bash scripts/verify-changeability.sh`): `changeability bundle: OK (repo)`, all R-blocks clean.
**Explicit caveat (from the review):** the harness cannot see a compile error — R20 only resolves includes. The
first finding below is exactly that class of defect, and it is why a green harness is not compile evidence.

## Findings and dispositions

| # | Sev | Finding | Disposition |
| --- | --- | --- | --- |
| C1 | **High** | `src/gameplay/systems/FSkillResolveSystem.h:103` + `:122` — the slice-3 refactor moved `const int distance = gridDistance(...)` *inside* the `if (!withinSkillRange(...))` rejection branch, while the success `LOG_INFO` at `:122` still printed `distance`: `error: 'distance' was not declared in this scope`. A build-breaking defect that the harness reported as clean. | **FIXED** — `distance` is computed once before the branch (`FSkillResolveSystem.h:100-108`), and both the rejection and the success log read it. The range *rule* is still single-sourced in `withinSkillRange` (the point of the refactor). |
| C2 | Medium | `FBehaviorContent.h` declared `threshold` optional, and `decodeBehavior` projects a missing value to `0.0`. After projection "absent" and "authored 0" are indistinguishable, and the registry's negative-threshold check cannot see absence: a rule authored without a threshold silently becomes "never fires" (`self_hp_at_or_below`) or "always fires" (`opponent_count_at_or_above`). Directly an AC5 gap. | **FIXED** — `threshold` is now a **required** schema field (`FBehaviorContent.h:54-60`), so `FContentLoader` rejects a row that omits it at load time (`FContentLoader.cpp:222`), and all 8 rows in `assets/data/behaviors.json` carry it (rows whose condition ignores it author `0`). The field comment records why required-but-often-unused is the honest schema here. |
| C3 | Medium | `dungeonRowValid` (`FContentRegistry.cpp`) checked a rooms/room_kinds mismatch, unknown kinds and unknown encounters, but not an **empty** `rooms` list — a dungeon with no rooms loaded fine and was only refused later by `FRunFactory::enterDungeon`, i.e. a runtime refusal for content that should never have loaded. | **FIXED** — `dungeonRowValid` now rejects an empty room list at load time with its own message (`FContentRegistry.cpp:67-74`). |
| C4 | Medium | Encounter cells were never checked for (a) being inside the authored grid or (b) colliding across sides — `cellsMatchUnitList` only counts coordinates against the unit list. A party cell equal to an enemy cell starts the battle with two units on one cell (occupancy is per side, so `FGridOccupancy` cannot catch it), and an out-of-bounds cell failed only at spawn, rolling the run back to the hub mid-session. | **FIXED** — two load-time checks added: `cellsInsideGrid` (both sides) and `cellsDisjointFromOpponent`, run after the grid-size check (`FContentRegistry.cpp:21-60`, wired at `:246-250`). |
| C5 | Low | `rules/FGridStep.h` used `std::abs` without including `<cstdlib>`, relying on a transitive include. | **FIXED** — `<cstdlib>` included (`FGridStep.h:14`). |
| C6 | Low | Raised by the oracle pass (Q5 gap list): the threshold check rejected only negatives, so `self_hp_at_or_below: 3` (a ratio > 1) always fired and a fractional `opponent_count_at_or_above` was silently truncated to a different count than the author read — silent rule inversions, so the AC5 "런타임 열화 없음" promise did not hold for them. | **FIXED** — the registry now checks each condition's own domain: ratio thresholds must be `0~1`, count thresholds must be a whole number `>= 0` (`FContentRegistry.cpp:248-266`). |

## Verified during this pass (checked, not assumed)

- **Run-cycle refusals stay player-visible.** `FRunFactory::enterDungeon` refuses an unknown dungeon and an empty
  room list with `LOG_ERROR` and leaves the phase as `Hub`; `advanceRoom` puts the run back in the hub when the
  next room cannot spawn rather than stranding it on a null battle; every refusal the player can trigger
  (movement AP/bounds/occupancy via `applyStep`, skill range/AP/target via `FSkillResolveSystem`) queues an event
  the battle log renders.
- **Determinism inputs** (AC2): the monster's decision reads only `selfHealthPct`, `allyDowned`,
  `opponentInSkillRange`, `livingOpponents`, `actorHasSkill`; the opponent list comes from the sorted
  `livingOpponents` view and the behaviour list order is authored data — no `unordered_*` iteration, no time or
  input-polling input. The run seed is fixed by `FRunState` (design §1).
- **One action per turn holds on every path** of `FEnemyTurnSystem::update`: the decision event, at most one
  action, and `FTurnEndRequestedEvent` are queued unconditionally at `FEnemyTurnSystem.h:50-75`.
- **The new out-of-range refusal reuses the existing channel.** When a matched rule picks `use_first_skill` and no
  target is in range, the executor now queues `FSkillRejectedEvent{unit, skill->id, "사거리 안에 상대가 없다"}`
  (`FEnemyTurnSystem.h:180-190`) instead of only logging, so the player's "decided → did" pair is complete
  (fix for the structure pass's finding #4).

## Tooling note (false positive, do not "fix")

`pi-lens` reports `bit fields should not be used` on `src/core/data/FContentValue.h:14-17`,
`src/core/data/FContentLoader.h:18` and `src/core/AssetManager.h` on every edit in this slice. None of these files
is part of the slice (`git status` shows `src/core/` untouched), and none contains a bit field:

```text
$ grep -rnE "^\s*(const\s+)?[A-Za-z_][A-Za-z0-9_:<>, ]*\s+[A-Za-z_][A-Za-z0-9_]*\s*:\s*[0-9]+\s*;" --include=*.h --include=*.cpp src/
(no output)
$ sed -n '14,17p' src/core/data/FContentValue.h
    EContentFieldType type = EContentFieldType::Text;
    std::variant<std::string, double, bool, std::vector<std::string>, std::vector<double>> value{std::string{}};
```

The rule misparses `enum`-member initialisers / `std::variant` templates as bit-field declarations. Reported as
a linter bug, not as a code defect; touching `src/core/` to silence it would be unrelated churn outside the
contract.
